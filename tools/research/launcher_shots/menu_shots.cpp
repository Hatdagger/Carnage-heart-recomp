// Draws the Carnage Heart launcher menu pages (launcher/ch_menu.inc) with a
// stand-in launcher model, rasterises ImGui's triangles in software and writes
// PNGs (no window or GPU needed). It also drives the ONLINE CAMPAIGN pages
// with key presses against a local relay, so creating, joining and starting a
// game go through the real menu code. Run with run.sh; the default ImGui font
// stands in for the launcher's own.
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "ch_update.h"
#include "ch_online.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

enum { LNG_VIEW_DASHBOARD, LNG_VIEW_SETTINGS, LNG_VIEW_MODS, LNG_VIEW_NETPLAY_MODE };
enum { LNG_ACTION_NONE, LNG_ACTION_LAUNCH, LNG_ACTION_QUIT };
struct Settings { int volume = 80; bool skip_launcher = false; };
struct LauncherModel {
    bool netplay_supported = true, in_session = false, setup_wizard_supported = false,
         prepare_required_before_continue = false, rom_present = true;
    char rom_full[64] = "Carnage Heart (USA).cue";
    bool capturing = false, hk_capturing = false, camera_capturing = false, setup_wizard_open = false;
    int view = LNG_VIEW_DASHBOARD, action = LNG_ACTION_NONE;
    Settings s;
};
struct LauncherTheme {};
static int g_launches = 0;
static bool launcher_model_bios_blocks_play(LauncherModel*) { return false; }
static void launcher_model_bios_play_prompt(LauncherModel*) {}
static bool launcher_model_can_launch(LauncherModel*) { return true; }
static bool launcher_model_prepare_rom_patch(LauncherModel*) { return true; }
static void launcher_model_set_view(LauncherModel* m, int v) { m->view = v; }
static void request_mod_commit_launch(LauncherModel* m) { m->action = LNG_ACTION_LAUNCH; g_launches++; }
static const char* launcher_model_player_src_label(LauncherModel*, int) { return "Keyboard"; }
static unsigned launcher_model_memcard_blocks_used(LauncherModel*, int) { return 5; }
static const char* launcher_model_aspect_label(LauncherModel*) { return "4:3"; }
static void launcher_model_cycle_aspect(LauncherModel*) {}
static const char* launcher_model_fullscreen_label(LauncherModel*) { return "Off"; }
static void launcher_model_cycle_fullscreen(LauncherModel*) {}
static const char* launcher_model_window_size_label(LauncherModel*) { return "2x"; }
static void launcher_model_cycle_window_size(LauncherModel*) {}
static bool launcher_model_internal_resolution_offered(LauncherModel*) { return true; }
static int launcher_model_internal_resolution_count(LauncherModel*) { return 3; }
static int launcher_model_internal_resolution_index(LauncherModel*) { return 0; }
static void launcher_model_set_internal_resolution(LauncherModel*, int) {}
static const char* launcher_model_internal_resolution_label(LauncherModel*) { return "1x"; }
static const char* launcher_model_texture_filter_label(LauncherModel*) { return "Off"; }
static void launcher_model_toggle_texture_filter(LauncherModel*) {}
static const char* launcher_model_vsync_label(LauncherModel*) { return "On"; }
static void launcher_model_cycle_vsync(LauncherModel*) {}
static void launcher_model_volume_delta(LauncherModel*, int) {}
static void launcher_model_request_skip_toggle(LauncherModel*) {}
static void launcher_model_open_config(LauncherModel*, int) {}
static bool launcher_model_can_restore_defaults(LauncherModel*) { return true; }
static void launcher_model_request_restore_defaults(LauncherModel*) {}

namespace {
#include "ch_menu.inc"
}

/* ---- ch_update stand-in --------------------------------------------------- */
namespace ch_update {
Snapshot snapshot() { Snapshot s; s.current = "0.1.1"; return s; }
bool supported(std::string*) { return true; }
void check() {}
void download_and_apply() {}
bool restart_into_rebuild() { return false; }
const char* current_version() { return "0.1.1"; }
}

/* ---- software rasteriser -------------------------------------------------- */
static const int W = 960, H = 720;
static std::vector<float> fb(W * H * 3);
static unsigned char* g_font; static int g_fw, g_fh;

