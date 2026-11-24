from reportlab.lib.pagesizes import letter
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle,
                                Image, PageBreak, KeepTogether)
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib import colors
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.graphics.shapes import Drawing, Rect, String, Line, Polygon
from reportlab.graphics import renderPDF

F = "/usr/share/fonts/truetype/dejavu/"
pdfmetrics.registerFont(TTFont("Sans", F + "DejaVuSans.ttf"))
pdfmetrics.registerFont(TTFont("Sans-Bold", F + "DejaVuSans-Bold.ttf"))
pdfmetrics.registerFont(TTFont("Sans-Oblique", F + "DejaVuSans-Oblique.ttf"))
from reportlab.lib.fonts import addMapping
addMapping("Sans", 0, 0, "Sans"); addMapping("Sans", 1, 0, "Sans-Bold"); addMapping("Sans", 0, 1, "Sans-Oblique")

IMG = "/home/claude/brief/img/"
OUT = "/home/claude/brief/CarnageHeart_Recomp_Status_Briefing.pdf"

NAVY = colors.HexColor("#1b2a4a"); ACCENT = colors.HexColor("#2f6db5")
GREEN = colors.HexColor("#2e8b57"); AMBER = colors.HexColor("#d08a12")
RED = colors.HexColor("#b03a3a"); GREY = colors.HexColor("#8a8f99")
LIGHT = colors.HexColor("#eef2f8")

ss = getSampleStyleSheet()
body = ParagraphStyle("b", parent=ss["Normal"], fontName="Sans", fontSize=10, leading=14, spaceAfter=6)
small = ParagraphStyle("s", parent=body, fontSize=8.5, leading=11, textColor=colors.HexColor("#444"))
cap = ParagraphStyle("c", parent=small, alignment=1, fontName="Sans-Oblique", spaceAfter=10)
h1 = ParagraphStyle("h1", parent=body, fontName="Sans-Bold", fontSize=17, leading=21, textColor=NAVY, spaceBefore=4, spaceAfter=8)
h2 = ParagraphStyle("h2", parent=body, fontName="Sans-Bold", fontSize=12.5, leading=16, textColor=ACCENT, spaceBefore=8, spaceAfter=4)
cell = ParagraphStyle("cell", parent=body, fontSize=8.8, leading=11.5, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Sans-Bold")
bullet = ParagraphStyle("bl", parent=body, leftIndent=12, bulletIndent=2, spaceAfter=3)

STATUS = {
    "DONE": GREEN, "WORKING": GREEN, "BUILT": GREEN,
    "IN PROGRESS": AMBER, "TESTING": AMBER, "PARTIAL": AMBER,
    "PLANNED": GREY, "NOT STARTED": RED, "LATER": GREY,
}

def badge(s):
    c = STATUS[s]
    return Paragraph(f'<font color="{c.hexval().replace("0x", "#")}"><b>● {s}</b></font>', cell)

def P(t, st=body): return Paragraph(t, st)
def bullets(items): return [Paragraph(i, bullet, bulletText="•") for i in items]

def img(name, w, caption=None):
    from PIL import Image as PI
    im = PI.open(IMG + name); iw, ih = im.size
    out = [Image(IMG + name, width=w, height=w * ih / iw)]
    if caption: out.append(P(caption, cap))
    return out

def status_table(rows, widths):
    data = [[P("<b>Item</b>", cell), P("<b>Status</b>", cell), P("<b>Notes</b>", cell)]]
    for a, s, n in rows:
        data.append([P(a, cellb), badge(s), P(n, cell)])
    t = Table(data, colWidths=widths, repeatRows=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), LIGHT),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LINEBELOW", (0, 0), (-1, -1), 0.4, colors.HexColor("#ccd3de")),
        ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ]))
    return t

