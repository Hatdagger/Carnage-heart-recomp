/* Carnage Heart: update check and self-update from the project's GitHub
 * releases (Hatdagger/carnage-heart-recomp). Nothing here runs by itself: the
 * launcher calls check() or download_and_apply() when the player asks.
 *
 * The release is a setup kit (project sources + recompiler, no game code), so
 * an update replaces the kit's files and then asks for one rebuild: the next
 * start of CarnageHeart_Recompiled regenerates the game from the player's disc
 * and builds it again. Disc images, memory cards, settings, installed mods and
 * the build-release/ folder (where saves live) are never touched.
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
/* After Done: start the kit's setup exe (which rebuilds) and return true; the
 * caller then closes the launcher. */
bool restart_into_rebuild();

} /* namespace ch_update */
