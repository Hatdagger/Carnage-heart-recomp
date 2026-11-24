/* Carnage Heart launcher menu: LAUNCH GAME, ONLINE PLAY, SHARE DATA, UPDATE,
 * SETTINGS.
 *
 * Included into recomp-ui's launcher_imgui.cpp by cmake/ch_launcher_menu.cmake
 * (a build-time copy of that file with a two-line hook; the recomp-ui
 * submodule itself is not changed). It owns the launcher's dashboard and
 * settings views. Everything else (the controller page, the full settings
 * page, the first-run setup wizard, every modal) is the stock launcher,
 * reached from here and returned from with its own Back.
 *
 * Built from the Master of Arena system menu (Hatdagger/AC1Recomp,
 * recomp-ui/src/common/backends/imgui/moa_menu.inc).
 *
 * Input. Every function works with any one of these alone:
 *   controller   D-pad / left stick move, A confirm, B back, START = launch
 *   keyboard     arrows move, Enter / Space confirm, Esc / Backspace back
 *   2-button mouse  point at a row, LEFT click = confirm, RIGHT click = back
 *                   (the wheel also moves, and ADJUST rows take a click on
 *                   their < > arrows, but neither is ever required)
 * No page needs typed text.
 *
 * Expected from the including file: LauncherModel, LauncherTheme,
 * request_mod_commit_launch().
 */

/* ch_update.h is included at the top of the file (see the cmake module). */