static void raster(ImDrawData* dd) {
    for (int i = 0; i < W * H; ++i) { fb[i*3] = fb[i*3+1] = fb[i*3+2] = 0; }
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* cl = dd->CmdLists[n];
        for (const ImDrawCmd& cmd : cl->CmdBuffer) {
            if (cmd.UserCallback) continue;
            const int cx0 = std::max(0, (int)cmd.ClipRect.x), cy0 = std::max(0, (int)cmd.ClipRect.y);
            const int cx1 = std::min(W, (int)cmd.ClipRect.z), cy1 = std::min(H, (int)cmd.ClipRect.w);
            for (unsigned e = 0; e < cmd.ElemCount; e += 3) {
                const ImDrawVert* v[3];
                for (int k = 0; k < 3; ++k) v[k] = &cl->VtxBuffer[cl->IdxBuffer[cmd.IdxOffset + e + k] + cmd.VtxOffset];
                float minx = std::min({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}), maxx = std::max({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x});
                float miny = std::min({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y}), maxy = std::max({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y});
                int x0 = std::max(cx0, (int)std::floor(minx)), x1 = std::min(cx1, (int)std::ceil(maxx));
                int y0 = std::max(cy0, (int)std::floor(miny)), y1 = std::min(cy1, (int)std::ceil(maxy));
                float area = (v[1]->pos.x - v[0]->pos.x) * (v[2]->pos.y - v[0]->pos.y) - (v[2]->pos.x - v[0]->pos.x) * (v[1]->pos.y - v[0]->pos.y);
                if (std::fabs(area) < 1e-6f) continue;
                for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
                    float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((v[1]->pos.x - px) * (v[2]->pos.y - py) - (v[2]->pos.x - px) * (v[1]->pos.y - py)) / area;
                    float w1 = ((v[2]->pos.x - px) * (v[0]->pos.y - py) - (v[0]->pos.x - px) * (v[2]->pos.y - py)) / area;
                    float w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x;
                    float t = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
                    int tx = std::min(g_fw - 1, std::max(0, (int)(u * g_fw))), ty = std::min(g_fh - 1, std::max(0, (int)(t * g_fh)));
                    const unsigned char* tex = g_font + (ty * g_fw + tx) * 4;
                    float col[4];
                    for (int c = 0; c < 4; ++c) {
                        float a = ((v[0]->col >> (8*c)) & 255) * w0 + ((v[1]->col >> (8*c)) & 255) * w1 + ((v[2]->col >> (8*c)) & 255) * w2;
                        col[c] = a / 255.0f * tex[c] / 255.0f;
                    }
                    float* p = &fb[(y * W + x) * 3];
                    for (int c = 0; c < 3; ++c) p[c] = p[c] * (1 - col[3]) + col[c] * col[3];
                }
            }
        }
    }
}

static void save(const char* name) {
    std::vector<unsigned char> img(W * H * 3);
    for (int i = 0; i < W * H * 3; ++i) img[i] = (unsigned char)std::min(255.0f, fb[i] * 255.0f + 0.5f);
    char path[256]; std::snprintf(path, sizeof path, "shots/%s.png", name);
    stbi_write_png(path, W, H, 3, img.data(), W * 3);
    std::printf("wrote %s\n", path);
}