# ---------- diagrams ----------
def progress_chart():
    items = [("Linux build", 100, GREEN), ("Windows setup kit", 85, AMBER),
             ("Mouse / pointer controls", 85, AMBER), ("Launcher", 70, AMBER),
             ("Campaign versus (multiplayer)", 60, AMBER), ("Card sharing", 20, GREY),
             ("Android", 5, RED), ("Browser", 5, RED), ("Turn notifications", 0, RED)]
    W, rowh = 460, 16
    d = Drawing(W, rowh * len(items) + 24)
    x0, bw = 175, 240
    for i, (name, pct, col) in enumerate(items):
        y = d.height - 20 - (i + 1) * rowh + 6
        d.add(String(x0 - 8, y + 4, name, fontName="Sans", fontSize=9, textAnchor="end"))
        d.add(Rect(x0, y, bw, 13, fillColor=colors.HexColor("#e3e7ee"), strokeColor=None))
        if pct: d.add(Rect(x0, y, bw * pct / 100, 13, fillColor=col, strokeColor=None))
        d.add(String(x0 + bw + 6, y + 3, f"{pct}%", fontName="Sans-Bold", fontSize=9))
    d.add(String(x0, d.height - 10, "Rough completion estimate per work stream", fontName="Sans-Oblique", fontSize=8.5, fillColor=GREY))
    return d

def box(d, x, y, w, h, text, fill, fs=8.5, tc=colors.white):
    d.add(Rect(x, y, w, h, rx=5, ry=5, fillColor=fill, strokeColor=None))
    lines = text.split("\n")
    for i, ln in enumerate(lines):
        yy = y + h / 2 + (len(lines) / 2 - i - 0.75) * (fs + 2)
        d.add(String(x + w / 2, yy, ln, fontName="Sans-Bold" if i == 0 else "Sans", fontSize=fs if i == 0 else fs - 1, fillColor=tc, textAnchor="middle"))

def arrow(d, x1, y1, x2, y2, col=GREY):
    d.add(Line(x1, y1, x2, y2, strokeColor=col, strokeWidth=1.5))
    import math
    a = math.atan2(y2 - y1, x2 - x1); s = 6
    d.add(Polygon([x2, y2, x2 - s * math.cos(a - .45), y2 - s * math.sin(a - .45),
                   x2 - s * math.cos(a + .45), y2 - s * math.sin(a + .45)], fillColor=col, strokeColor=None))

def roadmap():
    steps = [("1. Campaign\nRAM map", GREEN), ("2. VS + saves\nremote battles", GREEN),
             ("3. Order +\nfactory map", GREEN), ("4. Hot-seat\none PC", GREEN),
             ("5. Turn files\n2 copies", AMBER), ("6. Storage +\nnotifications", GREY),
             ("7. Enemy turn\nreplay", GREY), ("8. Card share\n+ live VS", GREY)]
    d = Drawing(470, 120)
    w, h, gap = 104, 44, 15
    for i, (t, c) in enumerate(steps):
        r, col = divmod(i, 4)
        x = col * (w + gap); y = 66 - r * 60
        box(d, x, y, w, h, t, c)
        if col < 3: arrow(d, x + w + 1, y + h / 2, x + w + gap - 1, y + h / 2)
    arrow(d, 3 * (w + gap) + w / 2, 66, 3 * (w + gap) + w / 2, 6 + h + 2) if False else None
    d.add(Line(3 * (w + gap) + w / 2, 66, 3 * (w + gap) + w / 2, 58, strokeColor=GREY, strokeWidth=1.5))
    d.add(Line(3 * (w + gap) + w / 2, 58, w / 2, 58, strokeColor=GREY, strokeWidth=1.5))
    arrow(d, w / 2, 58, w / 2, 6 + h + 1)
    return d

def turn_flow():
    d = Drawing(470, 175)
    box(d, 0, 120, 140, 48, "Player A's turn\ngive orders, End Turn", ACCENT)
    box(d, 165, 120, 140, 48, "A's half of End Turn\nmove + fight (A watches)", ACCENT)
    box(d, 330, 120, 140, 48, "Write turn file\nturn-000001.cht (~37 KB)", NAVY)
    box(d, 330, 20, 140, 48, "Player B loads it\nstate + designs + log", colors.HexColor("#6a4fa0"))
    box(d, 165, 20, 140, 48, "B's turn\nsides swapped in RAM", colors.HexColor("#6a4fa0"))
    box(d, 0, 20, 140, 48, "B ends turn\nwrites turn-000002", colors.HexColor("#6a4fa0"))
    arrow(d, 141, 144, 164, 144); arrow(d, 306, 144, 329, 144)
    arrow(d, 400, 119, 400, 69); arrow(d, 329, 44, 306, 44); arrow(d, 164, 44, 141, 44)
    arrow(d, 70, 69, 70, 119)
    d.add(String(235, 92, "Shared folder today  →  GitHub / relay + phone notification later",
                 fontName="Sans-Oblique", fontSize=8.5, fillColor=GREY, textAnchor="middle"))
    return d

