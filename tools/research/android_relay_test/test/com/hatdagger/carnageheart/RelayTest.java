package com.hatdagger.carnageheart;
import android.content.Context;
import java.io.*;
import java.nio.file.*;
public class RelayTest {
    static int fails = 0;
    static void ok(boolean c, String what) { System.out.println((c ? "PASS " : "FAIL ") + what); if (!c) fails++; }
    static void turn(File f, int seq, int scen) throws IOException {
        byte[] b = new byte[200];
        b[0]='C';b[1]='H';b[2]='T';b[3]='F'; b[4]=2; b[8]=(byte)seq; b[48]=(byte)scen; b[60]=(byte)seq;
        f.getParentFile().mkdirs(); Files.write(f.toPath(), b);
    }
    public static void main(String[] a) throws Exception { run(a[0], a[1]); if (fails > 0) System.exit(1); }
    public static void run(String relay, String root) throws Exception {
        Context a = new Context(new File(root, "A")), b = new Context(new File(root, "B")), b2 = new Context(new File(root, "B2"));
        for (Context c : new Context[]{a,b,b2}) { new File(c.getFilesDir(), "online-turns").mkdirs();
            Files.write(new File(c.getFilesDir(), "online-turns/relay.txt").toPath(), (relay + "/\n").getBytes()); }
        Object ga = Relay.create(a);
        ok(ga instanceof Relay.Game, "create: " + ga);
        Relay.Game A = (Relay.Game) ga;
        ok(A.code.matches("[A-Z2-9]{4}-[A-Z2-9]{4}") && A.role=='A', "code looks right " + A.code);
        Relay.Game c = Relay.find(a, A.code); Relay.check(a, c);
        ok(c.status == Relay.Status.YOUR_TURN && !c.joined, "A before join: " + c.status + " / " + c.detail);
        ok(Relay.join(b, "zzzz-zzzz") instanceof String, "join bad code -> " + Relay.join(b, "zzzz-zzzz"));
        Object gb = Relay.join(b, A.code.toLowerCase().replace("-", " "));
        ok(gb instanceof Relay.Game && ((Relay.Game) gb).role=='B', "join lower-case: " + gb);
        ok(Relay.join(b2, A.code) instanceof String, "third player refused: " + Relay.join(b2, A.code));
        Relay.Game B = Relay.find(b, A.code);
        // A's game writes turn 1; the game's relay thread uses nativePut.
        File t1 = Relay.turnFile(a, A.code, 1); turn(t1, 1, 0);
        String base = relay + "/v1/games/" + A.code + "/turns/";
        ok(Relay.nativePut(base + "1", B.key, t1.getPath(), 30) == 22, "B's key can't store turn 1");
        ok(Relay.nativePut(base + "1", A.key, t1.getPath(), 30) == 0, "nativePut turn 1");
        ok(Relay.nativePut(base + "1", A.key, t1.getPath(), 30) == 0, "retried upload is fine");
        File out = new File(root, "dl.cht");
        ok(Relay.nativeGet(base + "2", out.getPath(), 5, 15) == 22 && !out.exists(), "nativeGet missing -> 22, no file");
        ok(Relay.nativeGet(base + "1", out.getPath(), 5, 15) == 0 && Files.readAllBytes(out.toPath())[60]==1, "nativeGet turn 1");
        ok(Relay.nativeGet("http://127.0.0.1:9/x", out.getPath(), 2, 3) == 7, "no connection -> 7");
        Relay.check(b, B);
        ok(B.status == Relay.Status.YOUR_TURN && B.local == 1 && B.joined, "B fetched turn 1, its turn: " + B.detail);
        // B plays turn 2 but the game was closed before uploading: check() sends it.
        turn(Relay.turnFile(b, A.code, 2), 2, 0);
        Relay.check(b, B);
        ok(B.latest == 2 && B.status == Relay.Status.THEIR_TURN, "check sent B's unsent turn 2: " + B.detail);
        Relay.check(a, c);
        ok(c.status == Relay.Status.YOUR_TURN && c.local == 2 && c.scenario == 0, "A sees its turn, scenario " + Relay.scenarioLabel(c));
        // B on a new device: empty folder catches up.
        Relay.Game nb = B.copy(); Relay.add(b2, nb);
        Relay.check(b2, nb);
        ok(nb.local == 2 && nb.status == Relay.Status.THEIR_TURN, "new device fetched both files");
        // game over flag
        File t3 = Relay.turnFile(a, A.code, 3); turn(t3, 3, 0); byte[] x = Files.readAllBytes(t3.toPath()); x[7]=1; Files.write(t3.toPath(), x);
        Relay.check(a, c); Relay.check(b, B);
        ok(c.status == Relay.Status.OVER && B.status == Relay.Status.OVER, "game over on both: " + c.status + " " + B.status);
        ok(Relay.load(b).size()==1, "games.txt reload");
        Relay.forget(b, A.code);
        ok(Relay.load(b).isEmpty(), "forget");
        System.out.println(fails == 0 ? "ALL PASS" : fails + " FAILED");
    }
}
