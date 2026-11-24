/* Carnage Heart: online campaign games in the launcher. See ch_online.h. */
#include "ch_online.h"

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/wait.h>
extern char** environ;
#endif

/* Set by the build when the game has turn files (-DCH_REMOTE_BATTLE=ON). */
#ifndef CH_ONLINE_CAMPAIGN
#  define CH_ONLINE_CAMPAIGN 0
#endif
/* A relay address built in (-DCH_TURN_RELAY_URL=...); online-turns/relay.txt
 * or the CH_TURN_RELAY variable override it. */
#ifndef CH_TURN_RELAY_DEFAULT
#  define CH_TURN_RELAY_DEFAULT ""
#endif

namespace fs = std::filesystem;

namespace ch_online {
namespace {

const char* const kDir = "online-turns";
const char* const kCodeChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::mutex& g_mu = *new std::mutex;
Snapshot&  g_snap = *new Snapshot;
bool       g_loaded = false;

/* ---- the background thread ------------------------------------------------- */

/* Never destroyed: the worker thread is detached and may still be waiting on
 * these when the program exits (destroying a condition variable someone
 * waits on hangs the exit). */
std::mutex& g_qmu = *new std::mutex;
std::condition_variable& g_qcv = *new std::condition_variable;
std::deque<std::function<void()>>& g_queue = *new std::deque<std::function<void()>>;
bool g_thread = false;
std::atomic<bool> g_refresh_queued{false};

void post(std::function<void()> job) {
    std::lock_guard<std::mutex> l(g_qmu);
    g_queue.push_back(std::move(job));
    if (!g_thread) {
        g_thread = true;
        std::thread([] {
            for (;;) {
                std::function<void()> j;
                {
                    std::unique_lock<std::mutex> l(g_qmu);
                    g_qcv.wait(l, [] { return !g_queue.empty(); });
                    j = std::move(g_queue.front());
                    g_queue.pop_front();
                }
                { std::lock_guard<std::mutex> l(g_mu); g_snap.busy = true; }
                j();
                bool more;
                { std::lock_guard<std::mutex> l(g_qmu); more = !g_queue.empty(); }
                { std::lock_guard<std::mutex> l(g_mu); g_snap.busy = more; }
            }
        }).detach();
    }
    g_qcv.notify_one();
}

/* ---- running curl ------------------------------------------------------------ */

#ifdef _WIN32
int run_curl(const std::vector<std::string>& args) {
    wchar_t sys[MAX_PATH] = {0};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring exe = std::wstring(sys) + L"\\curl.exe";
    std::wstring cmd = L"\"" + exe + L"\"";
    for (const std::string& a : args) cmd += L" \"" + fs::u8path(a).wstring() + L"\"";
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    std::memset(&si, 0, sizeof si); si.cb = sizeof si;
    std::memset(&pi, 0, sizeof pi);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}
#else
int run_curl(const std::vector<std::string>& args) {
    std::vector<std::string> av = { "curl" };
    av.insert(av.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : av) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
#endif

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* One request to the relay. Returns the HTTP status (or -1 when curl could not
 * run or reach the relay); the response body goes to *body or into body_file. */
int http(const std::string& method, const std::string& url, const std::string& key,
         const fs::path& upload, std::string* body, const fs::path& body_file = {}) {
    std::error_code ec;
    const fs::path tmp = fs::path(kDir) / ".tmp";
    fs::create_directories(tmp, ec);
    const fs::path hdr = tmp / "headers.txt", out = body_file.empty() ? tmp / "body.txt" : body_file;
    fs::remove(hdr, ec);
    fs::remove(out, ec);
    std::vector<std::string> a = { "-sS", "--connect-timeout", "10", "--max-time", "30",
                                   "-A", "CarnageHeart-Launcher", "-X", method,
                                   "-D", hdr.u8string(), "-o", out.u8string() };
#ifdef _WIN32
    a.push_back("--ssl-no-revoke");
#endif
    if (!key.empty()) { a.push_back("-H"); a.push_back("X-CH-Key: " + key); }
    if (!upload.empty()) {
        a.push_back("-H"); a.push_back("Content-Type: application/octet-stream");
        a.push_back("--data-binary"); a.push_back("@" + upload.u8string());
    }
    a.push_back(url);
    const int rc = run_curl(a);
    if (rc != 0) return -1;
    /* The last status line (a proxy may add its own block first). */
    int status = -1;
    std::istringstream hs(read_file(hdr));
    std::string line;
    while (std::getline(hs, line))
        if (line.compare(0, 5, "HTTP/") == 0) {
            const size_t sp = line.find(' ');
            if (sp != std::string::npos) status = std::atoi(line.c_str() + sp + 1);
        }
    if (body) *body = read_file(out);
    return status;
}

/* Tiny JSON readers for the relay's flat answers. */
std::string json_str(const std::string& j, const char* name) {
    const std::string k = std::string("\"") + name + "\":\"";
    const size_t p = j.find(k);
    if (p == std::string::npos) return "";
    const size_t e = j.find('"', p + k.size());
    return e == std::string::npos ? "" : j.substr(p + k.size(), e - p - k.size());
}
long json_num(const std::string& j, const char* name, long dflt) {
    const std::string k = std::string("\"") + name + "\":";
    const size_t p = j.find(k);
    return p == std::string::npos ? dflt : std::atol(j.c_str() + p + k.size());
}
bool json_true(const std::string& j, const char* name) {
    const std::string k = std::string("\"") + name + "\":true";
    return j.find(k) != std::string::npos;
}

/* ---- remembered games ---------------------------------------------------------- */

fs::path games_file() { return fs::path(kDir) / "games.txt"; }
fs::path folder_of(const std::string& code) { return fs::path(kDir) / code; }
fs::path turn_path(const std::string& code, int seq) {
    char name[32];
    std::snprintf(name, sizeof name, "turn-%06d.cht", seq);
    return folder_of(code) / name;
}

void load_locked() {
    if (g_loaded) return;
    g_loaded = true;
    std::ifstream f(games_file());
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        Game g;
        std::string role;
        if (!(ls >> g.code >> role >> g.key >> g.relay)) continue;
        g.role = role == "B" ? 'B' : 'A';
        g_snap.games.push_back(g);
    }
}

bool save_locked() {
    std::error_code ec;
    fs::create_directories(kDir, ec);
    const fs::path tmp = games_file().u8string() + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        for (const Game& g : g_snap.games)
            f << g.code << ' ' << g.role << ' ' << g.key << ' ' << g.relay << '\n';
        if (!f) return false;
    }
    fs::remove(games_file(), ec);
    fs::rename(tmp, games_file(), ec);
    return !ec;
}

void set_result(const std::string& msg, bool failed, const std::string& made = "") {
    std::lock_guard<std::mutex> l(g_mu);
    g_snap.message = msg;
    g_snap.failed = failed;
    g_snap.just_made = made;
}

std::string base_of(std::string u) {
    while (!u.empty() && (u.back() == '/' || std::isspace((unsigned char)u.back()))) u.pop_back();
    return u;
}

/* Turn files numbered 1, 2, 3... with no gaps (as the game writes them). */
int local_latest(const std::string& code) {
    int s = 0;
    std::error_code ec;
    while (fs::exists(turn_path(code, s + 1), ec)) ++s;
    return s;
}

/* Flags byte of a turn file (bit 0: game over), or -1. */
int turn_flags(const std::string& code, int seq) {
    std::ifstream f(turn_path(code, seq), std::ios::binary);
    char h[8];
    if (!f.read(h, 8) || std::memcmp(h, "CHTF", 4) != 0) return -1;
    return (unsigned char)h[7];
}

/* Scenario and map from a turn file's header (version 2 and later keep them
 * in bytes 48 and 49; see src/turn_files.c). False for older or unreadable
 * files. */
bool turn_scenario(const std::string& code, int seq, int* scen, int* map) {
    std::ifstream f(turn_path(code, seq), std::ios::binary);
    unsigned char h[52];
    if (!f.read((char*)h, sizeof h) || std::memcmp(h, "CHTF", 4) != 0) return false;
    if ((h[4] | (h[5] << 8)) < 2) return false;
    *scen = h[48];
    *map = h[49];
    return true;
}

bool is_mine(char role, int seq) { return ((seq & 1) == 1) == (role == 'A'); }

std::string upper_code(const std::string& s) {
    std::string c;
    for (char ch : s) {
        const char u = (char)std::toupper((unsigned char)ch);
        if (std::strchr(kCodeChars, u) && u) c += u;
    }
    if (c.size() != 8) return "";
    return c.substr(0, 4) + "-" + c.substr(4);
}

/* Check one game: bring the folder up to date with the relay (send my turn
 * files it does not have, fetch the ones I do not have) and work out whose
 * turn it is. Runs on the worker thread with a copy of the game. */
void check_one(Game& g) {
    const std::string base = base_of(g.relay) + "/v1/games/" + g.code;
    std::string body;
    int st = http("GET", base, "", {}, &body);
    if (st == 404) { g.status = Status::Gone; g.detail = "THE RELAY NO LONGER HAS THIS GAME"; return; }
    if (st != 200) { g.status = Status::Offline; g.detail = "COULD NOT REACH THE RELAY"; return; }
    g.joined = json_true(body, "joined");
    g.latest = (int)json_num(body, "latest", 0);

    std::error_code ec;
    fs::create_directories(folder_of(g.code), ec);
    g.local = local_latest(g.code);
    /* My turn files the relay does not have yet (the game was closed before
     * it could send them). */
    for (int s = g.latest + 1; s <= g.local && is_mine(g.role, s); ++s) {
        st = http("PUT", base + "/turns/" + std::to_string(s), g.key, turn_path(g.code, s), &body);
        if (st != 200 && st != 201) break;
        g.latest = s;
    }
    /* Turn files I do not have (the other player's newest, or all of them on
     * a new computer). */
    for (int s = g.local + 1; s <= g.latest; ++s) {
        const fs::path dst = turn_path(g.code, s), part = dst.u8string() + ".dl";
        st = http("GET", base + "/turns/" + std::to_string(s), "", {}, nullptr, part);
        std::ifstream f(part, std::ios::binary);
        char magic[4] = {0};
        const bool ok = st == 200 && f.read(magic, 4) && std::memcmp(magic, "CHTF", 4) == 0;
        f.close();
        if (!ok) { fs::remove(part, ec); break; }
        fs::remove(dst, ec);
        fs::rename(part, dst, ec);
        if (ec) break;
    }
    g.local = local_latest(g.code);
    if (g.local > 0) turn_scenario(g.code, g.local, &g.scenario, &g.map);

    const int next = g.latest + 1;
    const int round = (g.latest + 2) / 2;
    char buf[160];
    const int flags = g.local > 0 ? turn_flags(g.code, g.local) : -1;
    if (flags > 0 && (flags & 1) && g.local >= g.latest) {
        g.status = Status::GameOver;
        g.detail = "THE GAME IS OVER";
    } else if (g.local > g.latest && is_mine(g.role, g.latest + 1)) {
        g.status = Status::Sending;
        g.detail = "YOUR TURN IS NOT ON THE RELAY YET. IT IS SENT WHEN THE RELAY CAN BE REACHED";
    } else if (is_mine(g.role, next)) {
        g.status = Status::YourTurn;
        std::snprintf(buf, sizeof buf, "TURN %d. IT'S YOUR TURN%s", round,
                      g.joined ? "" : " (PLAYER B HAS NOT JOINED YET)");
        g.detail = buf;
    } else {
        g.status = Status::TheirTurn;
        if (!g.joined)
            g.detail = "WAITING FOR PLAYER B TO JOIN WITH THE CODE";
        else {
            std::snprintf(buf, sizeof buf, "TURN %d. WAITING FOR PLAYER %c", round, g.role == 'A' ? 'B' : 'A');
            g.detail = buf;
        }
    }
}

void check_all() {
    g_refresh_queued = false;
    std::vector<Game> games;
    {
        std::lock_guard<std::mutex> l(g_mu);
        load_locked();
        for (Game& g : g_snap.games) { g.status = Status::Checking; }
        games = g_snap.games;
    }
    for (Game& g : games) {
        check_one(g);
        std::lock_guard<std::mutex> l(g_mu);
        for (Game& s : g_snap.games)
            if (s.code == g.code) s = g;
    }
}

void add_game(const Game& g) {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    g_snap.games.insert(g_snap.games.begin(), g);
    save_locked();
}

void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name);
    else setenv(name, value.c_str(), 1);
