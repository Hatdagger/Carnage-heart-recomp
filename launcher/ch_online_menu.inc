/* Carnage Heart launcher: ONLINE CAMPAIGN pages (multiplayer plan step 6).
 *
 * Included into ch_menu.inc (inside namespace ch_menu), after the shared row,
 * panel and list helpers. The network side is launcher/ch_online.cpp.
 *
 *   PAGE_CAMPAIGN  the player's online games, NEW GAME, JOIN A GAME
 *   PAGE_CGAME     one game: PLAY, CHECK AGAIN, FORGET THIS GAME
 *   PAGE_JOIN      enter the other player's game code
 *
 * Same rules as the rest of the menu: everything works with a controller, a
 * keyboard or a 2-button mouse alone. The game code is entered with the
 * D-pad (left/right pick a letter, up/down change it); a keyboard can also
 * type it or paste it (Ctrl+V), and a mouse clicks the top or bottom half of
 * a letter to change it.
 */

namespace online_ui {

static std::string g_game;              /* the game PAGE_CGAME shows */
static std::string g_handled;           /* last create/join result acted on */
static bool        g_forget_armed = false;
static double      g_next_check = 0.0;
static const char  kChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const int   kNChars = 32;
static int         g_code[8] = {0};
static int         g_pos = 0;

static const char* kHowTo =
    "Start the game from here with PLAY. Load your own save if you have one "
    "(it brings your OKE designs), then start the scenario shown above. The map "
    "catches up with the other player's newest turn by itself. Give your "
    "orders and press End Turn: your turn is sent, and you can close the "
    "game once the bottom line says it is on the relay. When it is your "
    "turn again, this screen says so.";

static void check_soon() { g_next_check = 0.0; }

static void auto_check() {
    const double now = ImGui::GetTime();
    if (now >= g_next_check) {
        g_next_check = now + 60.0;
        ch_online::refresh();
    }
}

static ImU32 status_color(ch_online::Status s) {
    using ch_online::Status;
    switch (s) {
        case Status::YourTurn: return kHi;
        case Status::Offline: case Status::Gone: return kWarn;
        case Status::GameOver: return kDim;
        default: return kText;
    }
}

static const ch_online::Game* find(const ch_online::Snapshot& s, const std::string& code) {
    for (const ch_online::Game& g : s.games)
        if (g.code == code) return &g;
    return nullptr;
}

/* Result of the last create/join, at the bottom of the right panel. */
static void result_line(const Gfx& g, const ch_online::Snapshot& s, float x, float y, float w) {
    if (s.message.empty()) return;
    text(g, x, y, 11.0f, s.failed ? kWarn : kOn, s.message.c_str(), w);
}

/* After a create or join worked, show that game. */
static void follow_result(const ch_online::Snapshot& s) {
    if (!s.just_made.empty() && s.just_made != g_handled) {
        g_handled = s.just_made;
        g_game = s.just_made;
        g_forget_armed = false;
        open_page(PAGE_CGAME);
    }
}

} /* namespace online_ui */

/* ---- the list of games ------------------------------------------------------ */

