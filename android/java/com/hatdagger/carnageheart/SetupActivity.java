package com.hatdagger.carnageheart;

import android.app.Activity;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * First screen. Unpacks the game files from the APK (after every install or
 * update), and the first time asks the player to pick their Carnage Heart disc
 * (the .cue and its .bin tracks, or a .chd). The files are copied into the
 * app's own storage so the game can read them directly. Then the game opens.
 */
public class SetupActivity extends Activity {
    private static final int PICK_DISC = 1;
    private static final int GREEN = Color.rgb(0x7C, 0xFF, 0x9A);
    private static final int BG = Color.rgb(0x06, 0x14, 0x0A);

    private final Handler ui = new Handler(Looper.getMainLooper());
    private TextView message;
    private ProgressBar progress;
    private Button pickButton;
    private boolean busy;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        buildUi();
        busy = true;
        setStatus("Preparing game files...", true);
        new Thread(() -> {
            String error = null;
            try {
                if (!GameFiles.assetsCurrent(this)) GameFiles.installAssets(this);
            } catch (IOException e) {
                error = "Could not unpack the game files: " + e.getMessage();
            }
            final String err = error;
            ui.post(() -> {
                busy = false;
                if (err != null) {
                    setStatus(err, false);
                    return;
                }
                File disc = GameFiles.findDisc(GameFiles.discDir(this));
                if (disc != null) {
                    startGame();
                } else {
                    askForDisc(null);
                }
            });
        }, "ch-setup").start();
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(BG);
        int pad = dp(32);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("CARNAGE HEART");
        title.setTextColor(GREEN);
        title.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        title.setTextSize(30);
        title.setGravity(Gravity.CENTER);
        root.addView(title, wrap());

        message = new TextView(this);
        message.setTextColor(Color.WHITE);
        message.setTypeface(Typeface.MONOSPACE);
        message.setTextSize(16);
        message.setGravity(Gravity.CENTER);
        message.setPadding(0, dp(24), 0, dp(24));
        root.addView(message, wrap());

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(420), ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.CENTER;
        root.addView(progress, lp);

        pickButton = new Button(this);
        pickButton.setText("CHOOSE DISC FILES");
        pickButton.setTextSize(18);
        pickButton.setOnClickListener(v -> openPicker());
        LinearLayout.LayoutParams bp = wrap();
        bp.topMargin = dp(24);
        root.addView(pickButton, bp);
        pickButton.setVisibility(View.GONE);