#endif
}

} /* namespace */

bool built_in(std::string* why) {
    if (CH_ONLINE_CAMPAIGN) return true;
    if (why) *why = "This build of the game does not include the online campaign yet.";
    return false;
}

std::string relay_url() {
    const char* e = std::getenv("CH_TURN_RELAY");
    if (e && *e) return base_of(e);
    std::ifstream f(fs::path(kDir) / "relay.txt");
    std::string line;
    if (std::getline(f, line) && !base_of(line).empty()) return base_of(line);
    return base_of(CH_TURN_RELAY_DEFAULT);
}

Snapshot snapshot() {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    return g_snap;
}

void refresh() {
    if (!CH_ONLINE_CAMPAIGN) return;
    {
        std::lock_guard<std::mutex> l(g_mu);
        load_locked();
        if (g_snap.games.empty()) return;
    }
    if (g_refresh_queued.exchange(true)) return;
    post(check_all);
}

void create_game() {
    const std::string relay = relay_url();
    if (relay.empty()) { set_result("NO RELAY ADDRESS IS SET UP", true); return; }
    set_result("MAKING A NEW GAME...", false);
    post([relay] {
        std::string body;
        const int st = http("POST", relay + "/v1/games", "", {}, &body);
        Game g;
        g.code = json_str(body, "game");
        g.key = json_str(body, "key");
        g.role = 'A';
        g.relay = relay;
        if (st == -1) { set_result("COULD NOT REACH THE RELAY. CHECK THE CONNECTION", true); return; }
        if (st != 201 || g.code.empty() || g.key.empty()) {
            set_result("THE RELAY DID NOT MAKE A GAME (ERROR " + std::to_string(st) + ")", true);
            return;
        }
        add_game(g);
        set_result("NEW GAME " + g.code + ". GIVE THIS CODE TO THE OTHER PLAYER", false, g.code);
        check_all();
    });
}

