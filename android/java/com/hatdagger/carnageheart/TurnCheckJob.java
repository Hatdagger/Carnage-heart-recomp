package com.hatdagger.carnageheart;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

import java.util.List;

/**
 * "It's your turn" for the online campaign, without a push server: Android
 * runs this about every 15 minutes (JobScheduler's shortest period; the
 * system may stretch it to save battery) when there is a connection. For each
 * remembered game it does what the Online screen's check does (sends any
 * turn file the game could not, fetches new ones) and posts a notification
 * when the other player has played. One notification per game and turn; it
 * goes away when the game's page is opened.
 */
public class TurnCheckJob extends JobService {
    private static final int JOB_ID = 4201;
    private static final String CHANNEL = "turns";
    private static final String PREFS = "turn_notify";

    private volatile Thread worker;

    static void schedule(Context ctx) {
        JobScheduler js = ctx.getSystemService(JobScheduler.class);
        if (js == null || js.getPendingJob(JOB_ID) != null) return;
        JobInfo job = new JobInfo.Builder(JOB_ID, new ComponentName(ctx, TurnCheckJob.class))
            .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
            .setPeriodic(15 * 60 * 1000L)
            .setPersisted(true)                 // survives a restart (RECEIVE_BOOT_COMPLETED)
            .build();
        js.schedule(job);
    }

    static void cancel(Context ctx) {
        JobScheduler js = ctx.getSystemService(JobScheduler.class);
        if (js != null) js.cancel(JOB_ID);
    }

    @Override
    public boolean onStartJob(JobParameters params) {
        worker = new Thread(() -> {
            boolean any = false;
            try {
                List<Relay.Game> games = Relay.load(this);
                any = !games.isEmpty();
                for (Relay.Game g : games) {
                    if (Thread.interrupted()) return;
                    Relay.check(this, g);
                    // While the game itself is open it fetches and shows the
                    // turn on its own; no notification needed.
                    if (!GameActivity.running) notifyIfNew(this, g);
                }
            } finally {
                if (!any) cancel(this);
                jobFinished(params, false);
            }
        }, "ch-turn-check");
        worker.start();
        return true;
    }

    @Override
    public boolean onStopJob(JobParameters params) {
        Thread t = worker;
        if (t != null) t.interrupt();
        return true;                            // try again later
    }

    private static int notifyId(String code) { return 1000 + (code.hashCode() & 0xFFFF); }

    /** The player has seen this game's state (Online screen): don't notify
     *  about turns up to here. */
    static void noteSeen(Context ctx, Relay.Game g) {
        if (g.status == Relay.Status.YOUR_TURN || g.status == Relay.Status.OVER)
            ctx.getSharedPreferences(PREFS, MODE_PRIVATE).edit().putInt(g.code, g.latest).apply();
    }

    static void dismiss(Context ctx, String code) {
        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        if (nm != null) nm.cancel(notifyId(code));
    }

    static void notifyIfNew(Context ctx, Relay.Game g) {
        boolean turn = g.status == Relay.Status.YOUR_TURN && g.latest > 0;
        boolean over = g.status == Relay.Status.OVER;
        if (!turn && !over) {
            if (g.status == Relay.Status.THEIR_TURN) dismiss(ctx, g.code);
            return;
        }
        SharedPreferences p = ctx.getSharedPreferences(PREFS, MODE_PRIVATE);
        if (p.getInt(g.code, 0) >= g.latest) return;
        p.edit().putInt(g.code, g.latest).apply();

        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        if (nm == null) return;
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Online turns",
            NotificationManager.IMPORTANCE_DEFAULT));
        Intent open = new Intent(ctx, OnlineActivity.class)
            .putExtra(OnlineActivity.EXTRA_GAME, g.code)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent pi = PendingIntent.getActivity(ctx, notifyId(g.code), open,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        char other = g.role == 'A' ? 'B' : 'A';
        String title = over ? "Carnage Heart: game " + g.code + " is over"
                            : "Carnage Heart: it's your turn";
        String text = over ? "Open it to see who won."
                           : "Player " + other + " has played (game " + g.code + ", turn " + ((g.latest + 2) / 2) + ").";
        Notification n = new Notification.Builder(ctx, CHANNEL)
            .setSmallIcon(smallIcon(ctx))
            .setContentTitle(title)
            .setContentText(text)
            .setContentIntent(pi)
            .setAutoCancel(true)
            .build();
        try {
            nm.notify(notifyId(g.code), n);
        } catch (SecurityException ignored) {
            // Notifications not allowed (Android 13+ permission refused).
        }
    }

    /** The white status-bar icon (res/drawable/ic_stat_turn.xml). This build has
     *  no R class, so it is looked up by name; the app icon is the fallback. */
    static int smallIcon(Context ctx) {
        int id = ctx.getResources().getIdentifier("ic_stat_turn", "drawable", ctx.getPackageName());
        return id != 0 ? id : ctx.getApplicationInfo().icon;
    }
}
