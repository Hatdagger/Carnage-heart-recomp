/* Carnage Heart: update check and self-update. See ch_update.h.
 *
 * Based on the Master of Arena updater (Hatdagger/AC1Recomp,
 * games/master-of-arena/moa_update.cpp), changed for the setup kit layout.
 *
 * Network access goes through the system's own curl (Windows 10+ and every
 * desktop Linux ship one), run without a window; the program carries no TLS
 * code. The release list comes from the GitHub API and the kit zip from the
 * release page. A download is accepted only when its SHA-256 matches the
 * digest GitHub lists for the asset (or the release's SHA256SUMS.txt).
 *
 * Applying: the zip is unpacked into <kit>/Update/stage, and each file is moved
 * over the old one (the old one is renamed first, so a running file can be
 * replaced on Windows and every move can be undone if one fails). The player's
 * files are skipped (protected_path). Last, when a file the game C is made
 * from changed (needs_regenerate), <kit>/generated is removed: the setup exe
 * then sees the game code is missing, regenerates it from the disc and
 * rebuilds build-release/ on its next start. When only compiled files changed,
 * generated/ stays and launcher/ch_quick_rebuild.py rebuilds without
 * regenerating (start_quick_rebuild).
 */
#include "ch_update.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

extern "C" {
#include "psx_sha256.h"
}

#ifdef _WIN32
#  include <windows.h>
#else
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <fcntl.h>
#  include <unistd.h>
extern char** environ;
#endif

#ifndef CH_VERSION
#  define CH_VERSION "dev"
#endif

namespace fs = std::filesystem;