namespace ch_menu {

enum Page { PAGE_MAIN = 0, PAGE_ONLINE, PAGE_SHARE, PAGE_UPDATE, PAGE_SETTINGS, PAGE__COUNT };

struct State {
    int    cursor[PAGE__COUNT] = {0};
    int    top[PAGE__COUNT] = {0};
    Page   page = PAGE_MAIN;             /* which dashboard sub-page is up */
    bool   classic_settings = false;     /* the stock settings page was asked for */
    int    last_view = -1;
    bool   blocked = false;          /* a dialog or capture owns input this frame */
    char   note[200] = {0};
    double note_until = 0.0;
};
static State g_state;

/* Design grid. */
static const float kW = 640.0f, kH = 480.0f;
static const float kRowH = 26.0f;

/* Carnage Heart palette: dark field, green circuit lines, amber cursor. */
static const ImU32 kBg      = IM_COL32(5, 10, 8, 255);
static const ImU32 kGrid    = IM_COL32(24, 52, 36, 110);
static const ImU32 kPanel   = IM_COL32(9, 20, 15, 225);
static const ImU32 kLine    = IM_COL32(44, 88, 62, 255);
static const ImU32 kText    = IM_COL32(184, 214, 196, 255);
static const ImU32 kBright  = IM_COL32(250, 246, 222, 255);
static const ImU32 kDim     = IM_COL32(96, 134, 112, 255);
static const ImU32 kHi      = IM_COL32(242, 176, 40, 255);
static const ImU32 kOn      = IM_COL32(100, 226, 140, 255);
static const ImU32 kOff     = IM_COL32(58, 86, 72, 255);
static const ImU32 kWarn    = IM_COL32(228, 92, 60, 255);
static const ImU32 kOnText  = IM_COL32(6, 17, 10, 255);

struct Gfx {
    ImDrawList* dl;
    ImFont*     font;
    float       s;      /* window pixels per design unit */
    ImVec2      o;      /* window position of design (0,0) */
};

static ImVec2 at(const Gfx& g, float x, float y) {
    return ImVec2(g.o.x + x * g.s, g.o.y + y * g.s);
}

static float text_width(const Gfx& g, float size, const char* s) {
    return g.font->CalcTextSizeA(size * g.s, FLT_MAX, 0.0f, s).x / g.s;
}

static void text(const Gfx& g, float x, float y, float size, ImU32 color,
                 const char* s, float wrap = 0.0f) {
    g.dl->AddText(g.font, size * g.s, at(g, x, y), color, s, nullptr,
                  wrap > 0.0f ? wrap * g.s : 0.0f);
}

static void text_right(const Gfx& g, float right, float y, float size,
                       ImU32 color, const char* s) {
    text(g, right - text_width(g, size, s), y, size, color, s);
}

static std::string upper(const char* s) {
    std::string out(s ? s : "");
    for (char& c : out) c = (char)std::toupper((unsigned char)c);
    return out;
}

static bool same_word(const char* a, const char* b) {
    if (!a || !b) return false;
    for (; *a && *b; ++a, ++b)
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

static bool mouse_moved() {
    const ImVec2 d = ImGui::GetIO().MouseDelta;
    return d.x != 0.0f || d.y != 0.0f;
}

static void panel(const Gfx& g, float x, float y, float w, float h,
                  const char* caption) {
    g.dl->AddRectFilled(at(g, x, y), at(g, x + w, y + h), kPanel);
    g.dl->AddRect(at(g, x, y), at(g, x + w, y + h), kLine);
    if (caption && caption[0]) {
        const float cw = text_width(g, 10.0f, caption);
        g.dl->AddRectFilled(at(g, x + 10.0f, y - 6.0f),
                            at(g, x + 22.0f + cw, y + 6.0f), kBg);
        text(g, x + 16.0f, y - 6.0f, 10.0f, kDim, caption);
    }
}

static void header(const Gfx& g, const char* title, const char* sub) {
    text(g, 24.0f, 22.0f, 19.0f, IM_COL32(222, 240, 228, 255), title);
    text_right(g, kW - 24.0f, 32.0f, 10.0f, kDim, sub);
    g.dl->AddLine(at(g, 24.0f, 52.0f), at(g, kW - 24.0f, 52.0f), kLine);
}

static void note(const char* s) {
    std::snprintf(g_state.note, sizeof g_state.note, "%s", s ? s : "");
    g_state.note_until = ImGui::GetTime() + 4.0;
}

/* Footer: control hints, plus a BACK button for the mouse on sub-pages.
 * Returns true when that button was clicked. */
static bool footer(const Gfx& g, const char* hints, bool back_button) {
    g.dl->AddLine(at(g, 24.0f, kH - 36.0f), at(g, kW - 24.0f, kH - 36.0f), kLine);
    float x = 24.0f;
    bool clicked = false;
    if (back_button) {
        const char* label = "< BACK";
        const float w = text_width(g, 11.0f, label) + 16.0f;
        const ImVec2 a = at(g, x, kH - 31.0f), b = at(g, x + w, kH - 13.0f);
        const bool hov = !g_state.blocked && ImGui::IsMouseHoveringRect(a, b, false);
        g.dl->AddRect(a, b, hov ? kHi : kLine);
        text(g, x + 8.0f, kH - 28.0f, 11.0f, hov ? kHi : kText, label);
        clicked = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        x += w + 12.0f;
    }
    text(g, x, kH - 28.0f, 10.0f, kDim, hints);
    if (g_state.note[0] && ImGui::GetTime() < g_state.note_until)
        text_right(g, kW - 24.0f, kH - 28.0f, 10.0f, kHi, g_state.note);
    return clicked;
}

static void lamp(const Gfx& g, float right, float y, bool on, const char* label) {
    const float w = 38.0f, h = 16.0f;
    const ImVec2 a = at(g, right - w, y), b = at(g, right, y + h);
    if (on) g.dl->AddRectFilled(a, b, kOn);
    g.dl->AddRect(a, b, on ? kOn : kOff);
    const float tw = text_width(g, 11.0f, label);
    text(g, right - w + (w - tw) * 0.5f, y + 2.0f, 11.0f, on ? kOnText : kOff, label);
}

/* What a mouse did to one row. */
struct RowHit {
    bool hovered = false;
    bool click = false;      /* left click on the row */
    int  arrow = 0;          /* -1 / +1: left click on the < or > of a value */
};

/* One selectable row. value: shown on the right; "< X >" values get
 * clickable arrows. */
static RowHit row(const Gfx& g, float x, float y, float w, bool selected,
                  bool enabled, const char* label, const char* value,
                  bool is_toggle, bool toggle_on) {
    RowHit hit;
    const ImVec2 a = at(g, x, y), b = at(g, x + w, y + kRowH);
    hit.hovered = !g_state.blocked && ImGui::IsMouseHoveringRect(a, b, false);
    if (selected) {
        g.dl->AddRectFilledMultiColor(a, b, IM_COL32(242, 176, 40, 70),
                                      IM_COL32(242, 176, 40, 0),
                                      IM_COL32(242, 176, 40, 0),
                                      IM_COL32(242, 176, 40, 70));
        g.dl->AddTriangleFilled(at(g, x + 12.0f, y + 8.0f),
                                at(g, x + 21.0f, y + 13.0f),
                                at(g, x + 12.0f, y + 18.0f), kHi);
    }
    const ImU32 color = !enabled ? kOff : selected ? kBright : kText;
    text(g, x + 30.0f, y + 6.0f, 13.0f, color, label);
    const float right = x + w - 14.0f;
    if (is_toggle) {
        lamp(g, right, y + 5.0f, toggle_on && enabled,
             !enabled ? "N/A" : toggle_on ? "ON" : "OFF");
    } else if (value && value[0]) {
        text_right(g, right, y + 6.0f, 13.0f, enabled ? kHi : kOff, value);
        /* "< X >": the left and right thirds of the value are its arrows. */
        const size_t n = std::strlen(value);
        if (enabled && n >= 4 && value[0] == '<' && value[n - 1] == '>') {
            const float vw = text_width(g, 13.0f, value);
            const ImVec2 la = at(g, right - vw - 4.0f, y), lb = at(g, right - vw + 14.0f, y + kRowH);
            const ImVec2 ra = at(g, right - 14.0f, y), rb = at(g, right + 6.0f, y + kRowH);
            if (!g_state.blocked && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (ImGui::IsMouseHoveringRect(la, lb, false)) hit.arrow = -1;
                else if (ImGui::IsMouseHoveringRect(ra, rb, false)) hit.arrow = 1;
            }
        }
    }
    hit.click = hit.hovered && enabled && hit.arrow == 0 &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    return hit;
}

/* ---- input ---------------------------------------------------------------- */

struct Input {
    bool up, down, left, right, confirm, back, start;
};

static bool key(ImGuiKey k, bool repeat) { return ImGui::IsKeyPressed(k, repeat); }

static Input read_input(const LauncherModel* m) {
    Input in = {};
    /* A modal owns the screen (keys AND mouse: a click on the dialog must not
     * reach a row drawn under it), a text field owns the keys, and a rebind
     * capture owns the pad. */
    g_state.blocked =
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                    ImGuiPopupFlags_AnyPopupLevel) ||
        ImGui::GetIO().WantTextInput || m->capturing || m->hk_capturing ||
        m->camera_capturing || m->setup_wizard_open;
    if (g_state.blocked)
        return in;
    in.up    = key(ImGuiKey_UpArrow, true) || key(ImGuiKey_GamepadDpadUp, true) ||
               key(ImGuiKey_GamepadLStickUp, true);
    in.down  = key(ImGuiKey_DownArrow, true) || key(ImGuiKey_GamepadDpadDown, true) ||
               key(ImGuiKey_GamepadLStickDown, true);
    in.left  = key(ImGuiKey_LeftArrow, true) || key(ImGuiKey_GamepadDpadLeft, true) ||
               key(ImGuiKey_GamepadLStickLeft, true);
    in.right = key(ImGuiKey_RightArrow, true) || key(ImGuiKey_GamepadDpadRight, true) ||
               key(ImGuiKey_GamepadLStickRight, true);
    in.confirm = key(ImGuiKey_Enter, false) || key(ImGuiKey_KeypadEnter, false) ||
                 key(ImGuiKey_Space, false) || key(ImGuiKey_GamepadFaceDown, false);
    in.back  = key(ImGuiKey_Escape, false) || key(ImGuiKey_Backspace, false) ||
               key(ImGuiKey_GamepadFaceRight, false) ||
               ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    in.start = key(ImGuiKey_GamepadStart, false);
    /* The wheel moves the cursor too (never needed: pointing does it). */
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel > 0.0f) in.up = true;
    if (wheel < 0.0f) in.down = true;
    return in;
}

