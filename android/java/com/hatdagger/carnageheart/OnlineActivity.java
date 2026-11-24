package com.hatdagger.carnageheart;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputFilter;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/**
 * ONLINE CAMPAIGN: the Android version of the desktop launcher's Online
 * screen (launcher/ch_online_menu.inc). Your games with whose turn it is,
 * NEW GAME, JOIN A GAME by code, and a page per game with PLAY, CHECK AGAIN,
 * SHARE CODE and FORGET. PLAY starts the game with the relay switches set
 * (GameActivity puts them in the environment for src/turn_files.c).
 */
public class OnlineActivity extends Activity {
    static final String EXTRA_GAME = "game";
    private static final int GREEN = Color.rgb(0x7C, 0xFF, 0x9A);
    private static final int BG = Color.rgb(0x06, 0x14, 0x0A);
    private static final int ASK_NOTIFY = 7;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private LinearLayout root;
    private List<Relay.Game> games = new ArrayList<>();
    private String message = "";
    private boolean busy;
    /** The game page being shown (null: the list). */
    private String page;
    private boolean forgetArmed;
    private boolean joining;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setBackgroundColor(BG);
        int pad = dp(28);
        root.setPadding(pad, pad, pad, pad);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        scroll.addView(root, new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
        games = Relay.load(this);
        page = getIntent() != null ? getIntent().getStringExtra(EXTRA_GAME) : null;
        if (page != null && Relay.find(this, page) == null) page = null;
        show();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        String g = intent.getStringExtra(EXTRA_GAME);
        if (g != null && Relay.find(this, g) != null) { page = g; joining = false; }
    }

    @Override
    protected void onResume() {
        super.onResume();
        checkAll();
    }

    @Override
    public void onBackPressed() {
        if (joining) { joining = false; message = ""; show(); return; }
        if (page != null) { page = null; forgetArmed = false; message = ""; show(); return; }
        startActivity(new Intent(this, SetupActivity.class));
        finish();
    }

    // ---- network work (off the main thread) -------------------------------

    private void checkAll() {
        games = Relay.load(this);
        if (games.isEmpty()) { show(); return; }
        TurnCheckJob.schedule(this);
        if (busy) return;
        busy = true;
        for (Relay.Game g : games) g.status = Relay.Status.CHECKING;
        show();
        final List<Relay.Game> work = new ArrayList<>();
        for (Relay.Game g : games) work.add(g.copy());
        new Thread(() -> {
            for (Relay.Game g : work) Relay.check(this, g);
            ui.post(() -> {
                busy = false;
                games = work;
                for (Relay.Game g : work) TurnCheckJob.noteSeen(this, g);
                show();
            });
        }, "ch-online-check").start();
    }

    private void createGame() {
        if (busy) return;
        busy = true;
        message = "Making a new game...";
        show();
        new Thread(() -> {
            Object r = Relay.create(this);
            ui.post(() -> {
                busy = false;
                if (r instanceof Relay.Game) {
                    page = ((Relay.Game) r).code;
                    message = "New game. Give this code to the other player.";
                    askNotifications();
                } else {
                    message = (String) r;
                }
                checkAll();
            });
        }, "ch-online-new").start();
    }

    private void joinGame(String code) {
        if (busy) return;
        busy = true;
        message = "Joining...";
        show();
        new Thread(() -> {
            Object r = Relay.join(this, code);
            ui.post(() -> {
                busy = false;
                if (r instanceof Relay.Game) {
                    joining = false;
                    page = ((Relay.Game) r).code;
                    message = "You joined as player B.";
                    askNotifications();
                } else {
                    message = (String) r;
                }
                checkAll();
            });
        }, "ch-online-join").start();
    }