def footer(c, doc):
    c.saveState()
    c.setFont("Sans", 7.5); c.setFillColor(GREY)
    c.drawString(0.75 * inch, 0.5 * inch, "Carnage Heart ReComp — status briefing, 23 Nov 2026")
    c.drawRightString(7.75 * inch, 0.5 * inch, f"Page {doc.page}")
    c.restoreState()

def cover(c, doc):
    c.saveState()
    c.setFillColor(NAVY); c.rect(0, 9.1 * inch, 8.5 * inch, 1.9 * inch, fill=1, stroke=0)
    c.setFillColor(colors.white); c.setFont("Sans-Bold", 26)
    c.drawString(0.75 * inch, 10.1 * inch, "Carnage Heart ReComp")
    c.setFont("Sans", 13)
    c.drawString(0.75 * inch, 9.7 * inch, "Project status briefing  ·  23 November 2026")
    c.setFont("Sans", 9.5)
    c.drawString(0.75 * inch, 9.38 * inch, "PS1 Carnage Heart (USA, SCUS-94604) recompiled with PSXRecomp  ·  repo Hatdagger/carnage-heart-recomp")
    c.restoreState()
    footer(c, doc)

# ---------- content ----------
S = []
S.append(Spacer(1, 1.45 * inch))
S += img("pointer-hover.png", 2.7 * inch, "The recompiled game running natively — title screen with the mouse pointer driving the menu.")
S.append(P("At a glance", h2))
S += bullets([
    "<b>The game runs natively on Linux</b> and a self-building <b>Windows setup kit</b> is released (latest tag v0.1.2).",
    "<b>Mouse controls work</b>: point-and-click plus a labelled right-click menu that tells you what each pad button does on the current screen.",
    "<b>A custom launcher</b> (Launch, Online play, Share Data, Update, Settings) is built; Update and Settings work.",
    "<b>Two-player campaign versus works on one PC (hot-seat)</b> and over turn files in a simulator. Next: a test with two real copies of the game.",
    "<b>Not started yet:</b> Android, browser, turn notifications, card sharing uploads.",
])
S.append(Spacer(1, 6))
S.append(progress_chart())
S.append(PageBreak())

# Goals
S.append(P("1. Goals", h1))
S.append(P("From the project's Goals document and the decisions made since:"))
S += bullets([
    "Get the game running on <b>Windows, Linux, Android</b> and possibly <b>browser</b>.",
    "Full <b>mouse and touch</b> support, including Carnage Heart EZ-style PS1 Mouse behaviour brought into the US version.",
    "A <b>launcher</b> that installs the game on the player's PC from their own disc (no game code in the repo), updates itself, and is fully usable with a controller or a 2-button mouse.",
    "<b>Multiplayer in two steps:</b> (1) card sharing for AI-vs-AI battles; (2) two players on a shared tactical map, taking turns moving units and preparing OKEs.",
    "On <b>Android</b>, notify the other player when a turn is over; when they resume, show the enemy's moves and play out the AI battle results.",
    "<b>Priority decided 21 Nov:</b> campaign versus (step 2) comes before VS-mode card sharing.",
])
S.append(Spacer(1, 6))
S.append(P("Goal status", h2))
W3 = [1.75 * inch, 1.05 * inch, 4.2 * inch]
S.append(status_table([
    ("Linux", "WORKING", "Native build boots on OpenBIOS (no retail BIOS), reaches the campaign map, battles and chip programming."),
    ("Windows", "TESTING", "Setup kit v0.1.1/v0.1.2 released; CI first-run check is green. Full install on your own PC still to confirm."),
    ("Android", "NOT STARTED", "Plan written: SDL3 + NDK, software renderer first. Big plus: no code overlays, so everything compiles ahead of time."),
    ("Browser", "NOT STARTED", "Plan written: Emscripten + SDL3, software renderer, threads need special host headers."),
    ("Mouse controls", "DONE", "Merged to main. Labelled right-click menu on menus, battles and chip grid."),
    ("Touch controls", "PARTIAL", "Tap / long-press / two-finger tap coded; not tried on a real touch screen. Android/browser drawing path not done."),
    ("Launcher + installer", "IN PROGRESS", "Menu, installer, Update and Settings work; Online play and Share Data pages are placeholders (SOON)."),
    ("MP step 1: card sharing", "PLANNED", "Save and card layout fully mapped (one design = 1152 bytes). Upload / browse not built."),
    ("MP step 2: shared map", "IN PROGRESS", "Hot-seat tested in the real game; turn files tested in a simulator. Branch research/vs-battle-and-saves (PR #2)."),
    ("Turn notifications", "NOT STARTED", "Plan: Android checks for new turn files ~every 15 min; desktop checks when the launcher opens."),
], W3))
S.append(PageBreak())

