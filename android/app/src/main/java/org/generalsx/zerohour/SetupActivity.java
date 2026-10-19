package org.generalsx.zerohour;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;
import android.text.format.Formatter;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * @port Android — first-run game-data setup, the app's launcher.
 *
 * The native engine reads the game files from the app's internal files dir (SDL3Main.cpp
 * chdir()s there). Two ways to fill it:
 *  - Self-contained build: the data is bundled in the APK as gamedataNNN.pak parts (see
 *    scripts/package_selfcontained.py) and unpacked here on first launch.
 *  - Engine-only build (the CI APK): the player picks the Zero Hour folder they copied to the
 *    phone, or a .zip of it, and GameDataImporter copies the needed files in.
 * A completion marker is written last; once it exists every launch goes straight to the game.
 */
public class SetupActivity extends Activity {

    private static final String MARKER = ".gamedata_ok_v1";
    private static final int REQUEST_FOLDER = 1;
    private static final int REQUEST_ZIP = 2;

    private TextView mStatus;
    private ProgressBar mProgress;
    private View mPickButtons;
    private Button mStartButton;
    private volatile boolean mWorking;
    private long mLastUiUpdate;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        File marker = new File(getFilesDir(), MARKER);
        if (marker.exists()) {
            // Already installed — go straight to the game.
            launchGame();
            return;
        }

        buildUi();
        if (hasBundledData()) {
            mPickButtons.setVisibility(View.GONE);
            mStatus.setText(R.string.setup_installing);
            startWork(this::extract, "gamedata-extract");
        } else {
            mStatus.setText(R.string.setup_intro);
        }
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(dp(32), dp(16), dp(32), dp(16));

        TextView title = new TextView(this);
        title.setText(R.string.setup_title);
        title.setTextColor(Color.WHITE);
        title.setTextSize(26);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        mStatus = new TextView(this);
        mStatus.setTextColor(Color.rgb(0xF0, 0xE0, 0xC0));
        mStatus.setTextSize(16);
        mStatus.setGravity(Gravity.CENTER);
        mStatus.setPadding(0, dp(16), 0, dp(16));
        root.addView(mStatus);