namespace ch_update {
namespace {

const char* const kRepo = "Hatdagger/carnage-heart-recomp";
const char* const kSums = "SHA256SUMS.txt";
const char* const kZipPrefix = "carnageheart-setup-";
#ifdef _WIN32
const char* const kAssetSuffix = "-windows-x64.zip";
const char* const kSetupExe = "CarnageHeart_Recompiled.exe";
#else
const char* const kAssetSuffix = "-linux-x64.zip";
const char* const kSetupExe = "CarnageHeart_Recompiled";
#endif
const char* const kOldSuffix = ".ch-old";

struct Asset { std::string name, url, digest; uint64_t size = 0; };

std::mutex g_mu;
Snapshot   g_snap;
Asset      g_asset, g_sums;       /* what the last check chose */
std::atomic<bool> g_busy{false};
bool       g_quick = false;       /* the installed update keeps generated/ (see needs_regenerate) */

void set(State s, const std::string& msg) {
    std::lock_guard<std::mutex> l(g_mu);
    g_snap.state = s;
    g_snap.message = msg;
}

/* ---- versions ------------------------------------------------------------- */

bool parse_version(const std::string& s, int v[4]) {
    v[0] = v[1] = v[2] = v[3] = 0;
    size_t i = 0;
    while (i < s.size() && !std::isdigit((unsigned char)s[i])) ++i;
    if (i == s.size()) return false;
    for (int k = 0; k < 4 && i < s.size(); ++k) {
        v[k] = std::atoi(s.c_str() + i);
        while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
        if (i < s.size() && s[i] == '.') ++i; else break;
    }
    return true;
}

int compare(const std::string& a, const std::string& b) {
    int x[4], y[4];
    parse_version(a, x);
    parse_version(b, y);
    for (int i = 0; i < 4; ++i)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

/* ---- running programs ----------------------------------------------------- */

bool safe_url(const std::string& u) {
    /* GitHub serves release assets case-insensitively by owner/repo. */
    std::string low = u;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    std::string a = std::string("https://github.com/") + kRepo + "/releases/download/";
    for (char& c : a) c = (char)std::tolower((unsigned char)c);
    if (low.compare(0, a.size(), a) != 0) return false;
    for (char c : u)
        if (!(std::isalnum((unsigned char)c) || std::strchr(".-_/:%+", c))) return false;
    return true;
}

#ifdef _WIN32
std::wstring system_tool(const wchar_t* name) {
    wchar_t sys[MAX_PATH] = {0};
    GetSystemDirectoryW(sys, MAX_PATH);
    return std::wstring(sys) + L"\\" + name;
}

/* Runs exe with args (each quoted), no window; exit code or -1. */
int run_wait(const std::wstring& exe, const std::vector<std::wstring>& args) {
    std::wstring cmd = L"\"" + exe + L"\"";
    for (const std::wstring& a : args) cmd += L" \"" + a + L"\"";
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
/* Runs argv[0] from PATH with output discarded; exit code or -1. */
int run_wait(const std::vector<std::string>& args) {
    std::vector<std::string> av = args;
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

std::vector<std::string> curl_args(const std::string& url, const fs::path& out, bool api) {
    std::vector<std::string> a = { "-fsSL", "--connect-timeout", "20", "--max-time", "1800",
                                   "--retry", "2", "-A", "CarnageHeart-Updater" };
#ifdef _WIN32
    a.push_back("--ssl-no-revoke");
#endif
    if (api) { a.push_back("-H"); a.push_back("Accept: application/vnd.github+json"); }
    a.push_back("-o"); a.push_back(out.u8string());
    a.push_back(url);
    return a;
}

int run_curl(const std::vector<std::string>& args) {
#ifdef _WIN32
    std::vector<std::wstring> w;
    for (const std::string& a : args) w.push_back(fs::u8path(a).wstring());
    return run_wait(system_tool(L"curl.exe"), w);
#else
    std::vector<std::string> av = { "curl" };
    av.insert(av.end(), args.begin(), args.end());
    return run_wait(av);
#endif
}

std::string curl_failure(int rc) {
    if (rc == -1) return "CURL IS NOT INSTALLED ON THIS SYSTEM";
    if (rc == 6 || rc == 7 || rc == 28 || rc == 35 || rc == 56) return "COULD NOT REACH GITHUB. CHECK THE CONNECTION";
    if (rc == 22) return "GITHUB REFUSED THE REQUEST (RATE LIMIT OR NO SUCH FILE)";
    return "THE DOWNLOAD FAILED (CURL ERROR " + std::to_string(rc) + ")";
}

/* ---- the release list ------------------------------------------------------ */

struct Release { std::string tag; bool draft = false, pre = false; std::vector<Asset> assets; };

std::string read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/* The string value of "key": after pos and before end, or "". */
std::string str_field(const std::string& j, const char* key, size_t pos, size_t end) {
    const std::string k = std::string("\"") + key + "\":";
    size_t p = j.find(k, pos);
    if (p == std::string::npos || p >= end) return "";
    p += k.size();
    while (p < j.size() && j[p] == ' ') ++p;
    if (p >= j.size() || j[p] != '"') return "";
    const size_t e = j.find('"', p + 1);
    return e == std::string::npos ? "" : j.substr(p + 1, e - p - 1);
}

bool bool_field(const std::string& j, const char* key, size_t pos, size_t end) {
    const std::string k = std::string("\"") + key + "\":";
    size_t p = j.find(k, pos);
    if (p == std::string::npos || p >= end) return false;
    p += k.size();
    while (p < j.size() && j[p] == ' ') ++p;
    return j.compare(p, 4, "true") == 0;
}

/* GitHub's release JSON, read just far enough: each release's tag, flags and
 * assets (name, url, size, digest). Fields of one asset sit between its
 * "url" key and the next asset's. */
std::vector<Release> parse_releases(const std::string& j) {
    std::vector<Release> out;
    const std::string kTag = "\"tag_name\":";
    std::vector<size_t> tags;
    for (size_t p = j.find(kTag); p != std::string::npos; p = j.find(kTag, p + 1)) tags.push_back(p);
    for (size_t i = 0; i < tags.size(); ++i) {
        const size_t end = i + 1 < tags.size() ? tags[i + 1] : j.size();
        Release r;
        r.tag = str_field(j, "tag_name", tags[i], end);
        r.draft = bool_field(j, "draft", tags[i], end);
        r.pre = bool_field(j, "prerelease", tags[i], end);
        const std::string kDl = "\"browser_download_url\":";
        /* An asset's block runs from the previous asset's download url (or the
         * "assets" key) to its own download url. */
        size_t block = j.find("\"assets\":", tags[i]);
        if (block == std::string::npos || block >= end) block = tags[i];
        for (size_t p = j.find(kDl, block); p != std::string::npos && p < end; p = j.find(kDl, p + 1)) {
            Asset a;
            a.url = str_field(j, "browser_download_url", p, end);
            a.name = a.url.substr(a.url.rfind('/') + 1);
            const size_t sk = j.find("\"size\":", block);
            if (sk != std::string::npos && sk < p)
                a.size = std::strtoull(j.c_str() + sk + 7, nullptr, 10);
            std::string d = str_field(j, "digest", block, p);
            if (d.compare(0, 7, "sha256:") == 0) a.digest = d.substr(7);
            for (char& c : a.digest) c = (char)std::tolower((unsigned char)c);
            block = p + kDl.size();
            r.assets.push_back(a);
        }
        out.push_back(r);
    }
    return out;
}

fs::path scratch_dir() {
    std::error_code ec;
    fs::path d = fs::temp_directory_path(ec) / "carnageheart-update";
    fs::create_directories(d, ec);
    return d;
}

/* ---- where this build lives ------------------------------------------------ */

fs::path self_exe() {
#ifdef _WIN32
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    if (!n || n >= buf.size()) return {};
    return fs::path(std::wstring(buf.data(), n));
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : p;
#endif
}

/* The unzipped kit: the folder holding the setup exe, the project files and
 * the psxrecomp SDK. The running program is either that setup exe or the
 * built game under build-release/. Empty when not found. */
fs::path kit_root() {
    std::error_code ec;
    fs::path d = self_exe().parent_path();
    for (int i = 0; i < 4 && !d.empty(); ++i) {
        if (fs::is_regular_file(d / "psxrecomp" / "psxrecomp_cli.py", ec) &&
            fs::is_regular_file(d / "game.toml", ec) &&
            fs::is_regular_file(d / "CMakeLists.txt", ec))
            return d;
        if (d == d.parent_path()) break;
        d = d.parent_path();
    }
    return {};
}

bool is_dev_checkout(const fs::path& root) {
    std::error_code ec;
    return fs::exists(root / ".git", ec);
}

/* ---- checking a download --------------------------------------------------- */

std::string sha256_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    psx_sha256_ctx c;
    psx_sha256_init(&c);
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        if (f.gcount() > 0) psx_sha256_update(&c, (const uint8_t*)buf.data(), (size_t)f.gcount());
    }
    uint8_t h[32];
    psx_sha256_final(&c, h);
    char hex[65];
    for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", h[i]);
    return hex;
}

/* The hash a SHA256SUMS.txt lists for a file name; empty when not listed. */
std::string listed_hash(const std::string& sums, const std::string& name) {
    size_t p = 0;
    while (p < sums.size()) {
        size_t q = sums.find('\n', p);
        if (q == std::string::npos) q = sums.size();
        std::string line = sums.substr(p, q - p);
        p = q + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t sp = line.find_first_of(" \t");
        if (sp != 64) continue;
        size_t n = sp;
        while (n < line.size() && (line[n] == ' ' || line[n] == '\t' || line[n] == '*')) ++n;
        std::string listed = line.substr(n);
        listed = listed.substr(listed.find_last_of("/\\") + 1);
        if (listed == name) {
            std::string h = line.substr(0, 64);
            for (char& c : h) c = (char)std::tolower((unsigned char)c);
            return h;
        }
    }
    return "";
}

bool same_content(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec)) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    std::vector<char> x(1 << 20), y(1 << 20);
    while (fa && fb) {
        fa.read(x.data(), (std::streamsize)x.size());
        fb.read(y.data(), (std::streamsize)y.size());
        if (fa.gcount() != fb.gcount() || std::memcmp(x.data(), y.data(), (size_t)fa.gcount()) != 0) return false;
    }
    return true;
}

/* ---- moving the new files over the old ----------------------------------------- */

void sweep_old(const fs::path& root) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        if (it->is_directory(ec) && (p.filename() == "build-release" || p.filename() == "generated" ||
                                    p.filename() == ".git")) {
            it.disable_recursion_pending();
            continue;
        }
        if (p.extension() == kOldSuffix) { std::error_code r; fs::remove(p, r); }
    }
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