# Done: ports
S.append(P("2. What has been done", h1))
S.append(P("2.1 Recompilation and the Linux build", h2))
S += bullets([
    "Disc analysed: 3 tracks (1 data + 2 audio), boot EXE SCUS_946.04. <b>The whole game is one 854 KB executable with no overlays</b>, so every function is compiled ahead of time — the key fact that makes Android and browser realistic.",
    "Generated <b>2,612 functions, about 6.9 million lines of C</b> in 201 shards.",
    "Linux x64 build boots on the free OpenBIOS, reaches the title screen, the campaign strategic map (3D hex map), battles and the chip-program grid.",
])
S.append(P("2.2 Windows setup kit (install from your own disc)", h2))
S += bullets([
    "The repo never holds generated game code. Players download a ~71 MB kit; first launch finds the disc beside the exe (matched by its Redump track size), downloads a portable compiler pack once, generates the C and builds the game on their PC.",
    "Releases: v0.1.0 (broken first run), <b>v0.1.1 fixes it</b> (missing generated headers in the kit), v0.1.2 adds the per-screen mouse labels.",
    "GitHub Actions builds Windows and Linux kits and runs a clean-Windows first-run check on every release.",
])
S.append(P("2.3 Launcher", h2))
S.append(P("Built from the Master of Arena system menu, green circuit-grid look, usable with controller, keyboard or a 2-button mouse alone (no typing anywhere)."))
S.append(status_table([
    ("Launch game", "WORKING", "Shows INSTALL GAME until built, then LAUNCH GAME; status panel shows disc, controller, memory card, version."),
    ("Online play", "PLANNED", "Page exists; versus and online campaign marked SOON."),
    ("Share Data", "PLANNED", "Page exists; Browse / Share marked SOON. Open question: how uploads reach GitHub without the player opening it."),
    ("Update", "WORKING", "Checks GitHub Releases, verifies SHA-256, installs over the old kit, keeps saves and settings, then rebuilds."),
    ("Settings", "WORKING", "Aspect, fullscreen, resolution, filter, vsync, volume, controller and mouse page."),
], W3))
S.append(PageBreak())