static void move_cursor(Page page, int count, const Input& in, int visible) {
    int& c = g_state.cursor[page];
    int& t = g_state.top[page];
    if (count <= 0) { c = t = 0; return; }
    if (in.up)   c = (c + count - 1) % count;
    if (in.down) c = (c + 1) % count;
    if (c < 0) c = 0;
    if (c >= count) c = count - 1;
    if (c < t) t = c;
    if (c >= t + visible) t = c - visible + 1;
    if (t > count - visible) t = count - visible;
    if (t < 0) t = 0;
}

static void open_page(Page p) {
    g_state.page = p;
    g_state.cursor[p] = 0;
    g_state.top[p] = 0;
    g_state.note[0] = '\0';
}

/* ---- launching ------------------------------------------------------------- */

/* Not built yet: this is the setup exe and the game still has to be made from
 * the player's disc (the wizard's Generate & rebuild). */
static bool needs_install(const LauncherModel* m) {
    return m->setup_wizard_supported && m->prepare_required_before_continue;
}

static void launch_game(LauncherModel* m) {
    if (needs_install(m)) {            /* the first-run wizard does the install */
        m->setup_wizard_open = true;
        return;
    }
    /* The stock PLAY button's decisions, in the same order. */
    if (launcher_model_bios_blocks_play(m)) {
        launcher_model_bios_play_prompt(m);
    } else if (!launcher_model_can_launch(m)) {
        if (m->setup_wizard_supported) m->setup_wizard_open = true;
        else note("SELECT A DISC IMAGE FIRST");
    } else if (!launcher_model_prepare_rom_patch(m)) {
        launcher_model_set_view(m, LNG_VIEW_MODS);
    } else {
        request_mod_commit_launch(m);
    }
}

/* ---- simple list pages ------------------------------------------------------- */