bool ends_with_ci(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)s[s.size() - n + i]) != std::tolower((unsigned char)suffix[i]))
            return false;
    return true;
}

/* Never written by an update (paths relative to the kit root). */
bool protected_path(const std::string& rel) {
    /* The built game, its saves and settings; regenerated code; scratch. */
    for (const char* d : { "build-release/", "generated/", "disc/", "saves/", "cache/",
                           "netplay/", "Update/", ".git/",
                           /* online games (keys + turn files), Android build output */
                           "online-turns/", "build-android/", "build-android-work/" })
        if (starts_with(rel, d)) return true;
    /* Mods the release ships are replaced and added; the player's are not. */
    if (starts_with(rel, "mods/")) {
        return !(starts_with(rel, "mods/bundled/") || starts_with(rel, "mods/preloaded/"));
    }
    for (const char* ext : { ".mcd", ".mcr", ".cue", ".bin", ".chd", ".iso", ".img", ".ccd", ".sub" })
        if (ends_with_ci(rel, ext)) return true;
    return false;
}

/* The player's own copies of these are kept; a fresh install gets the kit's. */
bool keep_if_present(const std::string& rel) {
    static const std::set<std::string> user = {
        "disc.cfg", "bios.cfg", "rom.cfg", "launcher.ini", "launcher-window.ini",
        "config.ini", "settings.toml", "keybinds.ini", "input.ini", "overlay_captures.json" };
    return user.count(rel) != 0;
}