# Mouse
S.append(P("2.4 Mouse and pointer controls (merged, v0.1.2)", h1))
S.append(P("Carnage Heart USA isn't on the PS1 Mouse list, but the Japanese <i>Carnage Heart EZ</i> is. Comparing the two discs showed the US game kept the mouse cursor code and only lost the mouse branch of the pad reader — so it was put back, then extended for modern mice and touch."))
S.append(status_table([
    ("Move pointer", "DONE", "Moves the in-game cursor (the game's own mouse mode)."),
    ("Left click / tap", "DONE", "Cross (confirm) on the item under the pointer. Buttons were backwards at first — fixed."),
    ("Right click / long press", "DONE", "Small labelled menu at the pointer listing what the other buttons do on this screen."),
    ("Double right click / two-finger tap", "DONE", "Circle (back)."),
    ("Controller hand-off", "DONE", "Last device wins; pressing the pad snaps the cursor back onto the nearest menu box."),
    ("Base-screen minimap", "NOT STARTED", "Rotate arrows / MAP button aren't clickable (not in the game's hotspot table)."),
], W3))
S.append(Spacer(1, 8))
row = Table([[img("right-click-menu-base.png", 3.3 * inch, "Base screen: the menu labels what SELECT and L2 do here.")[0],
              img("right-click-menu-battle.png", 3.3 * inch)[0]],
             [P("Base screen: SELECT = Quit Game, L2 = Next Base, then More Buttons / Back.", cap),
              P("In battle: L2 Next OKE, Settings, OKE Status.", cap)]],
            colWidths=[3.5 * inch, 3.5 * inch])
row.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("ALIGN", (0, 0), (-1, -1), "CENTER")]))
S.append(row)
S.append(PageBreak())

row = Table([[img("right-click-menu-chips.png", 3.3 * inch)[0], img("right-click-menu-title.png", 3.3 * inch)[0]],
             [P("Chip-program grid: R2 opens the chip list.", cap),
              P("Unknown screen: the menu falls back to the full button list.", cap)],
             [img("right-click-menu-back.png", 3.3 * inch)[0],
              Table([[img("right-click-menu-base-closeup.png", 3.0 * inch)[0]], [img("right-click-menu-more.png", 2.2 * inch)[0]]])],
             [P("Load screen with Back highlighted.", cap), P("Close-ups: labelled entries, and MORE BUTTONS expanded in place.", cap)]],
            colWidths=[3.5 * inch, 3.5 * inch])
row.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("ALIGN", (0, 0), (-1, -1), "CENTER")]))
S.append(row)
S.append(P("How the labels work: a tool scanned 72 menu screens in the executable and recorded which buttons each one actually reacts to. The game reports which screen is showing each frame, and the menu shows only the buttons that do something there.", small))
S.append(PageBreak())

# Multiplayer
S.append(P("2.5 Multiplayer: campaign versus", h1))
S.append(P("The original game has no two-player campaign — VS mode only pits saved designs against each other while players watch. To build turn-based versus, the campaign's memory was mapped and the game's own code is reused wherever possible."))
S.append(P("Key findings", h2))
S += bullets([
    "<b>Orders are tiny</b>: a unit order is a task byte, a target hex and (for Defend) a home hex. The game handles pathing itself.",
    "<b>A battle's whole result is 6 small records</b> (HP, ammo, kills of the 6 OKEs). The game's own code then removes the dead, retreats the loser and captures bases.",
    "<b>Campaign battles can be fought as VS battles</b> — your idea — so the enemy side fights with the other player's real designs and chip programs.",
    "<b>A save file is a straight copy of the campaign tables in RAM</b> (40 KB, no checksum found). One design = one 1152-byte record.",
])
S.append(P("How a turn works now", h2))
S.append(turn_flow())
S.append(P("Each turn file carries the full end-of-turn state (already flipped to the receiver's point of view), the sender's designs, their orders and battle results. Loading the state instead of replaying it means the two copies can't drift apart; a battle is fought only once, on the device of the player who started it.", small))
S.append(P("Multiplayer roadmap", h2))
S.append(roadmap())
S.append(P("Green = done, amber = built and tested in a simulator, grey = to do.", cap))
S.append(PageBreak())

