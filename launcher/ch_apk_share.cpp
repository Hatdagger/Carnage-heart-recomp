/* Carnage Heart: send the Android app to a tablet over the home Wi-Fi (see
 * ch_apk_share.h). A very small web server: GET and HEAD only, one page and
 * a fixed list of files, all under one random code. */
#include "ch_apk_share.h"
#include "ch_update.h"

extern "C" {
#include "third_party/qrcodegen/qrcodegen.h"
}

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET sock_t;
static const sock_t kNoSock = INVALID_SOCKET;
static void close_sock(sock_t s) { closesocket(s); }
#  define CH_SEND_FLAGS 0
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
typedef int sock_t;
static const sock_t kNoSock = -1;
static void close_sock(sock_t s) { close(s); }
#  ifdef MSG_NOSIGNAL
#    define CH_SEND_FLAGS MSG_NOSIGNAL
#  else
#    define CH_SEND_FLAGS 0
#  endif
#endif

namespace fs = std::filesystem;

namespace ch_apk_share {
namespace {

/* Ports tried in turn; the first free one is used. */
const int kFirstPort = 47810, kPortCount = 20;
const int kMaxClients = 8;

struct File {
    std::string label;     /* shown on the page */
    std::string name;      /* file name the tablet saves */
    fs::path    path;
    uint64_t    size = 0;
    const char* type = "application/octet-stream";
};

struct Server {
    std::atomic<bool> stop{false};
    std::atomic<int>  clients{0};
    sock_t            listener = kNoSock;
    std::thread       acceptor;
    std::string       token;
    File              apk;
    std::vector<File> disc;    /* .cue first, then its track files */

    std::mutex mu;             /* guards st */
    Status     st;
};

std::mutex g_mu;               /* guards g_srv */
std::shared_ptr<Server> g_srv;
Status g_last;                 /* after stop() or a failed start() */

bool net_init() {
#ifdef _WIN32
    static bool ok = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
    return ok;
#else
    return true;
#endif
}

bool private_v4(uint32_t a) {     /* host byte order */
    return (a >> 24) == 10 || (a >> 20) == ((172u << 4) | 1u) || (a >> 16) == ((192u << 8) | 168u);
}

std::string v4_text(uint32_t a) {
    char b[32];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", (a >> 24) & 255u, (a >> 16) & 255u,
                  (a >> 8) & 255u, a & 255u);
    return b;
}

/* This PC's address on the home network. A UDP "connect" picks the network
 * the PC would use to reach the internet (no packet is sent); when that
 * fails (no internet), the first private address of this computer. */
std::string lan_address() {
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s != kNoSock) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
        uint32_t got = 0;
        if (connect(s, (const sockaddr*)&to, sizeof to) == 0) {
            sockaddr_in me{};
            socklen_t n = sizeof me;
            if (getsockname(s, (sockaddr*)&me, &n) == 0) got = ntohl(me.sin_addr.s_addr);
        }
        close_sock(s);
        if (got && (got >> 24) != 127) return v4_text(got);
    }
    char host[256] = {0};
    if (gethostname(host, sizeof host - 1) != 0) return "";
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return "";
    std::string best;
    for (addrinfo* r = res; r; r = r->ai_next) {
        const uint32_t a = ntohl(((sockaddr_in*)r->ai_addr)->sin_addr.s_addr);
        if ((a >> 24) == 127) continue;
        if (private_v4(a)) { best = v4_text(a); break; }
        if (best.empty()) best = v4_text(a);
    }
    freeaddrinfo(res);
    return best;
}

std::string random_code() {
    static const char k[] = "abcdefghjkmnpqrstuvwxyz23456789";
    std::random_device rd;
    std::mt19937_64 r(((uint64_t)rd() << 32) ^ rd() ^
                      (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
    std::string s;
    for (int i = 0; i < 10; ++i) s += k[r() % (sizeof k - 1)];
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

/* The .cue and the files its FILE lines name (beside it). */
std::vector<File> disc_files(const std::string& cue) {
    std::vector<File> out;
    std::error_code ec;
    const fs::path cp = fs::u8path(cue);
    if (cue.empty() || !fs::is_regular_file(cp, ec)) return out;
    File c;
    c.path = cp;
    c.name = cp.filename().u8string();
    c.size = fs::file_size(cp, ec);
    c.label = "Disc sheet (.cue)";
    out.push_back(c);
    std::ifstream f(cp);
    std::string line;
    int track = 0;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.size() < 5) continue;
        std::string head = line.substr(0, 5);
        for (char& ch : head) ch = (char)std::toupper((unsigned char)ch);
        if (head != "FILE ") continue;
        std::string rest = trim(line.substr(5)), name;
        if (!rest.empty() && rest[0] == '"') {
            const size_t q = rest.find('"', 1);
            if (q == std::string::npos) continue;
            name = rest.substr(1, q - 1);
        } else {
            name = rest.substr(0, rest.find(' '));
        }
        const fs::path p = cp.parent_path() / fs::u8path(name);
        if (name.empty() || !fs::is_regular_file(p, ec)) continue;
        File t;
        t.path = p;
        t.name = p.filename().u8string();
        t.size = fs::file_size(p, ec);
        char lb[48];
        std::snprintf(lb, sizeof lb, "Disc track %d", ++track);
        t.label = lb;
        out.push_back(t);
    }
    if (out.size() < 2) out.clear();      /* a sheet without its tracks is no use */
    return out;
}

std::string html_escape(const std::string& s) {
    std::string o;
    for (char ch : s) {
        switch (ch) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += ch;
        }
    }
    return o;
}