        mProgress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        mProgress.setMax(1000);
        mProgress.setVisibility(View.GONE);
        root.addView(mProgress, new LinearLayout.LayoutParams(dp(480), ViewGroup.LayoutParams.WRAP_CONTENT));

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER);
        buttons.addView(makeButton(R.string.setup_pick_folder, v -> pickFolder()));
        buttons.addView(makeButton(R.string.setup_pick_zip, v -> pickZip()));
        mPickButtons = buttons;
        root.addView(buttons);

        mStartButton = makeButton(R.string.setup_start, v -> launchGame());
        mStartButton.setVisibility(View.GONE);
        root.addView(mStartButton);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.rgb(0xB0, 0x12, 0x12));
        scroll.addView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
    }

    private Button makeButton(int text, View.OnClickListener onClick) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextSize(18);
        b.setAllCaps(false);
        b.setPadding(dp(24), dp(12), dp(24), dp(12));
        b.setOnClickListener(onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(8), dp(8), dp(8), dp(8));
        b.setLayoutParams(lp);
        return b;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    static String formatSize(Context context, long bytes) {
        return Formatter.formatShortFileSize(context, bytes);
    }

    private boolean hasBundledData() {
        try (InputStream in = getAssets().open("gamedata000.pak")) {
            return true;
        } catch (IOException notBundled) {
            return false;
        }
    }

    // ---- import from the player's files ------------------------------------------------------

    private void pickFolder() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        startActivityForResult(i, REQUEST_FOLDER);
    }

    private void pickZip() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/zip", "application/x-zip-compressed", "application/octet-stream" });
        startActivityForResult(i, REQUEST_ZIP);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null || mWorking) return;
        final Uri uri = data.getData();
        final boolean zip = requestCode == REQUEST_ZIP;
        mPickButtons.setVisibility(View.GONE);
        mProgress.setProgress(0);
        mProgress.setVisibility(View.VISIBLE);
        mStatus.setText(R.string.setup_scanning);
        startWork(() -> runImport(uri, zip), "gamedata-import");
    }

    private void runImport(Uri uri, boolean zip) {
        GameDataImporter importer = new GameDataImporter(this, getFilesDir(), this::onImportProgress);
        try {
            long bytes = zip ? importer.importZip(uri) : importer.importTree(uri);
            writeMarker("imported " + bytes + " bytes\n");
            runOnUiThread(() -> {
                mProgress.setVisibility(View.GONE);
                mStatus.setText(getString(R.string.setup_done, formatSize(this, bytes)));
                mStartButton.setVisibility(View.VISIBLE);
            });
        } catch (GameDataImporter.ImportException e) {
            showError(e.getMessage());
        } catch (Throwable e) {
            android.util.Log.e("GeneralsSetup", "import failed", e);
            String reason = e.getMessage() != null ? e.getMessage() : e.toString();
            if (reason.contains("ENOSPC")) reason = getString(R.string.setup_err_disk_full);
            showError(getString(R.string.setup_err_failed, reason));
        }
    }

    private void onImportProgress(int phase, String file, long done, long total) {
        long now = SystemClock.uptimeMillis();
        if (now - mLastUiUpdate < 200 && done < total) return;
        mLastUiUpdate = now;
        final String text = total > 0
                ? getString(phase, file, formatSize(this, done), formatSize(this, total))
                : getString(phase, file, formatSize(this, done), "?");
        final int permille = total > 0 ? (int) (done * 1000 / total) : 0;
        runOnUiThread(() -> {
            mStatus.setText(text);
            mProgress.setProgress(permille);
        });
    }

    private void showError(String message) {
        runOnUiThread(() -> {
            mProgress.setVisibility(View.GONE);
            mStatus.setText(message);
            mPickButtons.setVisibility(View.VISIBLE);
        });
    }

    // ---- bundled .pak parts (self-contained build) --------------------------------------------

    private void setStatus(final String s) {
        runOnUiThread(() -> { if (mStatus != null) mStatus.setText(s); });
    }

    private void extract() {
        File filesDir = getFilesDir();
        long bytes = 0;
        int count = 0;
        try {
            // The game-data zip is bundled split into <2GB parts (gamedata000.pak, 001, …)
            // because AGP can't process a single >2GB asset. Concatenate the parts back into
            // the original zip byte-stream. ACCESS_STREAMING so parts aren't mmap'd.
            java.util.Vector<InputStream> parts = new java.util.Vector<>();
            for (int p = 0; ; p++) {
                String name = String.format(java.util.Locale.US, "gamedata%03d.pak", p);
                try {
                    parts.add(getAssets().open(name, android.content.res.AssetManager.ACCESS_STREAMING));
                } catch (java.io.IOException notFound) {
                    break;
                }
            }
            if (parts.isEmpty()) throw new java.io.IOException("no gamedata*.pak parts in APK assets");
            setStatus(getString(R.string.setup_installing_parts, parts.size()));
            InputStream rawIn = new java.io.SequenceInputStream(parts.elements());
            ZipInputStream zis = new ZipInputStream(new BufferedInputStream(rawIn, 1 << 20));
            byte[] buf = new byte[1 << 20];
            ZipEntry e;
            while ((e = zis.getNextEntry()) != null) {
                String rel = GameDataLayout.safeRelativePath(e.getName());
                if (rel == null) {
                    continue;   // directory entry (created below as needed) or unsafe path
                }
                File out = new File(filesDir, rel);
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                FileOutputStream fos = new FileOutputStream(out);
                BufferedOutputStream bos = new BufferedOutputStream(fos, 1 << 20);
                int r;
                while ((r = zis.read(buf)) != -1) {
                    bos.write(buf, 0, r);
                    bytes += r;
                }
                bos.flush();
                bos.close();
                fos.close();
                zis.closeEntry();
                count++;
                if ((count % 4) == 0) {
                    setStatus(getString(R.string.setup_installing_progress, count, bytes / (1024 * 1024)));
                }
            }
            zis.close();

            // Completion marker written LAST so an interrupted extract re-runs cleanly.
            writeMarker("ok " + count + " files " + bytes + " bytes\n");

            setStatus(getString(R.string.setup_starting));
            runOnUiThread(this::launchGame);
        } catch (Throwable ex) {
            setStatus(getString(R.string.setup_install_failed, String.valueOf(ex)));
        }
    }

    // ---- common ------------------------------------------------------------------------------

    private void startWork(Runnable work, String name) {
        mWorking = true;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        new Thread(() -> {
            try {
                work.run();
            } finally {
                mWorking = false;
                runOnUiThread(() -> getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON));
            }
        }, name).start();
    }

    private void writeMarker(String content) throws IOException {
        try (FileOutputStream mk = new FileOutputStream(new File(getFilesDir(), MARKER))) {
            mk.write(content.getBytes("UTF-8"));
        }
    }

    @Override
    public void onBackPressed() {
        // Leaving mid-copy is harmless (a retry resumes), but an accidental back-swipe
        // shouldn't silently stop a multi-GB copy.
        if (!mWorking) super.onBackPressed();
    }

    private void launchGame() {
        Intent i = new Intent(this, ZerohourActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        startActivity(i);
        finish();
    }
}
