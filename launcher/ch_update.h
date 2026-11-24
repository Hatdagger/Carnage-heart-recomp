/* Carnage Heart: update check and self-update from the project's GitHub
 * releases (Hatdagger/carnage-heart-recomp). Nothing here runs by itself: the
 * launcher calls check() or download_and_apply() when the player asks.
 *
 * The release is a setup kit (project sources + recompiler, no game code), so
 * an update replaces the kit's files and then asks for one rebuild. When the
 * update changed something the game C is made from (the recompiler, symbols,
 * seeds, game.toml...), the next start of CarnageHeart_Recompiled regenerates
 * the game from the player's disc and builds it again. When it changed only
 * compiled parts (launcher, hooks in src/, build files), the game C is kept
 * and launcher/ch_quick_rebuild.py only rebuilds, which is quicker. Disc
 * images, memory cards, settings, installed mods, online games and the
 * build-release/ folder (where saves live) are never touched.
 */
#pragma once
#include <cstdint>
#include <string>

namespace ch_update {

enum class State {
    Idle,          /* nothing asked yet */
    Checking,
    UpToDate,
    Available,     /* a newer release exists */
    Downloading,
    Applying,
    Done,          /* installed; restart to rebuild and use it */
    Failed,
};

struct Snapshot {
    State       state = State::Idle;
    std::string current;      /* this build's version, e.g. "0.1.0" */
    std::string latest;       /* newest release found, e.g. "0.2.0" */
    std::string message;      /* last result, one line */
    uint64_t    done = 0;     /* bytes downloaded */
    uint64_t    total = 0;    /* bytes expected, 0 when unknown */
};

const char* current_version();
/* True when this build can replace itself; otherwise why not. */
bool supported(std::string* why);
Snapshot snapshot();
void check();                 /* asks GitHub which release is newest */
void download_and_apply();    /* only after check() found one */
/* After Done: start the rebuild and return true; the caller then closes the
 * launcher. A quick rebuild opens in its own console window and waits for the
 * launcher to close; otherwise the kit's setup exe starts and regenerates. */
bool restart_into_rebuild();

/* "Build Android App" (scripts/android, from the Android port): true when
 * the kit has the builder. start_android_build() opens it in its own console
 * window (Windows: "Build Android App.bat"; Linux: build-android-app.sh in a
 * terminal) and returns at once; false with the reason when it can't. */
bool android_builder_available();
bool start_android_build(std::string* why);

} /* namespace ch_update */