std::string mb(uint64_t n) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f MB", n / 1048576.0);
    return b;
}

std::string page_html(const Server& s) {
    std::string h =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Carnage Heart for Android</title><style>"
        "body{background:#050a08;color:#b8d6c4;font:17px/1.45 sans-serif;margin:0;padding:20px;max-width:640px}"
        "h1{color:#def0e4;font-size:24px;margin:0 0 4px}h2{color:#f2b028;font-size:18px;margin:26px 0 8px}"
        "p{margin:8px 0}.dim{color:#608670;font-size:14px}"
        "a.b{display:block;background:#f2b028;color:#06110a;text-decoration:none;font-weight:bold;"
        "padding:16px;border-radius:6px;margin:10px 0;text-align:center}"
        "a.f{display:block;border:1px solid #2c583e;color:#faf6de;text-decoration:none;"
        "padding:12px;border-radius:6px;margin:8px 0}a.f span{float:right;color:#608670}"
        "ol{padding-left:22px}li{margin:6px 0}</style></head><body>"
        "<h1>Carnage Heart</h1><p class=\"dim\">Sent from the launcher on your PC.</p>"
        "<h2>1. Get the app</h2>"
        "<a class=\"b\" href=\"app.apk\">Download the app (" + mb(s.apk.size) + ")</a>"
        "<ol><li>If the browser says the file might be harmful, choose <b>Download anyway</b>.</li>"
        "<li>Open the downloaded <b>CarnageHeart.apk</b> and tap <b>Install</b>. "
        "If asked, allow this browser to install unknown apps.</li>"
        "<li>If Play Protect warns, tap <b>More details</b>, then <b>Install anyway</b>.</li>"
        "<li>Already installed? Installing again updates it and keeps your memory cards.</li></ol>";
    if (!s.disc.empty()) {
        h += "<h2>2. Get your disc files</h2>"
             "<p>The app plays from your disc image, so it needs these too. "
             "Skip this if they are already on the tablet.</p>";
        for (size_t i = 0; i < s.disc.size(); ++i)
            h += "<a class=\"f\" href=\"disc/" + std::to_string(i) + "\">" +
                 html_escape(s.disc[i].label) + "<span>" + mb(s.disc[i].size) +
                 "</span><br><small>" + html_escape(s.disc[i].name) + "</small></a>";
        h += "<p>Tap each one once and wait for it to finish. In the app, tap "
             "<b>CHOOSE DISC FILES</b> and pick all of them together (they are in Downloads).</p>";
    }
    h += "<p class=\"dim\">This page works only while the QR code is on the PC screen. "
         "Your files stay on your home network.</p></body></html>";
    return h;
}

/* ---- connections ------------------------------------------------------------- */