bool unpack(const fs::path& zip, const fs::path& stage) {
#ifdef _WIN32
    /* Windows 10+ ships bsdtar, which reads zip. */
    return run_wait(system_tool(L"tar.exe"),
                    { L"-xf", zip.wstring(), L"-C", stage.wstring() }) == 0;
#else
    if (run_wait({ "unzip", "-q", "-o", zip.string(), "-d", stage.string() }) == 0) return true;
    if (run_wait({ "bsdtar", "-xf", zip.string(), "-C", stage.string() }) == 0) return true;
    /* Python is already needed to build the game; keep the zip's file modes. */
    static const char* kPy =
        "import sys,zipfile,os\n"
        "z=zipfile.ZipFile(sys.argv[1])\n"
        "for i in z.infolist():\n"
        "  p=z.extract(i,sys.argv[2])\n"
        "  m=(i.external_attr>>16)&0o777\n"
        "  if m and not i.is_dir(): os.chmod(p,m)\n";
    for (const char* py : { "python3", "python" })
        if (run_wait({ py, "-c", kPy, zip.string(), stage.string() }) == 0) return true;
    return false;
#endif
}

/* ---- quick rebuild (launcher/ch_quick_rebuild.py) ------------------------------ */

const char* const kQuickScript = "launcher/ch_quick_rebuild.py";

struct QuickTools { fs::path python, tc_bin; };

#ifdef _WIN32
/* The wizard's rebuild console (build-release/recomp_deferred_rebuild.cmd,
 * written by the setup exe at install) names the compiler pack's Python and
 * tool folder: set "PYTHON=..." and set "TC_BIN=..." ('%' written as '%%'). */
bool quick_tools(const fs::path& root, QuickTools* t) {
    std::ifstream f(root / "build-release" / "recomp_deferred_rebuild.cmd", std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        for (const char* key : { "set \"PYTHON=", "set \"TC_BIN=" }) {
            if (line.compare(0, std::strlen(key), key) != 0 || line.size() < std::strlen(key) + 1 ||
                line.back() != '"')
                continue;
            std::string v = line.substr(std::strlen(key), line.size() - std::strlen(key) - 1);
            std::string out;
            for (size_t i = 0; i < v.size(); ++i) {
                out += v[i];
                if (v[i] == '%' && i + 1 < v.size() && v[i + 1] == '%') ++i;
            }
            (key[5] == 'P' ? t->python : t->tc_bin) = fs::u8path(out);
        }
    }
    std::error_code ec;
    return !t->python.empty() && fs::is_regular_file(t->python, ec);
}
#else
bool quick_tools(const fs::path&, QuickTools* t) {
    t->python = "python3";
    return true;
}
#endif