S.append(P("Multiplayer status in detail", h2))
S.append(status_table([
    ("Campaign RAM map", "DONE", "Turn counters, funds, card / unit / base / OKE tables, End Turn routine, CPU brain, battle start and result."),
    ("VS mode + saves map", "DONE", "Shared battle engine, card and save layout, memory card format."),
    ("Remote-player battles", "DONE", "src/remote_battle.c — tested with each side attacking; borrowed card slots restored byte for byte."),
    ("Order + factory map", "DONE", "Five order types; forming units; factory lines and upgrades."),
    ("Hot-seat (one PC)", "DONE", "src/hotseat.c — played through Europa: units, battles with the other side's designs, base capture, victory screen."),
    ("Turn files (two copies)", "TESTING", "src/turn_files.c — 8 rounds checked in a simulator incl. quit/resume and game over. Not yet run in two real copies."),
    ("Storage + waiting", "NOT STARTED", "Replace the shared folder (GitHub sign-in or small relay); wait in the launcher, not frozen inside End Turn."),
    ("Notifications", "NOT STARTED", "Desktop first, then Android background check."),
    ("Enemy-turn replay", "PLANNED", "Orders and results are already in the file, so a visual replay can be added later."),
    ("Card sharing (step 1)", "PLANNED", "Export / import a 1152-byte design via Share Data."),
    ("Live VS netplay", "LATER", "PSXRecomp netplay with the guest's memory card in slot 2."),
], W3))
S.append(Spacer(1, 6))
S.append(P("Known gaps", h2))
S += bullets([
    "The waiting copy is paused inside End Turn and its window doesn't respond until the file arrives.",
    "Research / negotiation isn't mapped, so it isn't in the turn file; each copy keeps its own.",
    "Kills of the other side's OKEs are added to the CPU card records.",
    "No check that both players started the same scenario.",
    "The multiplayer work lives on branch <b>research/vs-battle-and-saves</b> (PR #2, open) until ready to merge.",
])
S.append(PageBreak())

# To do
S.append(P("3. What needs to be done", h1))
S.append(P("Suggested order, most useful first:"))
todo = [
    ("1", "Confirm the Windows install", "Run the v0.1.2 kit start to finish on your own PC (also checks HiDPI and a real touchpad)."),
    ("2", "Two-copy turn-file test", "Run tools/research/turns_demo.py: two real copies on one PC playing a campaign through a shared folder."),
    ("3", "Turn storage + waiting in the launcher", "Pick GitHub sign-in vs a small relay; move the wait out of End Turn."),
    ("4", "Android build", "SDL3 Android project + NDK, software renderer, disc picked through Android file access."),
    ("5", "Touch + GLES drawing", "Port the right-click menu drawing to GLES so it shows on Android/browser; test touch."),
    ("6", "Notifications", "Desktop check on launcher open, then Android background check every ~15 min."),
    ("7", "Merge multiplayer branch", "Merge PR #2 into main once the real two-copy test passes."),
    ("8", "Browser build", "Emscripten build with a file picker and IndexedDB saves."),
    ("9", "Card sharing + enemy-turn replay", "Share Data page; replay of what the enemy did from the turn file."),
    ("10", "Smaller items", "Clickable minimap, mouse-wheel chip-grid scrolling, research/negotiation mapping, wizard error log."),
]
data = [[P("<b>#</b>", cell), P("<b>Task</b>", cell), P("<b>What it means</b>", cell)]] + [[P(a, cellb), P(b, cellb), P(c, cell)] for a, b, c in todo]
t = Table(data, colWidths=[0.35 * inch, 2.1 * inch, 4.55 * inch], repeatRows=1)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                       ("LINEBELOW", (0, 0), (-1, -1), 0.4, colors.HexColor("#ccd3de")),
                       ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]))
S.append(t)
S.append(Spacer(1, 12))
S.append(P("Where things live", h2))
S += bullets([
    "<b>main</b> (tag v0.1.2): setup kit, launcher, mouse and pointer controls, per-screen labels.",
    "<b>research/vs-battle-and-saves</b> (PR #2): RAM maps, remote battles, hot-seat, turn files.",
    "Project docs in Claude: Goals, Port Plan, Mouse Support Findings, Launcher and Install Plan, Multiplayer Plan, Campaign RAM Map, VS Mode and Saves Map.",
])

doc = SimpleDocTemplate(OUT, pagesize=letter, leftMargin=0.75 * inch, rightMargin=0.75 * inch,
                        topMargin=0.7 * inch, bottomMargin=0.75 * inch,
                        title="Carnage Heart ReComp — Status Briefing", author="Claude")
doc.build(S, onFirstPage=cover, onLaterPages=footer)
print(OUT)
