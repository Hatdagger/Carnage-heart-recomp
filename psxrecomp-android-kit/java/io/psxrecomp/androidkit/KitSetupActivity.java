package io.psxrecomp.androidkit;

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
import java.io.RandomAccessFile;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * First screen. Unpacks the game files from the APK (after every install or
 * update), and the first time asks the player to pick their disc
 * (the .cue and its .bin tracks, or a .chd). The files are copied into the
 * app's own storage so the game can read them directly. Then the game opens.
 *
 * Per-game text, colours and extra buttons come from KitConfig (written by
 * scripts/build_apk.py from android_game.json).
 */
public class KitSetupActivity extends Activity {
    private static final int PICK_DISC = 1;
    private static final int GREEN = Color.parseColor(KitConfig.ACCENT);
    private static final int BG = Color.parseColor(KitConfig.BACKGROUND);

    private final Handler ui = new Handler(Looper.getMainLooper());
    private TextView message;
    private ProgressBar progress;
    private Button pickButton;
    private Button playButton;
    private Button playSoftButton;
    private final java.util.List<Button> extraButtons = new java.util.ArrayList<>();
    private Button scaleButton;
    private Button shareButton;
    private Button saveLogButton;
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
                if (!KitFiles.assetsCurrent(this)) KitFiles.installAssets(this);
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
                File disc = KitFiles.findDisc(KitFiles.discDir(this));
                if (disc != null) {
                    showReady();
                } else {
                    askForDisc(null);
                }
            });
        }, "psxak-setup").start();
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(BG);
        int pad = dp(32);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(KitConfig.TITLE);
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

        playButton = new Button(this);
        playButton.setText("PLAY");
        playButton.setTextSize(18);
        playButton.setOnClickListener(v -> startGame(KitGameActivity.RENDERER_GPU));
        LinearLayout.LayoutParams pp = wrap();
        pp.topMargin = dp(24);
        root.addView(playButton, pp);
        playButton.setVisibility(View.GONE);

        // Sharpness of the GPU renderer: 4x (sharpest) -> 3x -> 2x (lightest).
        // Remembered between runs; lower it if battles stutter or crackle.
        scaleButton = new Button(this);
        scaleButton.setTextSize(14);
        updateScaleButton();
        scaleButton.setOnClickListener(v -> {
            String s = scale();
            String next = "4x".equals(s) ? "3x" : "3x".equals(s) ? "2x" : "4x";
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(PREF_SCALE, next).apply();
            updateScaleButton();
        });
        LinearLayout.LayoutParams sc = wrap();
        sc.topMargin = dp(8);
        root.addView(scaleButton, sc);
        scaleButton.setVisibility(View.GONE);

        // The CPU renderer of test APK 5 (native resolution), in case the
        // GPU renderer misbehaves on a device.
        playSoftButton = new Button(this);
        playSoftButton.setText("PLAY (SOFTWARE RENDERER)");
        playSoftButton.setTextSize(14);
        playSoftButton.setOnClickListener(v -> startGame(KitGameActivity.RENDERER_SOFTWARE));
        LinearLayout.LayoutParams ps = wrap();
        ps.topMargin = dp(12);
        root.addView(playSoftButton, ps);
        playSoftButton.setVisibility(View.GONE);

        // The game's own extra screens (android_game.json "extra_buttons").
        for (String[] eb : KitConfig.EXTRA_BUTTONS) {
            final String label = eb[0];
            final String cls = eb[1];
            Button b = new Button(this);
            b.setTextSize(18);
            b.setText(label);
            b.setOnClickListener(v -> {
                try {
                    startActivity(new Intent(this, Class.forName(cls)));
                    finish();
                } catch (ClassNotFoundException e) {
                    setStatus("Missing screen: " + cls, false);
                }
            });
            LinearLayout.LayoutParams op = wrap();
            op.topMargin = dp(12);
            root.addView(b, op);
            b.setVisibility(View.GONE);
            extraButtons.add(b);
        }

        shareButton = new Button(this);
        shareButton.setText("SHARE LAST LOG");
        shareButton.setTextSize(16);
        shareButton.setOnClickListener(v -> shareLog());
        LinearLayout.LayoutParams sp = wrap();
        sp.topMargin = dp(12);
        root.addView(shareButton, sp);
        shareButton.setVisibility(View.GONE);

        saveLogButton = new Button(this);
        saveLogButton.setText("SAVE LOG TO DOWNLOADS");
        saveLogButton.setTextSize(16);
        saveLogButton.setOnClickListener(v -> saveLog());
        LinearLayout.LayoutParams sl = wrap();
        sl.topMargin = dp(12);
        root.addView(saveLogButton, sl);
        saveLogButton.setVisibility(View.GONE);

        // Scrolls if the buttons don't fit (small or split screens).
        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        scroll.addView(root, new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
    }

    private void setStatus(String text, boolean showProgress) {
        message.setText(text);
        progress.setVisibility(showProgress ? View.VISIBLE : View.INVISIBLE);
        progress.setIndeterminate(true);
    }

    private void askForDisc(String problem) {
        String text = KitConfig.DISC_PROMPT;
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
                if (problem == null) startGame(KitGameActivity.RENDERER_GPU);
                else askForDisc(problem);
            });
        }, "psxak-import").start();
    }

    /** Copies the picked files into disc/. Returns null when a bootable disc is
     *  there afterwards, else a message saying what is missing. */
    private String copyAndCheck(List<Uri> uris) throws IOException {
        ContentResolver cr = getContentResolver();
        File discDir = KitFiles.discDir(this);
        File tmp = new File(getFilesDir(), "disc-import");
        KitFiles.deleteTree(tmp);
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
                KitFiles.deleteTree(tmp);
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
        KitFiles.deleteTree(tmp);
        fixTrackNameCase(discDir);

        if (KitFiles.findDisc(discDir) != null) return null;
        File[] all = discDir.listFiles();
        if (all != null) {
            for (File f : all) {
                if (f.getName().toLowerCase(Locale.ROOT).endsWith(".cue")) {
                    List<String> missing = KitFiles.missingTracks(f);
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
            for (String want : KitFiles.missingTracks(cue)) {
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

    private File logFile() {
        return new File(getFilesDir(), KitConfig.LOG_NAME + ".log");
    }

    /** Disc is in place: offer PLAY, and SHARE LAST LOG / SAVE LOG TO DOWNLOADS
     *  when the previous run left a log (starting the game overwrites it). */
    private void showReady() {
        setStatus("Ready.", false);
        playButton.setVisibility(View.VISIBLE);
        playSoftButton.setVisibility(View.VISIBLE);
        scaleButton.setVisibility(View.VISIBLE);
        for (Button b : extraButtons) b.setVisibility(View.VISIBLE);
        KitHooks.Loader.get().onReady(this);
        File log = logFile();
        int logVis = log.isFile() && log.length() > 0 ? View.VISIBLE : View.GONE;
        shareButton.setVisibility(logVis);
        saveLogButton.setVisibility(logVis);
    }

    /** Start of the log (setup, renderer, GPU probe) + its end, as text small
     *  enough for an Android share. Android silently drops a share whose
     *  text is too big (test 6: a 200 KB tail did nothing). */
    private String logExcerpt(int headBytes, int tailBytes) throws IOException {
        File log = logFile();
        try (RandomAccessFile f = new RandomAccessFile(log, "r")) {
            long len = f.length();
            if (len <= headBytes + tailBytes) {
                byte[] b = new byte[(int) len];
                f.readFully(b);
                return new String(b, java.nio.charset.StandardCharsets.UTF_8);
            }
            byte[] head = new byte[headBytes];
            f.readFully(head);
            byte[] tail = new byte[tailBytes];
            f.seek(len - tailBytes);
            f.readFully(tail);
            return new String(head, java.nio.charset.StandardCharsets.UTF_8)
                + "\n[... " + (len - headBytes - tailBytes) / 1024 + " KB of the middle cut ...]\n"
                + new String(tail, java.nio.charset.StandardCharsets.UTF_8);
        }
    }

    /** Sends the start and end of the last game log as text (a chat app, mail). */
    private void shareLog() {
        String text;
        try {
            text = logExcerpt(12 * 1024, 48 * 1024);
        } catch (IOException e) {
            setStatus("Could not read the log: " + e.getMessage(), false);
            return;
        }
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_SUBJECT, KitConfig.APP_NAME + " log");
        send.putExtra(Intent.EXTRA_TEXT, text);
        try {
            startActivity(Intent.createChooser(send, "Share log"));
        } catch (RuntimeException e) {
            setStatus("Share failed (" + e.getClass().getSimpleName()
                + "). Use SAVE LOG TO DOWNLOADS instead.", false);
        }
    }

    /** Copies the whole last log to Downloads (no permission needed on
     *  Android 10+), where any app can attach it as a file. */
    private void saveLog() {
        File log = logFile();
        String name = KitConfig.LOG_NAME + "-log-" + new java.text.SimpleDateFormat(
            "yyyyMMdd-HHmmss", Locale.US).format(new java.util.Date()) + ".txt";
        android.content.ContentValues v = new android.content.ContentValues();
        v.put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, name);
        v.put(android.provider.MediaStore.MediaColumns.MIME_TYPE, "text/plain");
        v.put(android.provider.MediaStore.MediaColumns.RELATIVE_PATH, "Download/");
        ContentResolver cr = getContentResolver();
        Uri uri = null;
        try {
            uri = cr.insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
            if (uri == null) throw new IOException("no Downloads entry");
            try (InputStream in = new java.io.FileInputStream(log);
                 OutputStream out = cr.openOutputStream(uri)) {
                if (out == null) throw new IOException("cannot write");
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
            setStatus("Saved to Downloads: " + name
                + "\nAttach that file in the chat (paperclip / + button, then Files).", false);
        } catch (IOException | RuntimeException e) {
            if (uri != null) {
                try { cr.delete(uri, null, null); } catch (RuntimeException ignored) { }
            }
            setStatus("Could not save the log: " + e.getMessage(), false);
        }
    }

    private void startGame(String renderer) {
        Intent i = new Intent(this, KitGameActivity.class);
        i.putExtra(KitGameActivity.EXTRA_RENDERER, renderer);
        i.putExtra(KitGameActivity.EXTRA_SCALE, scale());
        startActivity(i);
        finish();
    }

    public static final String PREFS = "settings";
    public static final String PREF_SCALE = "gpu_scale";

    static boolean validScale(String s) {
        return "2x".equals(s) || "3x".equals(s) || "4x".equals(s);
    }

    /** The SHARPNESS choice (remembered), else the game's default. */
    public static String scale(android.content.Context c) {
        String s = c.getSharedPreferences(PREFS, MODE_PRIVATE).getString(PREF_SCALE, null);
        return validScale(s) ? s : KitConfig.DEFAULT_SCALE;
    }

    private String scale() {
        return scale(this);
    }

    private void updateScaleButton() {
        String s = scale();
        String what = s.equals(KitConfig.DEFAULT_SCALE) ? "default"
            : "4x".equals(s) ? "sharpest" : "3x".equals(s) ? "lighter" : "lightest";
        scaleButton.setText("SHARPNESS: " + s + " (" + what + ") - tap to change");
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