bool quick_rebuild_possible(const fs::path& root) {
    std::error_code ec;
    QuickTools t;
    return fs::is_directory(root / "build-release", ec) &&
           fs::is_regular_file(root / fs::u8path(kQuickScript), ec) &&
           quick_tools(root, &t);
}

/* Opens the quick rebuild in its own console window and returns at once; it
 * waits for this program to close before it builds. */
bool start_quick_rebuild(const fs::path& root) {
    QuickTools t;
    if (!quick_tools(root, &t)) return false;
    const fs::path script = root / fs::u8path(kQuickScript);
#ifdef _WIN32
    const std::wstring pid = std::to_wstring(GetCurrentProcessId());
    std::wstring cmd = L"\"" + t.python.wstring() + L"\" \"" + script.wstring() + L"\" \"" +
                       root.wstring() + L"\" " + pid + L" \"" + t.tc_bin.wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    std::memset(&si, 0, sizeof si); si.cb = sizeof si;
    std::memset(&pi, 0, sizeof pi);
    const std::wstring cwd = root.wstring();
    if (!CreateProcessW(t.python.c_str(), buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr, cwd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
#else
    const std::string pid = std::to_string((long)getpid());
    const std::string run = "python3 \"$0\" \"$1\" \"$2\"";
    struct Term { const char* exe; const char* flag; };
    static const Term terms[] = {
        { "x-terminal-emulator", "-e" }, { "gnome-terminal", "--" },
        { "konsole", "-e" }, { "xfce4-terminal", "-x" }, { "xterm", "-e" },
    };
    for (const Term& term : terms) {
        std::vector<std::string> args = { term.exe, term.flag, "bash", "-c", run,
                                          script.string(), root.string(), pid };
        std::vector<char*> argv;
        for (std::string& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t child = 0;
        if (posix_spawnp(&child, term.exe, nullptr, nullptr, argv.data(), environ) == 0)
            return true;
    }
    /* No terminal: run it without a window (its output still goes to
     * setup-log.txt; on failure it goes on to the full rebuild by itself). */
    std::vector<std::string> args = { "python3", script.string(), root.string(), pid };
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t child = 0;
    return posix_spawnp(&child, "python3", nullptr, nullptr, argv.data(), environ) == 0;
#endif
}

/* Does this kit file feed the game C made from the disc? Only those need the
 * game regenerated; everything else (the launcher, the hooks in src/, the build
 * files, the runtime UI, docs, mods) is compiled, so a rebuild picks it up.
 * Anything not listed as compile-only counts as a generator input, so a file
 * nobody thought of errs on the side of the full rebuild. */
bool compile_only(const std::string& rel) {
    for (const char* d : { "launcher/", "cmake/", "src/", "assets/", "mods/", "docs/",
                           "recomp-ui/", "scripts/", "android/" })
        if (starts_with(rel, d)) return true;
    for (const char* f : { "CMakeLists.txt", "README.md", "README-SETUP.txt", "VERSION",
                           "psx_game_version.txt", kSetupExe,
                           "Build Android App.bat", "build-android-app.sh" })
        if (rel == f) return true;
    /* Libraries beside the setup exe. */
    if (rel.find('/') == std::string::npos &&
        (ends_with_ci(rel, ".dll") || ends_with_ci(rel, ".so")))
        return true;
    return false;
}

bool needs_regenerate(const std::vector<std::string>& changed) {
    for (const std::string& rel : changed)
        if (!compile_only(rel)) return true;
    return false;
}

bool apply_kit(const fs::path& zip, const fs::path& root, std::string& err, bool* quick) {
    *quick = false;
    std::error_code ec;
    sweep_old(root);
    const fs::path stage = root / "Update" / "stage";
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);
    if (!unpack(zip, stage) ||
        !fs::is_regular_file(stage / kSetupExe, ec) ||
        !fs::is_regular_file(stage / "game.toml", ec) ||
        !fs::is_regular_file(stage / "psxrecomp" / "psxrecomp_cli.py", ec)) {
        fs::remove_all(root / "Update", ec);
        err = "THE UPDATE COULD NOT BE UNPACKED";
        return false;
    }
    struct Move { fs::path dest, old; bool had_old; };
    std::vector<Move> done;
    std::vector<std::string> changed;
    bool ok = true;
    for (fs::recursive_directory_iterator it(stage, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string rel = it->path().lexically_relative(stage).generic_u8string();
        if (protected_path(rel)) continue;
        const fs::path dest = root / fs::u8path(rel);
        if (keep_if_present(rel) && fs::exists(dest, ec)) continue;
        if (fs::exists(dest, ec) && same_content(it->path(), dest)) continue;
        fs::create_directories(dest.parent_path(), ec);
        Move mv{ dest, fs::path(dest.u8string() + kOldSuffix), false };
        if (fs::exists(dest, ec)) {
            fs::remove(mv.old, ec);
            fs::rename(dest, mv.old, ec);
            if (ec) { ok = false; break; }
            mv.had_old = true;
        }
        fs::rename(it->path(), dest, ec);
        if (ec) {
            if (mv.had_old) fs::rename(mv.old, dest, ec);
            ok = false;
            break;
        }
        done.push_back(mv);
        changed.push_back(rel);
    }
    if (!ok) {   /* put every file back as it was */
        for (auto i = done.rbegin(); i != done.rend(); ++i) {
            fs::remove(i->dest, ec);
            if (i->had_old) fs::rename(i->old, i->dest, ec);
        }
        fs::remove_all(root / "Update", ec);
        err = "A FILE WAS IN USE. NOTHING WAS CHANGED";
        return false;
    }
    /* The old copies are only needed to undo a failed install. One that is
     * still in use stays and is swept by the next update. The new files get
     * the current time: they keep the zip's times when unpacked, which can be
     * older than the player's last build, and the build would then think they
     * are already compiled. */
    const auto now = fs::file_time_type::clock::now();
    for (const Move& mv : done) {
        std::error_code r;
        if (mv.had_old) fs::remove(mv.old, r);
        fs::last_write_time(mv.dest, now, r);
    }
#ifndef _WIN32
    chmod((root / kSetupExe).c_str(), 0755);
#endif
    fs::remove_all(root / "Update", ec);
    /* Game C made from the disc is still right when no generator input
     * changed: keep it and rebuild only (restart_into_rebuild). Otherwise
     * remove it; the setup exe regenerates it when it is missing. */
    if (!needs_regenerate(changed) && fs::is_directory(root / "generated", ec) &&
        quick_rebuild_possible(root)) {
        *quick = true;
    } else {
        fs::remove_all(root / "generated", ec);
    }
    return true;
}

/* ---- the two jobs ------------------------------------------------------------ */

void do_check() {
    const fs::path out = scratch_dir() / "releases.json";
    std::error_code ec;
    fs::remove(out, ec);
    const std::string api = std::string("https://api.github.com/repos/") + kRepo + "/releases?per_page=15";
    const int rc = run_curl(curl_args(api, out, true));
    if (rc != 0) { set(State::Failed, curl_failure(rc)); return; }
    const std::vector<Release> rels = parse_releases(read_all(out));
    fs::remove(out, ec);
    const Release* best = nullptr;
    Asset pick, sums;
    for (const Release& r : rels) {
        if (r.draft || r.pre || r.tag.empty() || r.tag[0] != 'v') continue;
        Asset a, s;
        for (const Asset& x : r.assets) {
            if (starts_with(x.name, kZipPrefix) && ends_with_ci(x.name, kAssetSuffix)) a = x;
            if (x.name == kSums) s = x;
        }
        if (a.url.empty() || (a.digest.empty() && s.url.empty())) continue;
        if (!best || compare(r.tag, best->tag) > 0) { best = &r; pick = a; sums = s; }
    }
    if (!best) { set(State::Failed, "NO RELEASE FOR THIS SYSTEM WAS FOUND"); return; }
    const std::string latest = best->tag.substr(1);
    std::lock_guard<std::mutex> l(g_mu);
    g_snap.latest = latest;
    g_asset = pick; g_sums = sums;
    g_snap.total = pick.size;
    const std::string cur = CH_VERSION;
    if (cur == "dev" || compare(latest, cur) > 0) {
        g_snap.state = State::Available;
        g_snap.message = "VERSION " + latest + " IS AVAILABLE";
    } else {
        g_snap.state = State::UpToDate;
        g_snap.message = "YOU HAVE THE LATEST VERSION";
    }
}

void do_apply() {
    std::string why;
    if (!supported(&why)) { set(State::Failed, why); return; }
    Asset asset, sums;
    std::string latest;
    { std::lock_guard<std::mutex> l(g_mu); asset = g_asset; sums = g_sums; latest = g_snap.latest; }
    if (!safe_url(asset.url) || (!sums.url.empty() && !safe_url(sums.url))) {
        set(State::Failed, "THE RELEASE POINTS SOMEWHERE UNEXPECTED");
        return;
    }
    std::error_code ec;
    const fs::path root = kit_root();
    const fs::path dir = root / "Update";
    const fs::path file = dir / asset.name;
    fs::create_directories(dir, ec);
    { std::lock_guard<std::mutex> l(g_mu); g_snap.state = State::Downloading; g_snap.message = "DOWNLOADING"; g_snap.done = 0; g_snap.total = asset.size; }

    std::string want = asset.digest;
    if (want.empty()) {
        const fs::path sumfile = dir / kSums;
        fs::remove(sumfile, ec);
        const int rc = run_curl(curl_args(sums.url, sumfile, false));
        if (rc != 0) { fs::remove_all(dir, ec); set(State::Failed, curl_failure(rc)); return; }
        want = listed_hash(read_all(sumfile), asset.name);
        fs::remove(sumfile, ec);
    }
    if (want.size() != 64) { fs::remove_all(dir, ec); set(State::Failed, "THE RELEASE HAS NO CHECKSUM FOR THIS FILE"); return; }

    fs::remove(file, ec);
    std::atomic<bool> finished{false};
    std::thread meter([&] {
        while (!finished) {
            std::error_code e;
            const uint64_t n = fs::exists(file, e) ? (uint64_t)fs::file_size(file, e) : 0;
            { std::lock_guard<std::mutex> l(g_mu); if (g_snap.state == State::Downloading) g_snap.done = n; }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });
    const int rc = run_curl(curl_args(asset.url, file, false));
    finished = true;
    meter.join();
    if (rc != 0) { fs::remove_all(dir, ec); set(State::Failed, curl_failure(rc)); return; }

    set(State::Applying, "CHECKING THE DOWNLOAD");
    if (sha256_file(file) != want) {
        fs::remove_all(dir, ec);
        set(State::Failed, "THE DOWNLOAD DID NOT MATCH ITS CHECKSUM. NOTHING WAS CHANGED");
        return;
    }
    set(State::Applying, "INSTALLING");
    std::string err;
    /* apply_kit unpacks into Update/stage beside the zip, then clears Update/. */
    bool quick = false;
    const bool ok = apply_kit(file, root, err, &quick);
    fs::remove_all(dir, ec);
    { std::lock_guard<std::mutex> l(g_mu); g_quick = ok && quick; }
    if (ok && quick)
        set(State::Done, "VERSION " + latest + " IS INSTALLED. RESTART FOR A QUICK REBUILD (YOUR GAME CODE IS KEPT)");
    else if (ok)
        set(State::Done, "VERSION " + latest + " IS INSTALLED. RESTART TO REBUILD THE GAME WITH IT");
    else
        set(State::Failed, err);
}

void start(void (*job)()) {
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return;
    std::thread([job] { job(); g_busy = false; }).detach();
}

} /* namespace */

const char* current_version() { return CH_VERSION; }

bool supported(std::string* why) {
    const char* reason = nullptr;
#if !defined(_WIN32) && !defined(__linux__)
    reason = "UPDATING IS NOT AVAILABLE ON THIS SYSTEM YET";
#else
    const fs::path root = kit_root();
    if (root.empty()) reason = "UPDATING NEEDS THE SETUP KIT FOLDER (UNZIPPED FROM A RELEASE)";
    else if (is_dev_checkout(root)) reason = "THIS IS A DEVELOPER CHECKOUT. UPDATE IT WITH GIT";
#endif
    if (why) *why = reason ? reason : "";
    return reason == nullptr;
}

Snapshot snapshot() {
    std::lock_guard<std::mutex> l(g_mu);
    Snapshot s = g_snap;
    s.current = CH_VERSION;
    return s;
}

void check() {
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (g_busy) return;
        g_snap.state = State::Checking;
        g_snap.message = "LOOKING FOR UPDATES";
    }
    start(do_check);
}

void download_and_apply() {
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (g_busy || g_snap.state != State::Available) return;
        g_snap.state = State::Downloading;      /* at once, so a second press finds it busy */
        g_snap.message = "DOWNLOADING";
        g_snap.done = 0;
    }
    start(do_apply);
}

bool restart_into_rebuild() {
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (g_snap.state != State::Done) return false;
    }
    const fs::path root = kit_root();
    if (root.empty()) return false;
    std::error_code ec;
    bool quick;
    { std::lock_guard<std::mutex> l(g_mu); quick = g_quick; }
    if (quick) {
        if (start_quick_rebuild(root)) return true;
        /* Could not open it: fall back to the full rebuild. */
        fs::remove_all(root / "generated", ec);
    }
    const fs::path exe = root / kSetupExe;
    if (!fs::is_regular_file(exe, ec)) return false;
#ifdef _WIN32
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    std::memset(&si, 0, sizeof si); si.cb = sizeof si;
    std::memset(&pi, 0, sizeof pi);
    const std::wstring cwd = root.wstring();
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, cwd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
#else
    std::string path = exe.string();
    char* argv[] = { path.data(), nullptr };
    pid_t pid = 0;
    return posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv, environ) == 0;