static void draw_campaign_page(LauncherModel*, const Gfx& g, const Input& in) {
    using namespace online_ui;
    const ch_online::Snapshot s = ch_online::snapshot();
    std::string why;
    const bool built = ch_online::built_in(&why);
    const std::string relay = ch_online::relay_url();
    const bool can_make = built && !relay.empty() && !s.busy;
    if (built) auto_check();

    const int ngames = (int)s.games.size();
    const int count = ngames + 2;
    const int visible = 12;
    move_cursor(PAGE_CAMPAIGN, count, in, visible);
    int& cur = g_state.cursor[PAGE_CAMPAIGN];
    const int top = g_state.top[PAGE_CAMPAIGN];

    header(g, "ONLINE CAMPAIGN", "TAKE TURNS ON ONE MAP");
    panel(g, 24.0f, 76.0f, 270.0f, 340.0f, "YOUR GAMES");
    int activate = -1;
    for (int i = top; i < count && i < top + visible; ++i) {
        std::string label, value;
        bool enabled = built;
        if (i < ngames) {
            const ch_online::Game& gm = s.games[i];
            label = gm.code + (gm.role == 'A' ? "  (A)" : "  (B)");
            value = ch_online::status_label(gm.status);
        } else if (i == ngames) {
            label = "NEW GAME";
            enabled = can_make;
        } else {
            label = "JOIN A GAME";
            enabled = built && !relay.empty();
        }
        const RowHit h = row(g, 25.0f, 90.0f + kRowH * (i - top), 268.0f, i == cur, enabled,
                             label.c_str(), value.c_str(), false, false);
        if (h.hovered && mouse_moved()) cur = i;
        if (h.click) activate = i;
    }
    if (in.confirm) activate = cur;

    panel(g, 310.0f, 76.0f, 306.0f, 340.0f, "ABOUT");
    const float x = 324.0f, w = 278.0f;
    if (!built) {
        text(g, x, 92.0f, 11.0f, kWarn, "NOT IN THIS BUILD");
        text(g, x, 114.0f, 11.0f, kText, why.c_str(), w);
    } else if (cur < ngames) {
        const ch_online::Game& gm = s.games[cur];
        text(g, x, 92.0f, 11.0f, kHi, ("GAME " + gm.code).c_str());
        text(g, x, 114.0f, 11.0f, kText, gm.role == 'A' ? "YOU ARE PLAYER A (YOU MADE IT)"
                                                       : "YOU ARE PLAYER B (YOU JOINED IT)");
        text(g, x, 136.0f, 11.0f, status_color(gm.status),
             gm.detail.empty() ? "NOT CHECKED YET" : gm.detail.c_str(), w);
        text(g, x, 190.0f, 11.0f, kText, ("START " + ch_online::scenario_label(gm)).c_str(), w);
        text(g, x, 212.0f, 11.0f, kDim, "SELECT IT TO PLAY, CHECK AGAIN OR FORGET IT.", w);
    } else if (cur == ngames) {
        text(g, x, 92.0f, 11.0f, kHi, "NEW GAME");
        text(g, x, 114.0f, 11.0f, kText,
             "Makes a new online campaign. You are player A and play the first "
             "turn. You get a code like K7QX-M2PD: give it to the other player, "
             "who chooses JOIN A GAME and enters it.", w);
    } else {
        text(g, x, 92.0f, 11.0f, kHi, "JOIN A GAME");
        text(g, x, 114.0f, 11.0f, kText,
             "Enter the code the other player gave you. You are player B and "
             "play second.", w);
    }
    if (built && relay.empty())
        text(g, x, 250.0f, 11.0f, kWarn,
             "NO RELAY ADDRESS YET. Put the relay's address (from relay/README.md) "
             "on the first line of online-turns/relay.txt beside the game.", w);
    if (s.busy) text(g, x, 356.0f, 11.0f, kDim, "TALKING TO THE RELAY...");
    result_line(g, s, x, 374.0f, w);
    const bool back = footer(g, "A / ENTER / LEFT CLICK  SELECT      B / ESC / RIGHT CLICK  BACK", true);

    follow_result(s);
    if (in.back || back) { open_page(PAGE_ONLINE); return; }
    if (activate < 0 || !built) return;
    cur = activate;
    if (activate < ngames) {
        g_game = s.games[activate].code;
        g_forget_armed = false;
        open_page(PAGE_CGAME);
    } else if (activate == ngames) {
        if (can_make) ch_online::create_game();
    } else if (!relay.empty()) {
        open_page(PAGE_JOIN);
    }
}

/* ---- one game ----------------------------------------------------------------- */