struct Item {
    const char* label;
    const char* value;     /* right-hand text, e.g. "SOON" */
    bool        enabled;
    const char* info;
};

/* A page of rows on the left and the selected row's details on the right.
 * Returns the activated row, or -1. */
static int list_page(const Gfx& g, const Input& in, Page page, const char* title,
                     const char* sub, const Item* items, int count,
                     const char* side_caption, bool* back_clicked) {
    move_cursor(page, count, in, count);
    int& cur = g_state.cursor[page];
    header(g, title, sub);
    panel(g, 24.0f, 76.0f, 270.0f, 340.0f, "SELECT");
    int activate = -1;
    for (int i = 0; i < count; ++i) {
        const RowHit h = row(g, 25.0f, 90.0f + kRowH * i, 268.0f, i == cur,
                             items[i].enabled, items[i].label, items[i].value,
                             false, false);
        if (h.hovered && mouse_moved()) cur = i;
        if (h.click) activate = i;
    }
    if (in.confirm && items[cur].enabled) activate = cur;
    panel(g, 310.0f, 76.0f, 306.0f, 340.0f, side_caption);
    text(g, 324.0f, 92.0f, 11.0f, kHi, items[cur].label);
    text(g, 324.0f, 114.0f, 11.0f, kText, items[cur].info, 278.0f);
    *back_clicked = footer(g, "A / ENTER / LEFT CLICK  SELECT      B / ESC / RIGHT CLICK  BACK", true);
    return activate;
}

/* ---- ONLINE PLAY ----------------------------------------------------------------- */

static void draw_online_page(LauncherModel* m, const Gfx& g, const Input& in) {
    const bool net = m->netplay_supported;
    const Item items[] = {
        { "VERSUS", net ? "" : "SOON", net,
          "Two players, two OKE teams, one battle: over your own network (LAN) or "
          "online through a room. The host is player 1, the guest is player 2 "
          "and brings their own memory card." },
        { "ONLINE CAMPAIGN", "SOON", false,
          "Both players share one campaign map and take turns. On your turn you "
          "build, program and deploy OKEs; then the other player is told it is "
          "their turn. Battles are fought by the programs you wrote." },
    };
    bool back = false;
    const int act = list_page(g, in, PAGE_ONLINE, "ONLINE PLAY", "VERSUS AND CAMPAIGN",
                              items, 2, "ABOUT", &back);
    if (in.back || back) { open_page(PAGE_MAIN); return; }
    if (act == 0 && net) launcher_model_set_view(m, LNG_VIEW_NETPLAY_MODE);
}

/* ---- SHARE DATA ------------------------------------------------------------------ */

static void draw_share_page(LauncherModel*, const Gfx& g, const Input& in) {
    const Item items[] = {
        { "BROWSE SHARED DATA", "SOON", false,
          "See OKE designs and chip programs other players have shared, look at "
          "them, and copy one onto your memory card." },
        { "SHARE MY DATA", "SOON", false,
          "Pick a save from your memory card and share it, without opening "
          "GitHub yourself." },
    };
    bool back = false;
    list_page(g, in, PAGE_SHARE, "SHARE DATA", "TRADE OKES AND PROGRAMS",
              items, 2, "ABOUT", &back);
    if (in.back || back) open_page(PAGE_MAIN);
}

/* ---- UPDATE ---------------------------------------------------------------------- */

static bool update_live(const ch_update::Snapshot& s) {
    using ch_update::State;
    return s.state == State::Available || s.state == State::Downloading ||
           s.state == State::Applying;
}

