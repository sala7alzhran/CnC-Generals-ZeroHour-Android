package org.generalsx.zerohour;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.provider.OpenableColumns;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Properties;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * @port Android — copies the player's own Zero Hour files into the app's private files dir
 * (where SDL3Main.cpp chdir()s), either from a folder picked with ACTION_OPEN_DOCUMENT_TREE or
 * from a .zip picked with ACTION_OPEN_DOCUMENT. Runs on a background thread.
 *
 * Files are written as "name.part" and renamed when complete, so a retry after an interruption
 * skips everything already copied (same name and size) instead of starting over.
 */
final class GameDataImporter {

    interface Progress {
        /** {@code phase} is R.string.setup_copying or R.string.setup_unzipping. */
        void onProgress(int phase, String file, long done, long total);
    }

    /** A failure with a message meant for the player. */
    static final class ImportException extends Exception {
        ImportException(String message) { super(message); }
    }

    /**
     * Free space kept in reserve beyond the game data: engine logs, saves, the DXVK cache, and one
     * archive's worth of room while its sounds are being re-encoded.
     */
    private static final long SPACE_MARGIN = 320L << 20;
    private static final String ZIP_STAGING = ".import-zip";
    /** Archives already shrunk by {@link BigAudioCompactor}: relative path = size of the original. */
    private static final String COMPACT_LOG = ".compacted";

    private final Context mContext;
    private final ContentResolver mResolver;
    private final File mDest;
    private final Progress mProgress;
    private final byte[] mBuf = new byte[1 << 20];

    GameDataImporter(Context context, File dest, Progress progress) {
        mContext = context;
        mResolver = context.getContentResolver();
        mDest = dest;
        mProgress = progress;
    }

    // ---- folder ------------------------------------------------------------------------------

    private static final class Doc {
        final String rel; final String id; final long size; final boolean dir;
        Doc(String rel, String id, long size, boolean dir) {
            this.rel = rel; this.id = id; this.size = size; this.dir = dir;
        }
    }