static void draw_cgame_page(LauncherModel* m, const Gfx& g, const Input& in) {
    using namespace online_ui;
    using ch_online::Status;
    const ch_online::Snapshot s = ch_online::snapshot();
    const ch_online::Game* gm = find(s, g_game);
    if (!gm) { open_page(PAGE_CAMPAIGN); return; }
    auto_check();

    const bool over = gm->status == Status::GameOver || gm->status == Status::Gone;
    const int count = 3;
    move_cursor(PAGE_CGAME, count, in, count);
    int& cur = g_state.cursor[PAGE_CGAME];
    header(g, ("GAME " + gm->code).c_str(), gm->role == 'A' ? "YOU ARE PLAYER A" : "YOU ARE PLAYER B");
    panel(g, 24.0f, 76.0f, 270.0f, 340.0f, "SELECT");
    const char* labels[3] = { gm->status == Status::YourTurn ? "PLAY YOUR TURN" : "PLAY",
                              "CHECK AGAIN",
                              g_forget_armed ? "PRESS AGAIN TO FORGET" : "FORGET THIS GAME" };
    const bool enabled[3] = { !over, !s.busy, true };
    int activate = -1;
    for (int i = 0; i < count; ++i) {
        const RowHit h = row(g, 25.0f, 90.0f + kRowH * i, 268.0f, i == cur, enabled[i],
                             labels[i], "", false, false);
        if (h.hovered && mouse_moved()) cur = i;
        if (h.click) activate = i;
    }
    if (in.confirm && enabled[cur]) activate = cur;

    panel(g, 310.0f, 76.0f, 306.0f, 340.0f, "STATUS");
    const float x = 324.0f, w = 278.0f;
    text(g, x, 90.0f, 10.0f, kDim, "GAME CODE (GIVE IT TO THE OTHER PLAYER)");
    text(g, x, 104.0f, 26.0f, kBright, gm->code.c_str());
    text(g, x, 142.0f, 11.0f, kHi, ("SCENARIO: " + ch_online::scenario_label(*gm)).c_str(), w);
    text(g, x, 162.0f, 11.0f, status_color(gm->status),
         gm->detail.empty() ? "NOT CHECKED YET" : gm->detail.c_str(), w);
    if (cur == 2)
        text(g, x, 214.0f, 11.0f, kWarn,
             "Forgetting removes this game from the list. Your key goes with it, "
             "so you cannot play this game again from this computer. The turn "
             "files stay in the online-turns folder.", w);
    else
        text(g, x, 214.0f, 11.0f, kText, kHowTo, w);
    if (s.busy) text(g, x, 374.0f, 11.0f, kDim, "TALKING TO THE RELAY...");
    else result_line(g, s, x, 374.0f, w);
    const bool back = footer(g, "A / ENTER / LEFT CLICK  SELECT      B / ESC / RIGHT CLICK  BACK", true);

    if (in.back || back) { g_forget_armed = false; open_page(PAGE_CAMPAIGN); return; }
    if (activate != 2 && activate >= 0) g_forget_armed = false;
    if (activate < 0) return;
    cur = activate;
    if (activate == 0) {
        if (ch_online::prepare_launch(gm->code)) launch_game(m);
        else note("THIS GAME COULD NOT BE STARTED");
    } else if (activate == 1) {
        check_soon();
    } else if (!g_forget_armed) {
        g_forget_armed = true;
    } else {
        g_forget_armed = false;
        ch_online::forget(gm->code);
        open_page(PAGE_CAMPAIGN);
    }
}

/* ---- entering a code ---------------------------------------------------------- */

static void join_set_code(const char* s) {
    using namespace online_ui;
    int n = 0, pick[8];
    for (; s && *s && n < 8; ++s) {
        const char c = (char)std::toupper((unsigned char)*s);
        const char* p = std::strchr(kChars, c);
        if (p && c) pick[n++] = (int)(p - kChars);
    }
    if (n == 8) {
        std::memcpy(g_code, pick, sizeof g_code);
        g_pos = 7;
    } else {
        note("THE CLIPBOARD HAS NO 8-LETTER GAME CODE");
    }
}

static std::string join_code_text() {
    using namespace online_ui;
    std::string c;
    for (int i = 0; i < 8; ++i) {
        if (i == 4) c += '-';
        c += kChars[g_code[i]];
    }
    return c;
}

/* A letter or digit key pressed this frame, or 0. */
static char typed_char() {
    if (ImGui::GetIO().KeyCtrl) return 0;
    for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) return (char)('A' + (k - ImGuiKey_A));
    for (int k = ImGuiKey_0; k <= ImGuiKey_9; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) return (char)('0' + (k - ImGuiKey_0));
    for (int k = ImGuiKey_Keypad0; k <= ImGuiKey_Keypad9; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) return (char)('0' + (k - ImGuiKey_Keypad0));
    return 0;
}