static void draw_update_page(LauncherModel* m, const Gfx& g, const Input& in) {
    using ch_update::State;
    const ch_update::Snapshot s = ch_update::snapshot();
    const bool busy = s.state == State::Checking || s.state == State::Downloading ||
                      s.state == State::Applying;
    std::string why;
    const bool can = ch_update::supported(&why);
    const bool can_apply = can && s.state == State::Available;
    const bool done = s.state == State::Done;

    const int count = 2;
    move_cursor(PAGE_UPDATE, count, in, count);
    int& cur = g_state.cursor[PAGE_UPDATE];
    header(g, "UPDATE", "NOTHING IS DOWNLOADED UNTIL YOU ASK");
    panel(g, 24.0f, 76.0f, 592.0f, 64.0f, "SELECT");
    const char* second = done ? "RESTART AND REBUILD" : "DOWNLOAD AND INSTALL";
    const bool second_ok = done || (can_apply && !busy);
    const RowHit h0 = row(g, 25.0f, 90.0f, 590.0f, cur == 0, !busy && !done,
                          "CHECK FOR UPDATE", "", false, false);
    const RowHit h1 = row(g, 25.0f, 90.0f + kRowH, 590.0f, cur == 1, second_ok,
                          second, "", false, false);
    if (h0.hovered && mouse_moved()) cur = 0;
    if (h1.hovered && mouse_moved()) cur = 1;

    panel(g, 24.0f, 156.0f, 592.0f, 260.0f, "VERSION");
    text(g, 38.0f, 172.0f, 11.0f, kDim, "INSTALLED");
    text(g, 170.0f, 172.0f, 11.0f, kText, s.current == "dev" ? "DEVELOPMENT BUILD" : s.current.c_str());
    text(g, 38.0f, 191.0f, 11.0f, kDim, "NEWEST");
    text(g, 170.0f, 191.0f, 11.0f, s.latest.empty() ? kDim : kText,
         s.latest.empty() ? "NOT CHECKED YET" : s.latest.c_str());
    if (!s.message.empty()) {
        const ImU32 c = s.state == State::Failed ? kWarn
                      : s.state == State::Available ? kHi
                      : (s.state == State::UpToDate || done) ? kOn : kText;
        text(g, 38.0f, 214.0f, 11.0f, c, s.message.c_str(), 560.0f);
    }
    if (s.state == State::Downloading) {
        const float w = 540.0f;
        const float f = s.total ? std::min(1.0f, (float)((double)s.done / (double)s.total)) : 0.0f;
        g.dl->AddRect(at(g, 38.0f, 236.0f), at(g, 38.0f + w, 248.0f), kLine);
        g.dl->AddRectFilled(at(g, 39.0f, 237.0f), at(g, 39.0f + (w - 2.0f) * f, 247.0f), kHi);
        char b[80];
        std::snprintf(b, sizeof b, "%.1f / %.1f MB", s.done / 1048576.0, s.total / 1048576.0);
        text(g, 38.0f, 253.0f, 10.0f, kDim, b);
    } else if (s.state == State::Applying) {
        text(g, 38.0f, 236.0f, 10.0f, kWarn, "DO NOT CLOSE THE LAUNCHER WHILE THIS RUNS.");
    }
    if (!can) text(g, 38.0f, 270.0f, 11.0f, kWarn, why.c_str(), 560.0f);
    text(g, 38.0f, 296.0f, 11.0f, kText,
         "An update downloads the new setup kit from this game's GitHub releases "
         "and replaces the kit's own files. Your disc image, memory cards, "
         "settings and installed mods are not touched. After installing, the "
         "game is rebuilt from your disc once, the same way it was the first "
         "time, which takes a few minutes. The internet is used only when you "
         "press a button here.", 560.0f);
    const bool back = footer(g, "A / ENTER / LEFT CLICK  SELECT      B / ESC / RIGHT CLICK  BACK", true);

    if ((in.back || back) && !busy) { open_page(PAGE_MAIN); return; }
    if (busy) return;
    int act = -1;
    if (h0.click) act = 0;
    if (h1.click) act = 1;
    if (in.confirm) act = cur;
    if (act < 0) return;
    cur = act;
    if (act == 0 && !done) {
        ch_update::check();
    } else if (act == 1 && done) {
        if (ch_update::restart_into_rebuild()) m->action = LNG_ACTION_QUIT;
        else note("CLOSE THE LAUNCHER AND START CARNAGEHEART_RECOMPILED AGAIN");
    } else if (act == 1 && can_apply) {
        ch_update::download_and_apply();
    }
}

/* ---- main page ------------------------------------------------------------- */

enum MainEntry { MAIN_LAUNCH = 0, MAIN_ONLINE, MAIN_SHARE, MAIN_UPDATE, MAIN_ANDROID, MAIN_SETTINGS, MAIN_EXIT };

/* Result of the last BUILD ANDROID APP click, shown under its info text. */
static std::string g_android_msg;
static bool g_android_ok = false;

