/* Carnage Heart: online campaign games in the launcher (multiplayer plan
 * step 6). Talks to the turn relay (relay/worker.js) to create and join
 * games, remembers each game's code and this player's key, checks whose turn
 * it is, and hands the game its switches (CH_TURN_RELAY, CH_TURN_GAME,
 * CH_TURN_KEY, CH_TURN_ROLE) when the player starts a game from here.
 *
 * Everything that touches the network runs on one background thread; the
 * menu only reads snapshot() each frame. Network access goes through the
 * system's own curl, as the updater (ch_update.cpp) and the game
 * (src/turn_relay.c) do.
 *
 * Files, beside the game (the same folder the game's turn files use):
 *   online-turns/games.txt    one line per game: CODE ROLE KEY RELAY
 *   online-turns/relay.txt    optional: the relay address (first line)
 *   online-turns/<CODE>/      the game's turn files (written by the game)
 */
#pragma once
#include <string>
#include <vector>

namespace ch_online {

enum class Status {
    Unknown,        /* not checked yet */
    Checking,
    YourTurn,
    TheirTurn,
    Sending,        /* my newest turn file is not on the relay yet */
    GameOver,
    Offline,        /* the relay could not be reached */
    Gone,           /* the relay no longer has this game */
};

struct Game {
    std::string code;          /* "K7QX-M2PD" */
    char        role = 'A';    /* 'A' created the game, 'B' joined it */
    std::string key;           /* this player's secret key */
    std::string relay;         /* the relay the game lives on */
    Status      status = Status::Unknown;
    bool        joined = false;    /* player B has joined */
    int         latest = 0;        /* newest turn file on the relay */
    int         local = 0;         /* newest turn file in the folder */
    std::string detail;            /* one line for the player */
    int         scenario = -1;     /* from the turn files (byte 48): 0 Europa (Practice),
                                      1 Europa, 2 Ganymede, 3 Callisto; -1 none yet */
    int         map = -1;          /* map number within the scenario (byte 49), from 0 */
};

struct Snapshot {
    std::vector<Game> games;
    bool        busy = false;      /* create/join/check running */
    std::string message;           /* result of the last create/join, one line */
    bool        failed = false;    /* that result was an error */
    std::string just_made;         /* code of the game the last create/join added */
};

/* False when this build has no online campaign (built without
 * -DCH_REMOTE_BATTLE=ON); *why says so. */
bool built_in(std::string* why);
/* The relay address new games are made on ("" when none is set up). */
std::string relay_url();

Snapshot snapshot();
void refresh();                          /* check every game, send pending turns */
void create_game();
void join_game(const std::string& code); /* 8 code letters, with or without the dash */
void forget(const std::string& code);    /* forget the key; turn files are kept */
/* Set the game's switches for this game. Call just before launching. */
bool prepare_launch(const std::string& code);
/* Games where it is this player's turn. */
int your_turn_count();

const char* status_label(Status s);      /* short, for a row: "YOUR TURN" */
/* The scenario to start for this game, for the player: "EUROPA (PRACTICE)",
 * "GANYMEDE MAP 2"; before any turn file, the one new games use. */
std::string scenario_label(const Game& g);

} /* namespace ch_online */