static void draw_join_page(LauncherModel*, const Gfx& g, const Input& in) {
    using namespace online_ui;
    const ch_online::Snapshot s = ch_online::snapshot();
    const bool blocked = g_state.blocked;

    /* Keys. Backspace steps back a letter here instead of leaving the page. */
    const bool bs = !blocked && ImGui::IsKeyPressed(ImGuiKey_Backspace, true);
    const bool leave = in.back && !bs;
    bool join = in.confirm;
    if (!blocked) {
        if (in.left)  g_pos = (g_pos + 7) % 8;
        if (in.right) g_pos = (g_pos + 1) % 8;
        if (in.up)    g_code[g_pos] = (g_code[g_pos] + 1) % kNChars;
        if (in.down)  g_code[g_pos] = (g_code[g_pos] + kNChars - 1) % kNChars;
        if (bs && g_pos > 0) --g_pos;
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
            join_set_code(ImGui::GetClipboardText());
        const char c = typed_char();
        if (c) {
            const char* p = std::strchr(kChars, c);
            if (p) {
                g_code[g_pos] = (int)(p - kChars);
                if (g_pos < 7) ++g_pos;
            } else {
                note("GAME CODES HAVE NO 0, O, 1 OR I");
            }
        }
    }

    header(g, "JOIN A GAME", "ENTER THE OTHER PLAYER'S CODE");
    panel(g, 24.0f, 76.0f, 592.0f, 150.0f, "GAME CODE");
    const float bw = 40.0f, bh = 54.0f, gap = 6.0f, dash = 18.0f;
    const float total = 8 * bw + 7 * gap + dash;
    const float x0 = 24.0f + (592.0f - total) * 0.5f, y0 = 118.0f;
    for (int i = 0; i < 8; ++i) {
        const float x = x0 + i * (bw + gap) + (i >= 4 ? dash : 0.0f);
        const bool sel = i == g_pos;
        const ImVec2 a = at(g, x, y0), b = at(g, x + bw, y0 + bh);
        g.dl->AddRectFilled(a, b, sel ? IM_COL32(242, 176, 40, 50) : kPanel);
        g.dl->AddRect(a, b, sel ? kHi : kLine);
        const char ch[2] = { kChars[g_code[i]], 0 };
        text(g, x + (bw - text_width(g, 30.0f, ch)) * 0.5f, y0 + 10.0f, 30.0f,
             sel ? kBright : kText, ch);
        if (sel) {   /* up / down hints */
            const float cx = x + bw * 0.5f;
            g.dl->AddTriangleFilled(at(g, cx - 6.0f, y0 - 6.0f), at(g, cx + 6.0f, y0 - 6.0f),
                                    at(g, cx, y0 - 13.0f), kHi);
            g.dl->AddTriangleFilled(at(g, cx - 6.0f, y0 + bh + 6.0f), at(g, cx + 6.0f, y0 + bh + 6.0f),
                                    at(g, cx, y0 + bh + 13.0f), kHi);
        }
        /* Mouse: top half = next letter, bottom half = previous. */
        if (!blocked && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (ImGui::IsMouseHoveringRect(a, at(g, x + bw, y0 + bh * 0.5f), false)) {
                g_pos = i; g_code[i] = (g_code[i] + 1) % kNChars;
            } else if (ImGui::IsMouseHoveringRect(at(g, x, y0 + bh * 0.5f), b, false)) {
                g_pos = i; g_code[i] = (g_code[i] + kNChars - 1) % kNChars;
            }
        }
    }
    const float dx = x0 + 4 * (bw + gap) - gap * 0.5f + dash * 0.5f;
    g.dl->AddLine(at(g, dx - 6.0f, y0 + bh * 0.5f), at(g, dx + 6.0f, y0 + bh * 0.5f), kText, 2.0f * g.s);
    text(g, 38.0f, 190.0f, 10.0f, kDim,
         "LEFT / RIGHT  PICK A LETTER      UP / DOWN  CHANGE IT      KEYBOARD: TYPE IT, OR CTRL+V TO PASTE");
    text(g, 38.0f, 204.0f, 10.0f, kDim, "MOUSE: CLICK THE TOP OR THE BOTTOM HALF OF A LETTER");

    panel(g, 24.0f, 242.0f, 592.0f, 174.0f, "JOIN");
    const RowHit hj = row(g, 25.0f, 256.0f, 590.0f, true, !s.busy,
                          ("JOIN GAME " + join_code_text()).c_str(), "", false, false);
    const RowHit hp = row(g, 25.0f, 256.0f + kRowH, 590.0f, false, !s.busy,
                          "PASTE A CODE", "", false, false);
    if (hj.click) join = true;
    if (hp.click) join_set_code(ImGui::GetClipboardText());
    text(g, 38.0f, 316.0f, 11.0f, kText,
         "You become player B. Player A plays the first turn; this launcher "
         "tells you when it is yours.", 560.0f);
    if (s.busy) text(g, 38.0f, 370.0f, 11.0f, kDim, "TALKING TO THE RELAY...");
    else result_line(g, s, 38.0f, 370.0f, 560.0f);
    const bool back = footer(g, "A / ENTER  JOIN      B / ESC / RIGHT CLICK  BACK", true);

    follow_result(s);
    if (leave || back) { open_page(PAGE_CAMPAIGN); return; }
    if (join && !s.busy) ch_online::join_game(join_code_text());
}
