package com.hatdagger.carnageheart;

import android.content.Context;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The online campaign's turn relay (relay/worker.js), for the Android app.
 * The same rules as the desktop launcher (launcher/ch_online.cpp):
 *
 * - Games are remembered in files/online-turns/games.txt, one line each:
 *   code, A/B, key, relay address. Turn files live in
 *   files/online-turns/&lt;code&gt;/turn-000001.cht ..., where the game
 *   (src/turn_files.c) reads and writes them.
 * - check() brings one game's folder up to date with the relay (sends my
 *   turn files it lacks, fetches the ones I lack) and works out whose turn
 *   it is. Used by the Online screen and the background "your turn" check.
 * - nativeGet / nativePut are the two requests the game's relay thread makes
 *   (src/android/ch_android_relay.c); they answer like curl -f.
 */
public final class Relay {
    /** The project's relay; the desktop build has the same address
     *  (CH_TURN_RELAY_URL in cmake/ch_launcher_menu.cmake). A first line in
     *  files/online-turns/relay.txt overrides it. */
    static final String DEFAULT_URL = "https://ch-turns.surreal64.workers.dev";
    static final String DIR = "online-turns";
    /** Cloudflare refuses some default user agents (error 1010). */
    static final String USER_AGENT = "CarnageHeart-Android";
    /** Letters a game code can contain (never 0, O, 1 or I). */
    static final String CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

    enum Status { UNKNOWN, CHECKING, YOUR_TURN, THEIR_TURN, SENDING, OVER, OFFLINE, GONE }

    static final class Game {
        String code, key, relay;
        char role = 'A';
        Status status = Status.UNKNOWN;
        String detail = "";
        boolean joined;
        int latest, local;
        int scenario = -1, map = 0;

        Game copy() {
            Game g = new Game();
            g.code = code; g.key = key; g.relay = relay; g.role = role;
            g.status = status; g.detail = detail; g.joined = joined;
            g.latest = latest; g.local = local; g.scenario = scenario; g.map = map;
            return g;
        }
    }

    private Relay() { }

    // ---- HTTP -------------------------------------------------------------

    /** Result of one request: HTTP status (-1: no connection) and body. */
    static final class Reply {
        int status = -1;
        byte[] body = new byte[0];
        String text() { return new String(body, StandardCharsets.UTF_8); }
    }

    static Reply http(String method, String url, String key, File upload,
                      File download, int connectS, int maxS) {
        Reply r = new Reply();
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod(method);
            c.setConnectTimeout(connectS * 1000);
            c.setReadTimeout(maxS * 1000);
            c.setUseCaches(false);
            c.setRequestProperty("User-Agent", USER_AGENT);
            if (key != null && !key.isEmpty()) c.setRequestProperty("X-CH-Key", key);
            if (upload != null) {
                c.setDoOutput(true);
                c.setRequestProperty("Content-Type", "application/octet-stream");
                c.setFixedLengthStreamingMode(upload.length());
                try (InputStream in = new FileInputStream(upload);
                     OutputStream out = c.getOutputStream()) {
                    copy(in, out);
                }
            } else if ("POST".equals(method)) {
                c.setDoOutput(true);
                c.setFixedLengthStreamingMode(0);
                c.getOutputStream().close();
            }
            r.status = c.getResponseCode();
            InputStream in = r.status < 400 ? c.getInputStream() : c.getErrorStream();
            if (in != null) {
                try (InputStream s = in) {
                    if (download != null && r.status < 400) {
                        try (OutputStream out = new FileOutputStream(download)) { copy(s, out); }
                    } else {
                        ByteArrayOutputStream b = new ByteArrayOutputStream();
                        copy(s, b);
                        r.body = b.toByteArray();
                    }
                }
            }
        } catch (IOException | RuntimeException e) {
            r.status = -1;
        } finally {
            if (c != null) c.disconnect();
        }
        return r;
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[32 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
    }

    private static int curlCode(int status) {
        if (status < 0) return 7;               // could not connect
        return status >= 400 ? 22 : 0;          // curl -f
    }

    /** Called by the game (ch_android_relay.c): GET url into outPath. */
    public static int nativeGet(String url, String outPath, int connectS, int maxS) {
        File out = new File(outPath);
        Reply r = http("GET", url, null, null, out, connectS, maxS);
        int rc = curlCode(r.status);
        if (rc != 0) //noinspection ResultOfMethodCallIgnored
            out.delete();
        return rc;
    }

    /** Called by the game (ch_android_relay.c): PUT inPath to url. */
    public static int nativePut(String url, String key, String inPath, int maxS) {
        File in = new File(inPath);
        if (!in.isFile()) return 26;            // curl: could not read the file
        return curlCode(http("PUT", url, key, in, null, Math.min(maxS, 15), maxS).status);
    }

    // ---- games list -------------------------------------------------------