static void draw_main(LauncherModel* m, const Gfx& g, const Input& in) {
    struct Entry { MainEntry id; const char* label; const char* info; };
    Entry entries[8];
    int count = 0;
    const bool install = needs_install(m);
    if (m->in_session)
        entries[count++] = { MAIN_LAUNCH, "RESUME GAME", "Return to the game in progress." };
    else if (install)
        entries[count++] = { MAIN_LAUNCH, "INSTALL GAME",
            "First time only: finds your Carnage Heart (USA) disc image, turns it "
            "into a PC game and builds it on this computer. This takes a few "
            "minutes and needs the internet once, for the compiler pack." };
    else
        entries[count++] = { MAIN_LAUNCH, "LAUNCH GAME",
            "Start Carnage Heart with the settings chosen here." };
    if (!m->in_session) {
        entries[count++] = { MAIN_ONLINE, "ONLINE PLAY",
            "Versus against another player over your network or online, and a "
            "shared campaign map where you take turns." };
        entries[count++] = { MAIN_SHARE, "SHARE DATA",
            "Trade OKE designs and chip programs with other players." };
        entries[count++] = { MAIN_UPDATE, "UPDATE",
            "Look for a newer version and install it when you choose. Your disc, "
            "saves and settings are not touched." };
        if (!install && ch_update::android_builder_available())
            entries[count++] = { MAIN_ANDROID, "BUILD ANDROID APP",
                "Make the Android version of your installed game (CarnageHeart.apk) "
                "for your own phone or tablet. A window opens and shows the "
                "progress. The first time it downloads the Android tools once "
                "(large; needs the internet). When it says Finished, copy "
                "CarnageHeart.apk from the game folder to your device." };
    }
    entries[count++] = { MAIN_SETTINGS, "SETTINGS",
        "Display, sound, controller and mouse." };
    entries[count++] = { MAIN_EXIT, m->in_session ? "QUIT GAME" : "EXIT",
        m->in_session ? "Close the game." : "Close the launcher." };

    move_cursor(PAGE_MAIN, count, in, count);
    int& cur = g_state.cursor[PAGE_MAIN];

    header(g, "CARNAGE HEART", "RECOMPILED");
    panel(g, 24.0f, 76.0f, 270.0f, 340.0f, "SELECT");
    int activate = -1;
    const ch_update::Snapshot upd = ch_update::snapshot();
    for (int i = 0; i < count; ++i) {
        const char* badge = nullptr;
        if (entries[i].id == MAIN_UPDATE && update_live(upd)) badge = "NEW";
        if (entries[i].id == MAIN_UPDATE && upd.state == ch_update::State::Done) badge = "RESTART";
        const RowHit h = row(g, 25.0f, 90.0f + kRowH * i, 268.0f, i == cur, true,
                             entries[i].label, badge, false, false);
        if (h.hovered && mouse_moved()) cur = i;
        if (h.click) activate = i;
    }
    if (in.confirm) activate = cur;
    if (in.start) activate = 0;

    /* Status. */
    panel(g, 310.0f, 76.0f, 306.0f, 150.0f, "STATUS");
    {
        float y = 92.0f;
        char buf[160];
        const bool disc = m->rom_present && m->rom_full[0];
        text(g, 324.0f, y, 11.0f, kDim, "DISC");
        text(g, 424.0f, y, 11.0f, disc ? kOn : kWarn, disc ? "FOUND" : "NOT FOUND");
        y += 19.0f;
        text(g, 324.0f, y, 11.0f, kDim, "GAME");
        text(g, 424.0f, y, 11.0f, install ? kWarn : kOn, install ? "NOT INSTALLED" : "INSTALLED");
        y += 19.0f;
        text(g, 324.0f, y, 11.0f, kDim, "CONTROLLER");
        std::snprintf(buf, sizeof buf, "%s",
                      upper(launcher_model_player_src_label(m, 0)).c_str());
        text(g, 424.0f, y, 11.0f, kText, buf);
        y += 19.0f;
        text(g, 324.0f, y, 11.0f, kDim, "MEMORY CARD");
        std::snprintf(buf, sizeof buf, "%u / 15 BLOCKS",
                      (unsigned)launcher_model_memcard_blocks_used(m, 0));
        text(g, 424.0f, y, 11.0f, kText, buf);
        y += 19.0f;
        text(g, 324.0f, y, 11.0f, kDim, "VERSION");
        if (update_live(upd))
            std::snprintf(buf, sizeof buf, "%s  (v%s AVAILABLE)", upd.current.c_str(), upd.latest.c_str());
        else if (upd.state == ch_update::State::Done)
            std::snprintf(buf, sizeof buf, "v%s INSTALLED. RESTART", upd.latest.c_str());
        else
            std::snprintf(buf, sizeof buf, "%s", upd.current == "dev" ? "DEVELOPMENT" : upd.current.c_str());
        text(g, 424.0f, y, 11.0f, update_live(upd) || upd.state == ch_update::State::Done ? kHi : kText, buf);
    }

    panel(g, 310.0f, 242.0f, 306.0f, 174.0f, "INFO");
    text(g, 324.0f, 258.0f, 11.0f, kHi, entries[cur].label);
    text(g, 324.0f, 280.0f, 11.0f, kText, entries[cur].info, 278.0f);
    if (entries[cur].id == MAIN_ANDROID && !g_android_msg.empty())
        text(g, 324.0f, 398.0f, 11.0f, g_android_ok ? kOn : kWarn, g_android_msg.c_str(), 278.0f);

    footer(g, "A / ENTER / LEFT CLICK  SELECT      START  LAUNCH", false);

    if (activate < 0) return;
    cur = activate;
    switch (entries[activate].id) {
        case MAIN_LAUNCH:   launch_game(m); break;
        case MAIN_ONLINE:   open_page(PAGE_ONLINE); break;
        case MAIN_SHARE:    open_page(PAGE_SHARE); break;
        case MAIN_UPDATE:   open_page(PAGE_UPDATE); break;
        case MAIN_ANDROID: {
            std::string why;
            g_android_ok = ch_update::start_android_build(&why);
            g_android_msg = g_android_ok ? "BUILD WINDOW OPENED" : why;
            break;
        }
        case MAIN_SETTINGS: launcher_model_set_view(m, LNG_VIEW_SETTINGS); break;
        case MAIN_EXIT:     m->action = LNG_ACTION_QUIT; break;
    }
}

