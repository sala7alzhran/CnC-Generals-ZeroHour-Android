package org.generalsx.zerohour;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * @port Android — shrinks an imported .big archive by re-encoding its uncompressed 16-bit .wav
 * files as IMA ADPCM ({@link ImaAdpcmWav}); every other entry is copied byte for byte. About
 * 550 MB of the retail sound effects and base-game speech ship as plain PCM, so this saves
 * roughly 400 MB on the phone. The engine only reads each entry's offset and size from the
 * index (StdBIGFileSystem::openArchiveFile), so the rewritten archive keeps the same names in
 * the same order with the data packed after the index.
 *
 * Plain Java (no Android types) so it can be unit-tested on the build machine.
 */
final class BigAudioCompactor {

    private static final int HEADER_LEN = 16;

    /** Called with the bytes processed so far, for a progress bar. */
    interface Progress { void onProgress(long done, long total); }

    private static final class Entry {
        final String name; final long offset; final int size;
        Entry(String name, long offset, int size) { this.name = name; this.offset = offset; this.size = size; }
    }

    private BigAudioCompactor() {}

    /** True if {@code file} is a BIG archive that holds at least one 16-bit PCM .wav. */
    static boolean worthCompacting(File file) throws IOException {
        try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
            List<Entry> entries = readIndex(raf);
            if (entries == null) return false;
            byte[] head = new byte[512];
            for (Entry e : entries) {
                if (!e.name.toLowerCase().endsWith(".wav")) continue;
                int n = Math.min(head.length, e.size);
                raf.seek(e.offset);
                raf.readFully(head, 0, n);
                byte[] probe = n == head.length ? head : java.util.Arrays.copyOf(head, n);
                if (ImaAdpcmWav.parsePcm16(probe) != null) return true;
            }
        }
        return false;
    }

    /**
     * Writes a compacted copy of {@code in} to {@code out}. Returns false (and writes nothing)
     * if {@code in} is not a BIG archive.
     */
    static boolean compact(File in, File out, Progress progress) throws IOException {
        try (RandomAccessFile raf = new RandomAccessFile(in, "r")) {
            List<Entry> entries = readIndex(raf);
            if (entries == null) return false;

            long total = 0;
            for (Entry e : entries) total += e.size;

            // The index length doesn't depend on the data, so offsets can be assigned as we go:
            // write a placeholder index, then the data, then the real index on top.
            int indexLen = HEADER_LEN;
            for (Entry e : entries) indexLen += 8 + e.name.getBytes(StandardCharsets.ISO_8859_1).length + 1;

            long[] newOffset = new long[entries.size()];
            int[] newSize = new int[entries.size()];
            long pos = indexLen;
            try (OutputStream os = new BufferedOutputStream(new FileOutputStream(out), 1 << 20)) {
                os.write(new byte[indexLen]);   // the real index is written once the offsets are known
                long done = 0;
                for (int i = 0; i < entries.size(); i++) {
                    Entry e = entries.get(i);
                    byte[] data = new byte[e.size];
                    raf.seek(e.offset);
                    raf.readFully(data);
                    if (e.name.toLowerCase().endsWith(".wav")) {
                        ImaAdpcmWav.PcmWav pcm = ImaAdpcmWav.parsePcm16(data);
                        if (pcm != null) data = ImaAdpcmWav.encode(data, pcm);
                    }
                    os.write(data);
                    newOffset[i] = pos;
                    newSize[i] = data.length;
                    pos += data.length;
                    done += e.size;
                    if (progress != null) progress.onProgress(done, total);
                }
            }
            if (pos > 0xFFFFFFFFL) throw new IOException(in.getName() + ": archive too large");

            ByteBuffer index = ByteBuffer.allocate(indexLen);
            index.put("BIGF".getBytes(StandardCharsets.ISO_8859_1));
            index.order(ByteOrder.LITTLE_ENDIAN).putInt((int) pos);   // archive size, little endian
            index.order(ByteOrder.BIG_ENDIAN).putInt(entries.size()).putInt(indexLen);
            for (int i = 0; i < entries.size(); i++) {
                index.putInt((int) newOffset[i]).putInt(newSize[i]);
                index.put(entries.get(i).name.getBytes(StandardCharsets.ISO_8859_1)).put((byte) 0);
            }
            try (RandomAccessFile dst = new RandomAccessFile(out, "rw")) {
                dst.seek(0);
                dst.write(index.array());
            }
        }
        return true;
    }

    /** The archive's index, or null if the file is not a BIG archive. */
    private static List<Entry> readIndex(RandomAccessFile raf) throws IOException {
        if (raf.length() < HEADER_LEN) return null;
        byte[] magic = new byte[4];
        raf.seek(0);
        raf.readFully(magic);
        String m = new String(magic, StandardCharsets.ISO_8859_1);
        if (!m.equals("BIGF") && !m.equals("BIG4")) return null;
        raf.seek(8);
        int count = raf.readInt();   // big endian
        if (count < 0 || count > 1_000_000) return null;
        raf.seek(HEADER_LEN);
        List<Entry> entries = new ArrayList<>(count);
        StringBuilder name = new StringBuilder();
        for (int i = 0; i < count; i++) {
            long offset = raf.readInt() & 0xFFFFFFFFL;
            int size = raf.readInt();
            if (size < 0 || offset + size > raf.length()) return null;
            name.setLength(0);
            int c;
            while ((c = raf.read()) > 0) name.append((char) c);
            if (c < 0) return null;
            entries.add(new Entry(name.toString(), offset, size));
        }
        return entries;
    }
}