    static File dir(Context ctx) { return new File(ctx.getFilesDir(), DIR); }
    static File folder(Context ctx, String code) { return new File(dir(ctx), code); }
    static File turnFile(Context ctx, String code, int seq) {
        return new File(folder(ctx, code), String.format(Locale.ROOT, "turn-%06d.cht", seq));
    }

    static String relayUrl(Context ctx) {
        File f = new File(dir(ctx), "relay.txt");
        if (f.isFile()) {
            try (BufferedReader r = new BufferedReader(new FileReader(f))) {
                String line = r.readLine();
                if (line != null && !base(line).isEmpty()) return base(line);
            } catch (IOException ignored) { }
        }
        return DEFAULT_URL;
    }

    static String base(String u) {
        u = u.trim();
        while (u.endsWith("/")) u = u.substring(0, u.length() - 1);
        return u;
    }

    static synchronized List<Game> load(Context ctx) {
        List<Game> out = new ArrayList<>();
        File f = new File(dir(ctx), "games.txt");
        if (!f.isFile()) return out;
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] p = line.trim().split("\\s+");
                if (p.length < 4) continue;
                Game g = new Game();
                g.code = p[0];
                g.role = "B".equals(p[1]) ? 'B' : 'A';
                g.key = p[2];
                g.relay = p[3];
                out.add(g);
            }
        } catch (IOException ignored) { }
        return out;
    }

    static synchronized boolean save(Context ctx, List<Game> games) {
        File d = dir(ctx);
        //noinspection ResultOfMethodCallIgnored
        d.mkdirs();
        File f = new File(d, "games.txt"), tmp = new File(d, "games.txt.new");
        try (OutputStream o = new FileOutputStream(tmp)) {
            StringBuilder sb = new StringBuilder();
            for (Game g : games)
                sb.append(g.code).append(' ').append(g.role).append(' ')
                  .append(g.key).append(' ').append(g.relay).append('\n');
            o.write(sb.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            return false;
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
        return tmp.renameTo(f);
    }

    static Game find(Context ctx, String code) {
        for (Game g : load(ctx)) if (g.code.equals(code)) return g;
        return null;
    }

    static synchronized void add(Context ctx, Game g) {
        List<Game> games = load(ctx);
        games.add(0, g);
        save(ctx, games);
    }

    static synchronized void forget(Context ctx, String code) {
        List<Game> games = load(ctx);
        for (int i = 0; i < games.size(); i++)
            if (games.get(i).code.equals(code)) { games.remove(i); break; }
        save(ctx, games);
    }

    /** "K7QXM2PD", "k7qx-m2pd" ... -> "K7QX-M2PD", or null. */
    static String normaliseCode(String in) {
        StringBuilder c = new StringBuilder();
        for (char ch : in.toUpperCase(Locale.ROOT).toCharArray())
            if (CODE_CHARS.indexOf(ch) >= 0) c.append(ch);
        if (c.length() != 8) return null;
        return c.substring(0, 4) + "-" + c.substring(4);
    }

    static String jsonStr(String j, String name) {
        String k = "\"" + name + "\":\"";
        int p = j.indexOf(k);
        if (p < 0) return "";
        int e = j.indexOf('"', p + k.length());
        return e < 0 ? "" : j.substring(p + k.length(), e);
    }

    static long jsonNum(String j, String name, long dflt) {
        String k = "\"" + name + "\":";
        int p = j.indexOf(k);
        if (p < 0) return dflt;
        int s = p + k.length(), e = s;
        while (e < j.length() && Character.isDigit(j.charAt(e))) e++;
        try { return Long.parseLong(j.substring(s, e)); } catch (NumberFormatException x) { return dflt; }
    }

    /** Creates a game on the relay; player A. Returns the game or an error text. */
    static Object create(Context ctx) {
        String relay = relayUrl(ctx);
        Reply r = http("POST", relay + "/v1/games", null, null, null, 10, 20);
        if (r.status == -1) return "Could not reach the relay. Check the connection.";
        Game g = new Game();
        g.code = jsonStr(r.text(), "game");
        g.key = jsonStr(r.text(), "key");
        g.role = 'A';
        g.relay = relay;
        if (r.status != 201 || g.code.isEmpty() || g.key.isEmpty())
            return "The relay did not make a game (error " + r.status + ").";
        add(ctx, g);
        return g;
    }

    /** Joins a game as player B. Returns the game or an error text. */
    static Object join(Context ctx, String codeIn) {
        String code = normaliseCode(codeIn);
        if (code == null) return "A game code has 8 letters and numbers.";
        if (find(ctx, code) != null) return "You are already in game " + code + ".";
        String relay = relayUrl(ctx);
        Reply r = http("POST", relay + "/v1/games/" + code + "/join", null, null, null, 10, 20);
        if (r.status == -1) return "Could not reach the relay. Check the connection.";
        if (r.status == 404) return "There is no game " + code + ". Check the code.";
        if (r.status == 409) return "Game " + code + " already has two players.";
        Game g = new Game();
        g.code = jsonStr(r.text(), "game");
        g.key = jsonStr(r.text(), "key");
        g.role = 'B';
        g.relay = relay;
        if (r.status != 201 || g.code.isEmpty() || g.key.isEmpty())
            return "The relay did not let you join (error " + r.status + ").";
        add(ctx, g);
        return g;
    }

    // ---- checking a game --------------------------------------------------

    static boolean isMine(char role, int seq) { return ((seq & 1) == 1) == (role == 'A'); }

    static int localLatest(Context ctx, String code) {
        int s = 0;
        while (turnFile(ctx, code, s + 1).isFile()) s++;
        return s;
    }

    /** First 52 bytes of a turn file, or null. */
    private static byte[] header(File f) {
        byte[] h = new byte[52];
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            int n = r.read(h);
            if (n < 8 || h[0] != 'C' || h[1] != 'H' || h[2] != 'T' || h[3] != 'F') return null;
            if (n < 52) h[4] = h[5] = 0;        // too short for a scenario
            return h;
        } catch (IOException e) {
            return null;
        }
    }

    /** Brings the folder up to date with the relay and fills in g.status /
     *  g.detail. Network: call off the main thread. */
    static void check(Context ctx, Game g) {
        String base = base(g.relay) + "/v1/games/" + g.code;
        Reply r = http("GET", base, null, null, null, 10, 20);
        if (r.status == 404) { g.status = Status.GONE; g.detail = "The relay no longer has this game."; return; }
        if (r.status != 200) { g.status = Status.OFFLINE; g.detail = "Could not reach the relay."; return; }
        String body = r.text();
        g.joined = body.contains("\"joined\":true");
        g.latest = (int) jsonNum(body, "latest", 0);

        //noinspection ResultOfMethodCallIgnored
        folder(ctx, g.code).mkdirs();
        g.local = localLatest(ctx, g.code);
        // My turn files the relay does not have yet (the game was closed
        // before it could send them).
        for (int s = g.latest + 1; s <= g.local && isMine(g.role, s); s++) {
            Reply p = http("PUT", base + "/turns/" + s, g.key, turnFile(ctx, g.code, s), null, 10, 30);
            if (p.status != 200 && p.status != 201) break;
            g.latest = s;
        }
        // Turn files I do not have (the other player's newest, or all of them
        // on a new device).
        for (int s = g.local + 1; s <= g.latest; s++) {
            File dst = turnFile(ctx, g.code, s), part = new File(dst.getPath() + ".dl");
            Reply d = http("GET", base + "/turns/" + s, null, null, part, 10, 30);
            if (d.status != 200 || header(part) == null) {
                //noinspection ResultOfMethodCallIgnored
                part.delete();
                break;
            }
            //noinspection ResultOfMethodCallIgnored
            dst.delete();
            if (!part.renameTo(dst)) break;
        }
        g.local = localLatest(ctx, g.code);
        int flags = -1;
        if (g.local > 0) {
            byte[] h = header(turnFile(ctx, g.code, g.local));
            if (h != null) {
                flags = h[7] & 0xFF;
                if (((h[4] & 0xFF) | ((h[5] & 0xFF) << 8)) >= 2) {
                    g.scenario = h[48] & 0xFF;
                    g.map = h[49] & 0xFF;
                }
            }
        }

        int next = g.latest + 1;
        int round = (g.latest + 2) / 2;
        char other = g.role == 'A' ? 'B' : 'A';
        if (flags > 0 && (flags & 1) != 0 && g.local >= g.latest) {
            g.status = Status.OVER;
            g.detail = "The game is over.";
        } else if (g.local > g.latest && isMine(g.role, g.latest + 1)) {
            g.status = Status.SENDING;
            g.detail = "Your turn is not on the relay yet. It is sent when the relay can be reached.";
        } else if (isMine(g.role, next)) {
            g.status = Status.YOUR_TURN;
            g.detail = "Turn " + round + ". It's your turn"
                + (g.joined ? "." : " (player B has not joined yet).");
        } else {
            g.status = Status.THEIR_TURN;
            g.detail = !g.joined ? "Waiting for player B to join with the code."
                : "Turn " + round + ". Waiting for player " + other + ".";
        }
    }

    static String scenarioLabel(Game g) {
        String[] names = { "Europa (Practice)", "Europa", "Ganymede", "Callisto" };
        if (g.scenario < 0) return "Europa (Practice)";     // new online games: Practice only
        if (g.scenario > 3) return "an unknown scenario";
        String n = names[g.scenario];
        if (g.scenario != 0 && g.map > 0) n += " map " + (g.map + 1);
        return n;
    }

    static String statusLabel(Status s) {
        switch (s) {
            case YOUR_TURN: return "YOUR TURN";
            case THEIR_TURN: return "WAITING";
            case SENDING: return "SENDING";
            case OVER: return "OVER";
            case OFFLINE: return "OFFLINE";
            case GONE: return "GONE";
            case CHECKING: return "CHECKING";
            default: return "";
        }
    }
}