bool send_all(sock_t c, const char* p, size_t n, const Server& s) {
    while (n) {
        if (s.stop) return false;
        const int k = (int)std::min<size_t>(n, 1 << 16);
        const int w = (int)send(c, p, k, CH_SEND_FLAGS);
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

bool send_text(sock_t c, const Server& s, const char* status, const char* type,
               const std::string& body, bool head, const char* extra = "") {
    std::string r = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type +
                    "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nConnection: close\r\n" + extra + "\r\n";
    if (!head) r += body;
    return send_all(c, r.data(), r.size(), s);
}

/* "bytes=N-" or "bytes=N-M": the first byte asked for, or 0. */
uint64_t range_start(const std::string& req, uint64_t size, uint64_t* end) {
    *end = size ? size - 1 : 0;
    std::string low = req;
    for (char& ch : low) ch = (char)std::tolower((unsigned char)ch);
    const size_t at = low.find("\nrange:");
    if (at == std::string::npos) return 0;
    const size_t b = low.find("bytes=", at);
    if (b == std::string::npos) return 0;
    const char* p = low.c_str() + b + 6;
    char* q = nullptr;
    const unsigned long long a = std::strtoull(p, &q, 10);
    if (q == p || *q != '-') return 0;
    const char* e = q + 1;
    if (std::isdigit((unsigned char)*e)) {
        const unsigned long long z = std::strtoull(e, nullptr, 10);
        if (z < *end) *end = z;
    }
    return a < size && a <= *end ? a : 0;
}

void send_file(sock_t c, Server& s, const File& f, const std::string& req, bool head,
               const std::string& client) {
    std::ifstream in(f.path, std::ios::binary);
    std::error_code ec;
    const uint64_t size = fs::file_size(f.path, ec);
    if (!in || ec) { send_text(c, s, "404 Not Found", "text/plain", "Not found\n", head); return; }
    uint64_t last = 0;
    const uint64_t first = range_start(req, size, &last);
    const bool partial = first > 0 || (size && last != size - 1);
    const uint64_t count = size ? last - first + 1 : 0;
    std::string safe;
    for (char ch : f.name) safe += (ch == '"' || ch == '\\' || (unsigned char)ch < 32) ? '_' : ch;
    std::string r = std::string("HTTP/1.1 ") + (partial ? "206 Partial Content" : "200 OK") +
                    "\r\nContent-Type: " + f.type +
                    "\r\nContent-Length: " + std::to_string(count) +
                    "\r\nContent-Disposition: attachment; filename=\"" + safe + "\"" +
                    "\r\nAccept-Ranges: bytes\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if (partial)
        r += "Content-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) +
             "/" + std::to_string(size) + "\r\n";
    r += "\r\n";
    if (!send_all(c, r.data(), r.size(), s) || head) return;
    {
        std::lock_guard<std::mutex> l(s.mu);
        s.st.last_file = f.name;
        s.st.last_client = client;
        s.st.sending_done = first;
        s.st.sending_total = size;
    }
    in.seekg((std::streamoff)first);
    std::vector<char> buf(1 << 16);
    uint64_t left = count, sent = first;
    bool ok = true;
    while (left && ok) {
        in.read(buf.data(), (std::streamsize)std::min<uint64_t>(left, buf.size()));
        const std::streamsize n = in.gcount();
        if (n <= 0) { ok = false; break; }
        ok = send_all(c, buf.data(), (size_t)n, s);
        left -= (uint64_t)n;
        sent += (uint64_t)n;
        std::lock_guard<std::mutex> l(s.mu);
        if (s.st.last_file == f.name) s.st.sending_done = sent;
    }
    std::lock_guard<std::mutex> l(s.mu);
    if (ok && !left && sent == size) ++s.st.downloads;
    if (s.st.last_file == f.name) s.st.sending_total = 0;
}

void serve(sock_t c, std::shared_ptr<Server> s, std::string client) {
#ifdef _WIN32
    DWORD tv = 10000;
#else
    timeval tv{10, 0};
#endif
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
        const int n = (int)recv(c, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, (size_t)n);
    }
    const size_t sp1 = req.find(' ');
    const size_t sp2 = sp1 == std::string::npos ? sp1 : req.find(' ', sp1 + 1);
    if (sp2 != std::string::npos) {
        const std::string method = req.substr(0, sp1);
        std::string path = req.substr(sp1 + 1, sp2 - sp1 - 1);
        const size_t q = path.find('?');
        if (q != std::string::npos) path.resize(q);
        const bool head = method == "HEAD";
        const std::string base = "/" + s->token;
        if (method != "GET" && !head) {
            send_text(c, *s, "405 Method Not Allowed", "text/plain", "GET only\n", false);
        } else if (path == base) {
            send_text(c, *s, "302 Found", "text/plain", "", head,
                      ("Location: " + base + "/\r\n").c_str());
        } else if (path == base + "/") {
            send_text(c, *s, "200 OK", "text/html; charset=utf-8", page_html(*s), head);
        } else if (path == base + "/app.apk") {
            send_file(c, *s, s->apk, req, head, client);
        } else if (path.compare(0, base.size() + 6, base + "/disc/") == 0) {
            const std::string n = path.substr(base.size() + 6);
            const bool digits = !n.empty() && n.size() < 3 &&
                std::all_of(n.begin(), n.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
            const size_t i = digits ? (size_t)std::stoul(n) : (size_t)-1;
            if (i < s->disc.size()) send_file(c, *s, s->disc[i], req, head, client);
            else send_text(c, *s, "404 Not Found", "text/plain", "Not found\n", head);
        } else {
            send_text(c, *s, "404 Not Found", "text/plain",
                      "Not found. Open the QR code on the PC again.\n", head);
        }
    }
    close_sock(c);
    --s->clients;
}

void accept_loop(std::shared_ptr<Server> s) {
    while (!s->stop) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s->listener, &rd);
        timeval tv{0, 200000};
        const int r = select((int)s->listener + 1, &rd, nullptr, nullptr, &tv);
        if (r <= 0 || s->stop) continue;
        sockaddr_in from{};
        socklen_t n = sizeof from;
        const sock_t c = accept(s->listener, (sockaddr*)&from, &n);
        if (c == kNoSock) continue;
        if (s->clients >= kMaxClients) { close_sock(c); continue; }
        ++s->clients;
        std::thread(serve, c, s, v4_text(ntohl(from.sin_addr.s_addr))).detach();
    }
}

} /* namespace */