        setContentView(root);
    }

    private void setStatus(String text, boolean showProgress) {
        message.setText(text);
        progress.setVisibility(showProgress ? View.VISIBLE : View.INVISIBLE);
        progress.setIndeterminate(true);
    }

    private void askForDisc(String problem) {
        String text = "Choose your Carnage Heart (USA) disc.\n\n"
            + "Select the .cue file and all three .bin files together\n"
            + "(tap and hold the first one, then tap the others),\n"
            + "or a single .chd file.\n\n"
            + "They are copied into the app once (about 130 MB).";
        if (problem != null) text = problem + "\n\n" + text;
        setStatus(text, false);
        pickButton.setVisibility(View.VISIBLE);
    }

    private void openPicker() {
        if (busy) return;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(i, PICK_DISC);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_DISC || resultCode != RESULT_OK || data == null) return;
        final List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int k = 0; k < clip.getItemCount(); k++) uris.add(clip.getItemAt(k).getUri());
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        if (uris.isEmpty()) return;
        importDisc(uris);
    }

    private void importDisc(List<Uri> uris) {
        busy = true;
        pickButton.setVisibility(View.GONE);
        setStatus("Copying disc files...", true);
        progress.setIndeterminate(false);
        progress.setProgress(0);
        new Thread(() -> {
            String result;
            try {
                result = copyAndCheck(uris);
            } catch (IOException e) {
                result = "Copy failed: " + e.getMessage();
            }
            final String problem = result;
            ui.post(() -> {
                busy = false;
                if (problem == null) startGame();
                else askForDisc(problem);
            });
        }, "ch-import").start();
    }

    /** Copies the picked files into disc/. Returns null when a bootable disc is
     *  there afterwards, else a message saying what is missing. */
    private String copyAndCheck(List<Uri> uris) throws IOException {
        ContentResolver cr = getContentResolver();
        File discDir = GameFiles.discDir(this);
        File tmp = new File(getFilesDir(), "disc-import");
        GameFiles.deleteTree(tmp);
        if (!tmp.mkdirs() && !tmp.isDirectory()) throw new IOException("cannot create " + tmp);

        List<String> names = new ArrayList<>();
        long total = 0;
        for (Uri u : uris) {
            String[] meta = queryNameAndSize(cr, u);
            names.add(meta[0]);
            try { total += Long.parseLong(meta[1]); } catch (NumberFormatException ignored) { }
        }
        for (String n : names) {
            String low = n.toLowerCase(Locale.ROOT);
            if (low.endsWith(".7z") || low.endsWith(".zip") || low.endsWith(".rar")) {
                GameFiles.deleteTree(tmp);
                return "\"" + n + "\" is an archive. Unpack it first (Files app:\n"
                    + "tap it, then Extract), then choose the files inside.";
            }
        }

        long done = 0;
        byte[] buf = new byte[1 << 20];
        for (int k = 0; k < uris.size(); k++) {
            File out = new File(tmp, names.get(k));
            try (InputStream in = cr.openInputStream(uris.get(k));
                 OutputStream os = new FileOutputStream(out)) {
                if (in == null) throw new IOException("cannot open " + names.get(k));
                int n;
                while ((n = in.read(buf)) > 0) {
                    os.write(buf, 0, n);
                    done += n;
                    if (total > 0) {
                        final int p = (int) Math.min(1000, done * 1000 / total);
                        ui.post(() -> progress.setProgress(p));
                    }
                }
            }
        }

        // Move the new files over any earlier ones (a second pick can add the
        // tracks the first one missed).
        discDir.mkdirs();
        File[] got = tmp.listFiles();
        if (got != null) {
            for (File f : got) {
                File dst = new File(discDir, f.getName());
                //noinspection ResultOfMethodCallIgnored
                dst.delete();
                if (!f.renameTo(dst)) throw new IOException("cannot move " + f.getName());
            }
        }
        GameFiles.deleteTree(tmp);
        fixTrackNameCase(discDir);

        if (GameFiles.findDisc(discDir) != null) return null;
        File[] all = discDir.listFiles();
        if (all != null) {
            for (File f : all) {
                if (f.getName().toLowerCase(Locale.ROOT).endsWith(".cue")) {
                    List<String> missing = GameFiles.missingTracks(f);
                    StringBuilder sb = new StringBuilder("Still missing:\n");
                    for (String m : missing) sb.append(m).append('\n');
                    sb.append("Choose those files too.");
                    return sb.toString();
                }
            }
        }
        return "No .cue (or .chd) file was chosen.";
    }

    /** A .cue may name its tracks with different upper/lower case than the
     *  files have; Android's storage is case-sensitive, so rename to match. */
    private static void fixTrackNameCase(File dir) {
        File[] all = dir.listFiles();
        if (all == null) return;
        for (File cue : all) {
            if (!cue.getName().toLowerCase(Locale.ROOT).endsWith(".cue")) continue;
            for (String want : GameFiles.missingTracks(cue)) {
                for (File f : all) {
                    if (f.getName().equalsIgnoreCase(want)) {
                        //noinspection ResultOfMethodCallIgnored
                        f.renameTo(new File(dir, want));
                    }
                }
            }
        }
    }

    private static String[] queryNameAndSize(ContentResolver cr, Uri u) {
        String name = null;
        String size = "";
        try (Cursor c = cr.query(u, new String[] {
                OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                name = c.getString(0);
                if (!c.isNull(1)) size = Long.toString(c.getLong(1));
            }
        } catch (Exception ignored) { }
        if (name == null || name.isEmpty()) {
            String p = u.getLastPathSegment();
            name = p != null ? p.substring(p.lastIndexOf('/') + 1) : "disc.bin";
        }
        name = name.replace('/', '_').replace('\\', '_');
        return new String[] { name, size };
    }

    private void startGame() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private static LinearLayout.LayoutParams wrap() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.CENTER;
        return lp;
    }
}