    /** Returns the number of bytes now installed. */
    long importTree(Uri tree) throws IOException, ImportException {
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        List<Doc> top = listChildren(tree, rootId, "");

        // Accept the Zero Hour folder itself or its parent (e.g. "Download" holding it).
        List<Doc> rootDocs = top;
        if (findName(top, GameDataLayout.MARKER_FILE) == null) {
            for (Doc d : top) {
                if (!d.dir) continue;
                List<Doc> inner = listChildren(tree, d.id, "");
                if (findName(inner, GameDataLayout.MARKER_FILE) != null) { rootDocs = inner; break; }
            }
        }
        if (findName(rootDocs, GameDataLayout.MARKER_FILE) == null) {
            throw new ImportException(mContext.getString(R.string.setup_err_no_root));
        }

        List<Doc> files = new ArrayList<>();
        for (Doc d : rootDocs) {
            if (!d.dir) {
                files.add(d);
            } else if (d.rel.equalsIgnoreCase(GameDataLayout.DATA_DIR)) {
                collectFiles(tree, d, files);
            } else if (d.rel.equalsIgnoreCase(GameDataLayout.BASE_DIR)) {
                for (Doc c : listChildren(tree, d.id, d.rel + "/")) if (!c.dir) files.add(c);
            }
        }
        List<String> rels = new ArrayList<>();
        for (Doc d : files) rels.add(d.rel);
        checkComplete(rels);

        Properties compacted = loadCompactLog();
        List<Doc> toCopy = new ArrayList<>();
        List<String> imported = new ArrayList<>();
        long total = 0, pending = 0;
        for (Doc d : files) {
            if (!GameDataLayout.shouldImport(d.rel)) continue;
            toCopy.add(d);
            imported.add(d.rel);
            total += d.size;
            if (!alreadyInstalled(compacted, d.rel, d.size)) pending += d.size;
        }
        checkSpace(pending);

        long done = 0;
        for (Doc d : toCopy) {
            if (alreadyInstalled(compacted, d.rel, d.size)) { done += d.size; continue; }
            File out = new File(mDest, d.rel);
            compacted.remove(d.rel);
            Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, d.id);
            try (InputStream in = mResolver.openInputStream(uri)) {
                if (in == null) throw new IOException("cannot open " + d.rel);
                done = writeFile(in, out, d.rel, R.string.setup_copying, done, total);
            }
        }
        saveCompactLog(compacted);
        return compactSounds(imported);
    }

    /** A file is in place if it was copied whole, or copied and then shrunk by the compactor. */
    private boolean alreadyInstalled(Properties compacted, String rel, long sourceSize) {
        File out = new File(mDest, rel);
        if (!out.isFile()) return false;
        if (out.length() == sourceSize) return true;
        return String.valueOf(sourceSize).equals(compacted.getProperty(rel));
    }

    private void collectFiles(Uri tree, Doc dir, List<Doc> out) throws IOException {
        for (Doc c : listChildren(tree, dir.id, dir.rel + "/")) {
            if (c.dir) collectFiles(tree, c, out); else out.add(c);
        }
    }

    private List<Doc> listChildren(Uri tree, String parentId, String prefix) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] cols = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
                          Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE };
        List<Doc> out = new ArrayList<>();
        try (Cursor c = mResolver.query(children, cols, null, null, null)) {
            if (c == null) throw new IOException("cannot list " + (prefix.isEmpty() ? "folder" : prefix));
            while (c.moveToNext()) {
                boolean dir = Document.MIME_TYPE_DIR.equals(c.getString(2));
                long size = c.isNull(3) ? 0 : c.getLong(3);
                out.add(new Doc(prefix + c.getString(1), c.getString(0), size, dir));
            }
        }
        return out;
    }

    private static Doc findName(List<Doc> docs, String name) {
        for (Doc d : docs) {
            if (!d.dir && d.rel.substring(d.rel.lastIndexOf('/') + 1).equalsIgnoreCase(name)) return d;
        }
        return null;
    }

    // ---- zip ---------------------------------------------------------------------------------

    /** Returns the number of bytes now installed. */
    long importZip(Uri zip) throws IOException, ImportException {
        long zipSize = querySize(zip);
        if (zipSize > 0) checkSpace(zipSize);

        // The install root inside the zip is only known once every entry has been seen, so
        // unpack the candidate files into a staging dir first, then move them into place.
        File staging = new File(mDest, ZIP_STAGING);
        deleteTree(staging);
        List<String> rels = new ArrayList<>();
        try (InputStream raw = mResolver.openInputStream(zip)) {
            if (raw == null) throw new IOException("cannot open the zip file");
            CountingInputStream counted = new CountingInputStream(new BufferedInputStream(raw, 1 << 20));
            ZipInputStream zis = new ZipInputStream(counted);
            ZipEntry e;
            while ((e = zis.getNextEntry()) != null) {
                String rel = GameDataLayout.safeRelativePath(e.getName());
                if (e.isDirectory() || rel == null || !GameDataLayout.mayImportFromArchive(rel)) continue;
                writeEntry(zis, new File(staging, rel), rel, counted, zipSize);
                rels.add(rel);
            }
        }

        String root = GameDataLayout.findRoot(rels);
        if (root == null) {
            deleteTree(staging);
            throw new ImportException(mContext.getString(R.string.setup_err_no_root));
        }
        List<String> underRoot = new ArrayList<>();
        for (String rel : rels) {
            if (rel.length() > root.length() && rel.regionMatches(true, 0, root, 0, root.length())) {
                underRoot.add(rel.substring(root.length()));
            }
        }
        try {
            checkComplete(underRoot);
        } catch (ImportException missing) {
            deleteTree(staging);
            throw missing;
        }

        long total = 0;
        List<String> imported = new ArrayList<>();
        File stagedRoot = new File(staging, root);
        for (String rel : underRoot) {
            if (!GameDataLayout.shouldImport(rel)) continue;
            File from = new File(stagedRoot, rel), to = new File(mDest, rel);
            File parent = to.getParentFile();
            if (parent != null) parent.mkdirs();
            if (to.exists() && !to.delete()) throw new IOException("cannot replace " + rel);
            if (!from.renameTo(to)) throw new IOException("cannot move " + rel);
            total += to.length();
            imported.add(rel);
        }
        deleteTree(staging);
        Properties compacted = loadCompactLog();
        for (String rel : imported) compacted.remove(rel);   // freshly unpacked: not shrunk yet
        saveCompactLog(compacted);
        return compactSounds(imported);
    }

    private void writeEntry(InputStream in, File out, String rel, CountingInputStream counted,
                            long zipSize) throws IOException {
        File parent = out.getParentFile();
        if (parent != null) parent.mkdirs();
        long lastReport = 0;
        try (OutputStream os = new FileOutputStream(out)) {
            int r;
            while ((r = in.read(mBuf)) != -1) {
                os.write(mBuf, 0, r);
                long now = counted.count;
                if (now - lastReport >= (8L << 20)) {
                    lastReport = now;
                    mProgress.onProgress(R.string.setup_unzipping, rel, now, zipSize);
                }
            }
        }
        mProgress.onProgress(R.string.setup_unzipping, rel, counted.count, zipSize);
    }

    private long querySize(Uri uri) {
        try (Cursor c = mResolver.query(uri, new String[] { OpenableColumns.SIZE }, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getLong(0);
        } catch (RuntimeException ignored) {
            // Size is only used for progress and the space check.
        }
        return -1;
    }

    private static final class CountingInputStream extends FilterInputStream {
        long count;
        CountingInputStream(InputStream in) { super(in); }
        @Override public int read() throws IOException {
            int b = super.read();
            if (b >= 0) count++;
            return b;
        }
        @Override public int read(byte[] b, int off, int len) throws IOException {
            int n = super.read(b, off, len);
            if (n > 0) count += n;
            return n;
        }
        @Override public long skip(long n) throws IOException {
            long s = super.skip(n);
            count += s;
            return s;
        }
    }

    // ---- sound compaction ----------------------------------------------------------------------

    /**
     * Re-encodes the uncompressed sounds inside the imported .big archives (about 400 MB less on
     * the phone). Archives already done are skipped, so an interrupted import resumes. Returns
     * the number of bytes the imported files now take.
     */
    private long compactSounds(List<String> rels) throws IOException {
        Properties compacted = loadCompactLog();
        List<String> todo = new ArrayList<>();
        long todoBytes = 0;
        for (String rel : rels) {
            File f = new File(mDest, rel);
            if (!rel.toLowerCase(Locale.ROOT).endsWith(".big") || !f.isFile() || compacted.containsKey(rel)) continue;
            if (BigAudioCompactor.worthCompacting(f)) { todo.add(rel); todoBytes += f.length(); }
        }
        long doneBefore = 0;
        for (String rel : todo) {
            File f = new File(mDest, rel);
            File tmp = new File(f.getPath() + ".compact");
            final long base = doneBefore;
            final long all = todoBytes;
            final String name = rel;
            long originalSize = f.length();
            final long[] lastReport = { -1 };
            BigAudioCompactor.compact(f, tmp, (done, total) -> {
                if (lastReport[0] < 0 || done - lastReport[0] >= (8L << 20) || done == total) {
                    lastReport[0] = done;
                    mProgress.onProgress(R.string.setup_optimizing, name, base + done, all);
                }
            });
            if (!f.delete() || !tmp.renameTo(f)) throw new IOException("cannot replace " + rel);
            compacted.setProperty(rel, String.valueOf(originalSize));
            saveCompactLog(compacted);
            doneBefore += originalSize;
        }
        long installed = 0;
        for (String rel : rels) installed += new File(mDest, rel).length();
        return installed;
    }

    private Properties loadCompactLog() {
        Properties p = new Properties();
        File f = new File(mDest, COMPACT_LOG);
        if (f.isFile()) {
            try (InputStream in = new FileInputStream(f)) {
                p.load(in);
            } catch (IOException ignored) {
                // A lost log only means archives get copied again.
            }
        }
        return p;
    }

    private void saveCompactLog(Properties p) throws IOException {
        mDest.mkdirs();
        try (OutputStream os = new FileOutputStream(new File(mDest, COMPACT_LOG))) {
            p.store(os, null);
        }
    }

    // ---- shared ------------------------------------------------------------------------------

    private void checkComplete(List<String> rels) throws ImportException {
        List<String> missing = GameDataLayout.findMissing(rels);
        if (missing.isEmpty()) return;
        String msg = mContext.getString(R.string.setup_err_missing, String.join(", ", missing));
        if (GameDataLayout.missesBaseArchives(missing)) {
            msg += "\n\n" + mContext.getString(R.string.setup_err_missing_base);
        }
        throw new ImportException(msg);
    }

    private void checkSpace(long needed) throws ImportException {
        mDest.mkdirs();
        long free = mDest.getUsableSpace();
        if (needed + SPACE_MARGIN > free) {
            throw new ImportException(mContext.getString(R.string.setup_err_space,
                    SetupActivity.formatSize(mContext, needed + SPACE_MARGIN),
                    SetupActivity.formatSize(mContext, free)));
        }
    }

    /** Copies {@code in} to {@code out} via a .part file; returns the updated byte count. */
    private long writeFile(InputStream in, File out, String rel, int phase, long done, long total)
            throws IOException {
        File parent = out.getParentFile();
        if (parent != null) parent.mkdirs();
        File part = new File(out.getPath() + ".part");
        long lastReport = done;
        try (OutputStream os = new FileOutputStream(part)) {
            int r;
            while ((r = in.read(mBuf)) != -1) {
                os.write(mBuf, 0, r);
                done += r;
                if (done - lastReport >= (8L << 20)) {
                    lastReport = done;
                    mProgress.onProgress(phase, rel, done, total);
                }
            }
        }
        if (out.exists() && !out.delete()) throw new IOException("cannot replace " + rel);
        if (!part.renameTo(out)) throw new IOException("cannot rename " + part.getName());
        mProgress.onProgress(phase, rel, done, total);
        return done;
    }

    static void deleteTree(File f) {
        File[] children = f.listFiles();
        if (children != null) for (File c : children) deleteTree(c);
        f.delete();
    }
}