bool apk_info(std::string* path, uint64_t* size, int64_t* mtime) {
    const std::string root = ch_update::kit_folder();
    if (root.empty()) return false;
    std::error_code ec;
    const fs::path p = fs::u8path(root) / "CarnageHeart.apk";
    if (!fs::is_regular_file(p, ec)) return false;
    const uint64_t sz = fs::file_size(p, ec);
    if (ec || sz == 0) return false;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return false;
    if (path) *path = p.u8string();
    if (size) *size = sz;
    if (mtime) {
        /* file_time_type's clock is not system_clock everywhere; convert
         * through "now" on both clocks. */
        const auto now_f = fs::file_time_type::clock::now();
        const auto now_s = std::chrono::system_clock::now();
        const auto st = now_s + std::chrono::duration_cast<std::chrono::system_clock::duration>(t - now_f);
        *mtime = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(st.time_since_epoch()).count();
    }
    return true;
}

bool start(const std::string& cue) {
    stop();
    Status fail;
    auto give_up = [&](const char* why) {
        fail.error = why;
        std::lock_guard<std::mutex> l(g_mu);
        g_last = fail;
        return false;
    };
    std::string apk;
    uint64_t apk_size = 0;
    if (!apk_info(&apk, &apk_size, nullptr)) return give_up("NO CarnageHeart.apk YET: BUILD THE ANDROID APP FIRST");
    if (!net_init()) return give_up("THE NETWORK COULD NOT BE STARTED");
    const std::string ip = lan_address();
    if (ip.empty()) return give_up("THIS PC IS NOT ON A NETWORK. CONNECT IT TO YOUR HOME WI-FI OR ROUTER");

    auto s = std::make_shared<Server>();
    s->token = random_code();
    s->apk.path = fs::u8path(apk);
    s->apk.name = "CarnageHeart.apk";
    s->apk.size = apk_size;
    s->apk.label = "Carnage Heart app";
    s->apk.type = "application/vnd.android.package-archive";
    s->disc = disc_files(cue);

    int port = 0;
    for (int i = 0; i < kPortCount && !port; ++i) {
        sock_t l = socket(AF_INET, SOCK_STREAM, 0);
        if (l == kNoSock) break;
#ifndef _WIN32
        int one = 1;
        setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((uint16_t)(kFirstPort + i));
        if (bind(l, (const sockaddr*)&a, sizeof a) == 0 && listen(l, 8) == 0) {
            s->listener = l;
            port = kFirstPort + i;
        } else {
            close_sock(l);
        }
    }
    if (!port) return give_up("NO FREE NETWORK PORT (47810-47829) ON THIS PC");

    s->st.running = true;
    s->st.url = "http://" + ip + ":" + std::to_string(port) + "/" + s->token + "/";
    s->acceptor = std::thread(accept_loop, s);
    std::lock_guard<std::mutex> l(g_mu);
    g_srv = s;
    return true;
}

void stop() {
    std::shared_ptr<Server> s;
    {
        std::lock_guard<std::mutex> l(g_mu);
        s.swap(g_srv);
    }
    if (!s) return;
    s->stop = true;
    if (s->acceptor.joinable()) s->acceptor.join();
    close_sock(s->listener);
    /* Transfers still going see stop and end within one chunk; they hold
     * their own reference to the server. */
    std::lock_guard<std::mutex> l(g_mu);
    g_last = Status();
}

Status status() {
    std::shared_ptr<Server> s;
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (!g_srv) return g_last;
        s = g_srv;
    }
    std::lock_guard<std::mutex> l(s->mu);
    return s->st;
}

int qr(const std::string& text, std::vector<uint8_t>* modules) {
    std::vector<uint8_t> tmp(qrcodegen_BUFFER_LEN_MAX), out(qrcodegen_BUFFER_LEN_MAX);
    if (!qrcodegen_encodeText(text.c_str(), tmp.data(), out.data(), qrcodegen_Ecc_MEDIUM,
                              qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true))
        return 0;
    const int n = qrcodegen_getSize(out.data());
    modules->assign((size_t)n * n, 0);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            (*modules)[(size_t)y * n + x] = qrcodegen_getModule(out.data(), x, y) ? 1 : 0;
    return n;
}

} /* namespace ch_apk_share */
