package io.psxrecomp.androidkit;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The game folder on the device is the app's private files directory
 * (Context.getFilesDir()). It holds what the runtime expects beside its
 * executable on a PC: game.toml, game_options.toml, bios/, mods/ (unpacked
 * from the APK's assets/game/), plus disc/ (the player's disc, imported once),
 * saves/ (memory cards) and settings.toml (written by the game).
 */
public final class KitFiles {
    private KitFiles() {}

    public static final String ASSET_ROOT = "game";
    public static final String ASSET_LIST = "game/files.txt";
    private static final String STAMP = ".installed_build";

    public static File gameDir(Context c) { return c.getFilesDir(); }
    public static File discDir(Context c) { return new File(c.getFilesDir(), "disc"); }

    /** Build stamp of this APK: version code + install/update time. */
    private static String buildStamp(Context c) {
        try {
            PackageInfo pi = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return pi.getLongVersionCode() + ":" + pi.lastUpdateTime;
        } catch (Exception e) {
            return "unknown";
        }
    }

    /** True when the APK's game files are already unpacked for this build. */
    public static boolean assetsCurrent(Context c) {
        File stamp = new File(gameDir(c), STAMP);
        return stamp.isFile() && buildStamp(c).equals(readSmall(stamp));
    }

    /** Unpacks assets/game/** into the game folder (overwrites shipped files,
     *  never touches disc/, saves/ or settings the game wrote). */
    public static void installAssets(Context c) throws IOException {
        AssetManager am = c.getAssets();
        List<String> files = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                am.open(ASSET_LIST), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (!line.isEmpty()) files.add(line);
            }
        }
        File root = gameDir(c);
        byte[] buf = new byte[1 << 16];
        for (String rel : files) {
            File out = new File(root, rel);
            File parent = out.getParentFile();
            if (parent != null) parent.mkdirs();
            try (InputStream in = am.open(ASSET_ROOT + "/" + rel);
                 OutputStream os = new FileOutputStream(out)) {
                int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            }
        }
        writeSmall(new File(root, STAMP), buildStamp(c));
    }

    /** The disc image to boot: a .cue whose tracks are all present, else a .chd. */
    public static File findDisc(File dir) {
        File[] list = dir.listFiles();
        if (list == null) return null;
        File chd = null;
        for (File f : list) {
            String n = f.getName().toLowerCase(Locale.ROOT);
            if (n.endsWith(".cue") && missingTracks(f).isEmpty()) return f;
            if (n.endsWith(".chd")) chd = f;
        }
        return chd;
    }

    private static final Pattern FILE_LINE =
        Pattern.compile("^\\s*FILE\\s+\"([^\"]+)\"", Pattern.CASE_INSENSITIVE);

    /** File names a .cue sheet refers to. */
    public static List<String> cueTracks(File cue) {
        List<String> out = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                new FileInputStream(cue), StandardCharsets.ISO_8859_1))) {
            String line;
            while ((line = r.readLine()) != null) {
                Matcher m = FILE_LINE.matcher(line);
                if (m.find()) {
                    String name = m.group(1);
                    int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
                    out.add(slash >= 0 ? name.substring(slash + 1) : name);
                }
            }
        } catch (IOException e) {
            // unreadable cue: treat as having no tracks
        }
        return out;
    }

    /** Track files a .cue needs that are not in its folder. */
    public static List<String> missingTracks(File cue) {
        List<String> missing = new ArrayList<>();
        List<String> tracks = cueTracks(cue);
        if (tracks.isEmpty()) {
            missing.add("(no tracks listed in " + cue.getName() + ")");
            return missing;
        }
        for (String t : tracks) {
            if (!new File(cue.getParentFile(), t).isFile()) missing.add(t);
        }
        return missing;
    }

    public static String readSmall(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[256];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, StandardCharsets.UTF_8).trim() : "";
        } catch (IOException e) {
            return "";
        }
    }

    public static void writeSmall(File f, String s) throws IOException {
        try (OutputStream os = new FileOutputStream(f)) {
            os.write(s.getBytes(StandardCharsets.UTF_8));
        }
    }

    public static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}