void join_game(const std::string& code_in) {
    const std::string relay = relay_url();
    const std::string code = upper_code(code_in);
    if (relay.empty()) { set_result("NO RELAY ADDRESS IS SET UP", true); return; }
    if (code.empty()) { set_result("A GAME CODE HAS 8 LETTERS AND NUMBERS", true); return; }
    {
        std::lock_guard<std::mutex> l(g_mu);
        load_locked();
        for (const Game& g : g_snap.games)
            if (g.code == code) {
                g_snap.message = "YOU ARE ALREADY IN GAME " + code;
                g_snap.failed = false;
                g_snap.just_made = code;
                return;
            }
    }
    set_result("JOINING " + code + "...", false);
    post([relay, code] {
        std::string body;
        const int st = http("POST", relay + "/v1/games/" + code + "/join", "", {}, &body);
        if (st == -1) { set_result("COULD NOT REACH THE RELAY. CHECK THE CONNECTION", true); return; }
        if (st == 404) { set_result("THERE IS NO GAME " + code + ". CHECK THE CODE", true); return; }
        if (st == 409) { set_result("GAME " + code + " ALREADY HAS TWO PLAYERS", true); return; }
        Game g;
        g.code = json_str(body, "game");
        g.key = json_str(body, "key");
        g.role = 'B';
        g.relay = relay;
        if (st != 201 || g.code.empty() || g.key.empty()) {
            set_result("THE RELAY DID NOT LET YOU JOIN (ERROR " + std::to_string(st) + ")", true);
            return;
        }
        add_game(g);
        set_result("YOU JOINED GAME " + g.code + " AS PLAYER B", false, g.code);
        check_all();
    });
}