static LauncherModel M;
static void frame(std::initializer_list<ImGuiKey> keys = {}, bool shot = false, const char* name = nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DeltaTime = 1.0f / 60.0f;
    for (ImGuiKey k : keys) io.AddKeyEvent(k, true);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("x", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    LauncherTheme th;
    if (ch_menu::owns_view(&M)) ch_menu::draw(&M, th);
    ImGui::End();
    ImGui::Render();
    if (shot) { raster(ImGui::GetDrawData()); save(name); }
    ImGui::NewFrame(); ImGui::EndFrame();       /* key up */
    for (ImGuiKey k : keys) io.AddKeyEvent(k, false);
}
static void press(ImGuiKey k) { frame({k}); frame(); }
static void idle(int ms) {
    for (int t = 0; t < ms; t += 50) { frame(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    while (ch_online::snapshot().busy) { frame(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    frame();
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "A";
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fc; fc.SizePixels = 26.0f; fc.OversampleH = fc.OversampleV = 1;
    io.Fonts->AddFontDefault(&fc);
    io.Fonts->GetTexDataAsRGBA32(&g_font, &g_fw, &g_fh);
    if (mode == "A") {
        frame({}, true, "1-main");
        press(ImGuiKey_DownArrow);
        press(ImGuiKey_Enter);                  /* ONLINE PLAY */
        press(ImGuiKey_DownArrow);
        frame({}, true, "2-online-play");
        press(ImGuiKey_Enter);                  /* ONLINE CAMPAIGN */
        idle(300);
        frame({}, true, "3-campaign-empty");
        press(ImGuiKey_Enter);                  /* NEW GAME */
        idle(500);
        frame({}, true, "4-new-game");
        std::FILE* f = std::fopen("code.txt", "w"); std::fputs(ch_online::snapshot().just_made.c_str(), f); std::fclose(f);
        press(ImGuiKey_Enter);                  /* PLAY */
        std::printf("launches=%d action=%d CH_TURN_GAME=%s CH_TURN_ROLE=%s\n", g_launches, M.action,
                    getenv("CH_TURN_GAME"), getenv("CH_TURN_ROLE"));
    } else if (mode == "B") {
        char code[16] = {0}; std::FILE* f = std::fopen("../a/code.txt", "r"); std::fgets(code, sizeof code, f); std::fclose(f);
        press(ImGuiKey_DownArrow); press(ImGuiKey_Enter); press(ImGuiKey_DownArrow); press(ImGuiKey_Enter);
        idle(200);
        press(ImGuiKey_DownArrow); press(ImGuiKey_Enter);   /* JOIN A GAME */
        frame({}, true, "5-join-empty");
        /* Type the code on the keyboard, but change the 3rd letter with the D-pad. */
        for (const char* p = code; *p; ++p) {
            if (*p == '-') continue;
            ImGuiKey k = std::isdigit((unsigned char)*p) ? (ImGuiKey)(ImGuiKey_0 + (*p - '0')) : (ImGuiKey)(ImGuiKey_A + (*p - 'A'));
            press(k);
        }
        press(ImGuiKey_LeftArrow); press(ImGuiKey_LeftArrow); press(ImGuiKey_LeftArrow);
        press(ImGuiKey_LeftArrow); press(ImGuiKey_LeftArrow);
        press(ImGuiKey_UpArrow); press(ImGuiKey_DownArrow);
        frame({}, true, "6-join-typed");
        press(ImGuiKey_Enter);
        idle(800);
        frame({}, true, "7-joined-game");
        press(ImGuiKey_Escape);
        frame({}, true, "8-campaign-list");
        press(ImGuiKey_Escape); press(ImGuiKey_Escape);
        frame({}, true, "9-main-B-not-your-turn");
    } else if (mode == "A2") {
        idle(800);                              /* the launcher's check on opening */
        frame({}, true, "10-main-A-your-turn");
        press(ImGuiKey_DownArrow); press(ImGuiKey_Enter); press(ImGuiKey_DownArrow); press(ImGuiKey_Enter);
        idle(300);
        press(ImGuiKey_Enter);                  /* the game */
        press(ImGuiKey_DownArrow); press(ImGuiKey_DownArrow);
        frame({}, true, "11-forget");
    } else if (mode == "A3") {
        /* run.sh put a version-2 turn file 1 (Ganymede map 2) in the game's
         * folder: the game page shows that scenario, and the launcher sends
         * the file to the relay as one the game could not send. */
        idle(800);
        press(ImGuiKey_DownArrow); press(ImGuiKey_Enter); press(ImGuiKey_DownArrow); press(ImGuiKey_Enter);
        idle(300);
        frame({}, true, "12-list-scenario");
        press(ImGuiKey_Enter);                  /* the game */
        idle(300);
        frame({}, true, "13-game-scenario");
        const ch_online::Snapshot s = ch_online::snapshot();
        if (!s.games.empty())
            std::printf("scenario=%d map=%d label=%s status=%s\n", s.games[0].scenario, s.games[0].map,
                        ch_online::scenario_label(s.games[0]).c_str(),
                        ch_online::status_label(s.games[0].status));
    }
    return 0;
}