    /** Android 13+ asks before an app may post notifications. */
    private void askNotifications() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.POST_NOTIFICATIONS }, ASK_NOTIFY);
        }
    }

    // ---- pages --------------------------------------------------------------

    private void show() {
        root.removeAllViews();
        title(page == null || joining ? "ONLINE CAMPAIGN" : page);
        if (joining) showJoin();
        else if (page != null) showGame();
        else showList();
    }

    private void showList() {
        text("Play a campaign against a friend, one turn each.\n"
            + "You can close the game after your turn; the app tells you when it's your turn again.", 14, Color.WHITE);
        if (!message.isEmpty()) text(message, 15, GREEN);
        if (games.isEmpty()) {
            text("No games yet.", 15, Color.LTGRAY);
        } else {
            for (Relay.Game g : games) {
                String st = Relay.statusLabel(g.status);
                button(g.code + "  (player " + g.role + ")" + (st.isEmpty() ? "" : "  " + st), 17, v -> {
                    page = g.code;
                    forgetArmed = false;
                    message = "";
                    show();
                });
            }
        }
        button("NEW GAME", 17, v -> createGame()).setEnabled(!busy);
        button("JOIN A GAME", 17, v -> { joining = true; message = ""; show(); }).setEnabled(!busy);
        button("CHECK AGAIN", 14, v -> checkAll()).setEnabled(!busy && !games.isEmpty());
        button("BACK", 14, v -> onBackPressed());
    }

    private void showJoin() {
        text("Type the game code the other player gave you\n(8 letters and numbers, like K7QX-M2PD).", 15, Color.WHITE);
        final EditText code = new EditText(this);
        code.setTextColor(GREEN);
        code.setHintTextColor(Color.GRAY);
        code.setHint("XXXX-XXXX");
        code.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        code.setTextSize(30);
        code.setGravity(Gravity.CENTER);
        code.setSingleLine(true);
        code.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
            | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
        code.setFilters(new InputFilter[] { new InputFilter.AllCaps(), new InputFilter.LengthFilter(12) });
        code.setImeOptions(EditorInfo.IME_ACTION_GO);
        code.setOnEditorActionListener((v, id, ev) -> { joinGame(code.getText().toString()); return true; });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(340), ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.CENTER;
        lp.topMargin = dp(12);
        root.addView(code, lp);
        if (!message.isEmpty()) text(message, 15, GREEN);
        button("JOIN", 17, v -> joinGame(code.getText().toString())).setEnabled(!busy);
        button("CANCEL", 14, v -> onBackPressed());
        code.requestFocus();
    }

    private void showGame() {
        Relay.Game g = null;
        for (Relay.Game x : games) if (x.code.equals(page)) g = x;
        if (g == null) { page = null; showList(); return; }
        final Relay.Game game = g;
        TurnCheckJob.dismiss(this, game.code);
        text("You are player " + game.role + "."
            + (game.role == 'A' ? " Give the code above to player B." : ""), 15, Color.WHITE);
        text("Scenario: " + Relay.scenarioLabel(game)
            + "\nIn the game choose that scenario; your turn loads when the map opens.", 14, Color.LTGRAY);
        String st = game.status == Relay.Status.CHECKING ? "Checking..." : game.detail;
        if (st != null && !st.isEmpty()) text(st, 16, GREEN);
        if (!message.isEmpty()) text(message, 14, GREEN);
        button("PLAY", 18, v -> play(game, GameActivity.RENDERER_GPU));
        button("PLAY (SOFTWARE RENDERER)", 13, v -> play(game, GameActivity.RENDERER_SOFTWARE));
        button("CHECK AGAIN", 14, v -> { message = ""; checkAll(); }).setEnabled(!busy);
        button("SHARE CODE", 14, v -> shareCode(game));
        button(forgetArmed ? "PRESS AGAIN TO FORGET (your key goes with it)" : "FORGET THIS GAME", 13, v -> {
            if (!forgetArmed) { forgetArmed = true; show(); return; }
            Relay.forget(this, game.code);
            TurnCheckJob.dismiss(this, game.code);
            forgetArmed = false;
            page = null;
            message = "Forgot game " + game.code + ".";
            games = Relay.load(this);
            if (games.isEmpty()) TurnCheckJob.cancel(this);
            show();
        });
        button("BACK", 14, v -> onBackPressed());
    }

    private void shareCode(Relay.Game g) {
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_TEXT, "Join my Carnage Heart online campaign: game code " + g.code
            + " (ONLINE CAMPAIGN > JOIN A GAME). Scenario: " + Relay.scenarioLabel(g) + ".");
        try {
            startActivity(Intent.createChooser(send, "Share game code"));
        } catch (RuntimeException ignored) { }
    }

    private void play(Relay.Game g, String renderer) {
        Intent i = new Intent(this, GameActivity.class);
        i.putExtra(GameActivity.EXTRA_RENDERER, renderer);
        String s = getSharedPreferences(SetupActivity.PREFS, MODE_PRIVATE).getString(SetupActivity.PREF_SCALE, "3x");
        i.putExtra(GameActivity.EXTRA_SCALE, s);
        i.putExtra(GameActivity.EXTRA_ONLINE_GAME, g.code);
        startActivity(i);
        finish();
    }

    // ---- small view helpers -------------------------------------------------

    private void title(String s) {
        TextView t = text(s, s.length() > 12 ? 28 : 34, GREEN);
        t.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
    }

    private TextView text(String s, int size, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextColor(color);
        t.setTypeface(Typeface.MONOSPACE);
        t.setTextSize(size);
        t.setGravity(Gravity.CENTER);
        t.setPadding(0, dp(10), 0, dp(10));
        root.addView(t, wrap());
        return t;
    }

    private Button button(String s, int size, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(s);
        b.setTextSize(size);
        b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = wrap();
        lp.topMargin = dp(8);
        root.addView(b, lp);
        return b;
    }

    private int dp(int v) { return Math.round(v * getResources().getDisplayMetrics().density); }

    private static LinearLayout.LayoutParams wrap() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.CENTER;
        return lp;
    }
}