/* ---- settings page ---------------------------------------------------------- */

struct Setting {
    const char* label;
    const char* detail;
    bool        (*offered)(LauncherModel*);
    const char* (*value)(LauncherModel*);
    /* dir: 0 confirm, -1 left, +1 right. */
    void        (*step)(LauncherModel*, int dir);
};

static bool always(LauncherModel*) { return true; }

static const char* volume_value(LauncherModel* m) {
    static char buf[16];
    std::snprintf(buf, sizeof buf, "%d", m->s.volume);
    return buf;
}

static void internal_resolution_step(LauncherModel* m, int dir) {
    const int n = launcher_model_internal_resolution_count(m);
    if (n <= 0) return;
    int i = launcher_model_internal_resolution_index(m) + (dir < 0 ? -1 : 1);
    if (i < 0) i = n - 1;
    if (i >= n) i = 0;
    launcher_model_set_internal_resolution(m, i);
}

static const Setting kSettings[] = {
    { "ASPECT", "The shape of the picture. 4:3 is the original; wider shapes "
                "show more of the battlefield.",
      always, [](LauncherModel* m) { return launcher_model_aspect_label(m); },
      [](LauncherModel* m, int) { launcher_model_cycle_aspect(m); } },
    { "FULLSCREEN", "Off, borderless or exclusive fullscreen.",
      always, [](LauncherModel* m) { return launcher_model_fullscreen_label(m); },
      [](LauncherModel* m, int) { launcher_model_cycle_fullscreen(m); } },
    { "WINDOW SIZE", "Size of the window when fullscreen is off.",
      always, [](LauncherModel* m) { return launcher_model_window_size_label(m); },
      [](LauncherModel* m, int) { launcher_model_cycle_window_size(m); } },
    { "INTERNAL RESOLUTION", "How finely the 3D picture is drawn. Higher is "
                             "sharper and needs a faster graphics card.",
      [](LauncherModel* m) { return launcher_model_internal_resolution_offered(m) &&
                                    launcher_model_internal_resolution_count(m) > 0; },
      [](LauncherModel* m) { return launcher_model_internal_resolution_label(m); },
      internal_resolution_step },
    { "TEXTURE FILTER", "Smooths textures. Off keeps the original sharp pixels.",
      always, [](LauncherModel* m) { return launcher_model_texture_filter_label(m); },
      [](LauncherModel* m, int) { launcher_model_toggle_texture_filter(m); } },
    { "VSYNC", "Matches the picture to the display to avoid tearing.",
      always, [](LauncherModel* m) { return launcher_model_vsync_label(m); },
      [](LauncherModel* m, int) { launcher_model_cycle_vsync(m); } },
    { "VOLUME", "Overall sound level.",
      always, volume_value,
      [](LauncherModel* m, int dir) { launcher_model_volume_delta(m, dir < 0 ? -5 : 5); } },
    { "SKIP THIS MENU", "Start the game directly next time, without this menu.",
      always, [](LauncherModel* m) -> const char* { return m->s.skip_launcher ? "On" : "Off"; },
      [](LauncherModel* m, int) { launcher_model_request_skip_toggle(m); } },
    { "CONTROLLER AND MOUSE", "Choose the controller, change which button does "
                              "what, and set up the PlayStation Mouse.",
      always, [](LauncherModel*) -> const char* { return ">"; },
      [](LauncherModel* m, int dir) { if (dir >= 0) launcher_model_open_config(m, 0); } },
    { "ALL SETTINGS", "The full settings page: renderer, filters, hotkeys, mods "
                      "and everything else.",
      always, [](LauncherModel*) -> const char* { return ">"; },
      [](LauncherModel*, int dir) { if (dir >= 0) g_state.classic_settings = true; } },
    { "RESTORE DEFAULTS", "Put every setting back to how it was first installed.",
      [](LauncherModel* m) { return launcher_model_can_restore_defaults(m); },
      [](LauncherModel*) -> const char* { return ">"; },
      [](LauncherModel* m, int dir) { if (dir >= 0) launcher_model_request_restore_defaults(m); } },
};