#endif
}

/* ---- Build Android App ----------------------------------------------------- */

#ifdef _WIN32
const char* const kAndroidLauncher = "Build Android App.bat";
#else
const char* const kAndroidLauncher = "build-android-app.sh";
#endif

bool android_builder_available() {
    const fs::path root = kit_root();
    std::error_code ec;
    return !root.empty() &&
           fs::is_regular_file(root / "scripts" / "android" / "build_android_app.py", ec) &&
           fs::is_regular_file(root / kAndroidLauncher, ec);
}

bool start_android_build(std::string* why) {
    const fs::path root = kit_root();
    std::error_code ec;
    const fs::path script = root / kAndroidLauncher;
    if (root.empty() || !fs::is_regular_file(script, ec)) {
        if (why) *why = "THE ANDROID BUILDER IS NOT IN THIS VERSION";
        return false;
    }
#ifdef _WIN32
    /* A console of its own, so the player sees the progress and the .bat's
     * closing "press any key". cmd /c ""path"" keeps spaces in the path. */
    std::wstring cmd = L"cmd.exe /c \"\"" + script.wstring() + L"\"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    std::memset(&si, 0, sizeof si); si.cb = sizeof si;
    std::memset(&pi, 0, sizeof pi);
    const std::wstring cwd = root.wstring();
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr, cwd.c_str(), &si, &pi)) {
        if (why) *why = "COULD NOT OPEN THE BUILD WINDOW";
        return false;
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
#else
    /* Linux: the first terminal program found; it waits for Enter at the end
     * so the messages stay readable. */
    const std::string sh = script.string();
    const std::string run =
        "cd \"$(dirname \"$0\")\" && bash \"$0\"; echo; read -r -p 'Press Enter to close. ' _";
    struct Term { const char* exe; const char* flag; };
    static const Term terms[] = {
        { "x-terminal-emulator", "-e" }, { "gnome-terminal", "--" },
        { "konsole", "-e" }, { "xfce4-terminal", "-x" }, { "xterm", "-e" },
    };
    for (const Term& t : terms) {
        std::vector<std::string> args = { t.exe, t.flag, "bash", "-c", run, sh };
        std::vector<char*> argv;
        for (std::string& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t pid = 0;
        if (posix_spawnp(&pid, t.exe, nullptr, nullptr, argv.data(), environ) == 0)
            return true;
    }
    if (why) *why = "NO TERMINAL FOUND: RUN build-android-app.sh";
    return false;
#endif
}

} /* namespace ch_update */