void forget(const std::string& code) {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    for (size_t i = 0; i < g_snap.games.size(); ++i)
        if (g_snap.games[i].code == code) {
            g_snap.games.erase(g_snap.games.begin() + (long)i);
            break;
        }
    save_locked();
}

bool prepare_launch(const std::string& code) {
    Game g;
    {
        std::lock_guard<std::mutex> l(g_mu);
        load_locked();
        bool found = false;
        for (const Game& s : g_snap.games)
            if (s.code == code) { g = s; found = true; }
        if (!found) return false;
    }
    set_env("CH_TURN_RELAY", base_of(g.relay));
    set_env("CH_TURN_GAME", g.code);
    set_env("CH_TURN_KEY", g.key);
    set_env("CH_TURN_ROLE", std::string(1, g.role));
    set_env("CH_TURN_DIR", "");           /* the game's default: online-turns/<code> */
    return true;
}

int your_turn_count() {
    std::lock_guard<std::mutex> l(g_mu);
    int n = 0;
    for (const Game& g : g_snap.games) n += g.status == Status::YourTurn;
    return n;
}

std::string scenario_label(const Game& g) {
    static const char* names[] = { "EUROPA (PRACTICE)", "EUROPA", "GANYMEDE", "CALLISTO" };
    if (g.scenario < 0) return "EUROPA (PRACTICE)";   /* new online games: Practice only */
    if (g.scenario > 3) return "AN UNKNOWN SCENARIO";
    std::string n = names[g.scenario];
    if (g.scenario != 0 && g.map > 0) n += " MAP " + std::to_string(g.map + 1);
    return n;
}

const char* status_label(Status s) {
    switch (s) {
        case Status::Unknown:   return "";
        case Status::Checking:  return "CHECKING";
        case Status::YourTurn:  return "YOUR TURN";
        case Status::TheirTurn: return "WAITING";
        case Status::Sending:   return "SENDING";
        case Status::GameOver:  return "OVER";
        case Status::Offline:   return "OFFLINE";
        case Status::Gone:      return "GONE";
    }
    return "";
}

} /* namespace ch_online */