static void draw_settings_page(LauncherModel* m, const Gfx& g, const Input& in) {
    const Setting* rows[sizeof kSettings / sizeof kSettings[0]];
    int count = 0;
    for (const Setting& s : kSettings)
        if (s.offered(m)) rows[count++] = &s;
    const int visible = 12;
    move_cursor(PAGE_SETTINGS, count, in, visible);
    int& cur = g_state.cursor[PAGE_SETTINGS];
    const int top = g_state.top[PAGE_SETTINGS];

    header(g, "SETTINGS", "DISPLAY / SOUND / CONTROLS");
    panel(g, 24.0f, 76.0f, 340.0f, 340.0f, "SYSTEM");
    int clicked = -1, arrow = 0;
    for (int i = top; i < count && i < top + visible; ++i) {
        const char* v = rows[i]->value(m);
        const bool on = same_word(v, "On") || same_word(v, "Enabled");
        const bool off = same_word(v, "Off") || same_word(v, "Disabled");
        std::string shown;
        if (!on && !off && v) {
            shown = upper(v);
            if (shown != ">") shown = "< " + shown + " >";
        }
        const RowHit h = row(g, 25.0f, 90.0f + kRowH * (i - top), 338.0f, i == cur, true,
                             rows[i]->label, shown.c_str(), on || off, on);
        if (h.hovered && mouse_moved()) cur = i;
        if (h.click) clicked = i;
        if (h.arrow) { clicked = i; arrow = h.arrow; }
    }

    panel(g, 380.0f, 76.0f, 236.0f, 340.0f, "DETAIL");
    if (count > 0) {
        text(g, 394.0f, 92.0f, 11.0f, kHi, rows[cur]->label);
        text(g, 394.0f, 114.0f, 11.0f, kText, rows[cur]->detail, 208.0f);
        char pos[32];
        std::snprintf(pos, sizeof pos, "ITEM %02d / %02d", cur + 1, count);
        text_right(g, 604.0f, 396.0f, 10.0f, kDim, pos);
    }
    const bool back = footer(g, "A / LEFT CLICK  CHANGE      LEFT / RIGHT  ADJUST", true);

    if (in.back || back) { launcher_model_set_view(m, LNG_VIEW_DASHBOARD); return; }
    if (clicked >= 0) { cur = clicked; rows[cur]->step(m, arrow); return; }
    if (count > 0) {
        if (in.confirm)      rows[cur]->step(m, 0);
        else if (in.right)   rows[cur]->step(m, 1);
        else if (in.left)    rows[cur]->step(m, -1);
    }
}

/* ---- entry points -------------------------------------------------------------- */

/* True when this menu, not the stock page, draws the current view. */
static bool owns_view(const LauncherModel* m) {
    if (m->view == LNG_VIEW_DASHBOARD) {
        g_state.classic_settings = false;
        if (m->in_session) g_state.page = PAGE_MAIN;
        return true;
    }
    if (m->view == LNG_VIEW_SETTINGS) return !g_state.classic_settings;
    return false;
}

static void draw(LauncherModel* m, const LauncherTheme&) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    Gfx g;
    g.dl = ImGui::GetWindowDrawList();
    g.font = ImGui::GetFont();
    g.s = std::min(vp->Size.x / kW, vp->Size.y / kH);
    if (g.s <= 0.0f) g.s = 1.0f;
    g.o = ImVec2(vp->Pos.x + (vp->Size.x - kW * g.s) * 0.5f,
                 vp->Pos.y + (vp->Size.y - kH * g.s) * 0.5f);

    /* Ground and circuit grid across the whole window. */
    const ImVec2 a = vp->Pos, b = ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    g.dl->PushClipRect(a, b, false);
    g.dl->AddRectFilled(a, b, kBg);
    const float step = 32.0f * g.s;
    for (float x = std::fmod(g.o.x, step); x < b.x; x += step)
        g.dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), kGrid);
    for (float y = std::fmod(g.o.y, step); y < b.y; y += step)
        g.dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), kGrid);

    const Input in = read_input(m);
    if ((int)m->view != g_state.last_view) {   /* a note belongs to its page */
        g_state.last_view = (int)m->view;
        g_state.note[0] = '\0';
    }
    if (m->view == LNG_VIEW_SETTINGS) {
        draw_settings_page(m, g, in);
    } else {
        switch (g_state.page) {
            case PAGE_ONLINE: draw_online_page(m, g, in); break;
            case PAGE_SHARE:  draw_share_page(m, g, in); break;
            case PAGE_UPDATE: draw_update_page(m, g, in); break;
            default:          draw_main(m, g, in); break;
        }
    }
    g.dl->PopClipRect();
}

} /* namespace ch_menu */
