// Interface drawn with the SDL renderer on top of the scaled world.
#include "game.h"

static SDL_Texture *TEX[SP_COUNT];
static SDL_Texture *portrait;
static u32 pbuf[16 * 18];
static float mx, my;

typedef struct { float x, y, w, h; } Box;
static bool inbox(Box b, float x, float y) { return x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h; }

static SDL_Texture *tex(SDL_Renderer *r, int sp) {
    if (!TEX[sp]) {
        Spr *s = &SPR[sp];
        TEX[sp] = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, s->w, s->h);
        SDL_UpdateTexture(TEX[sp], NULL, s->p, s->w * 4);
        SDL_SetTextureScaleMode(TEX[sp], SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(TEX[sp], SDL_BLENDMODE_BLEND);
    }
    return TEX[sp];
}

void ui_init(SDL_Renderer *r) {
    portrait = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 16, 18);
    SDL_SetTextureScaleMode(portrait, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(portrait, SDL_BLENDMODE_BLEND);
}

static void fill(SDL_Renderer *r, float x, float y, float w, float h, u32 c, int a) {
    SDL_SetRenderDrawColor(r, CR(c), CG(c), CB(c), a);
    SDL_FRect f = {x, y, w, h};
    SDL_RenderFillRect(r, &f);
}
static void frame(SDL_Renderer *r, float x, float y, float w, float h, u32 c, int a) {
    SDL_SetRenderDrawColor(r, CR(c), CG(c), CB(c), a);
    SDL_FRect f = {x, y, w, h};
    SDL_RenderRect(r, &f);
}
static void panel(SDL_Renderer *r, float x, float y, float w, float h) {
    fill(r, x, y, w, h, 0x0F121C, 215);
    frame(r, x, y, w, h, 0xFFFFFF, 34);
    fill(r, x + 1, y + 1, w - 2, 1, 0xFFFFFF, 18);
}
static void text(SDL_Renderer *r, float x, float y, const char *s, u32 c, int a, float scale) {
    SDL_SetRenderScale(r, scale, scale);
    SDL_SetRenderDrawColor(r, 0, 0, 0, a / 2);
    SDL_RenderDebugText(r, x / scale + 1.0f / scale * 1, y / scale + 1.0f / scale * 1, s);
    SDL_SetRenderDrawColor(r, CR(c), CG(c), CB(c), a);
    SDL_RenderDebugText(r, x / scale, y / scale, s);
    SDL_SetRenderScale(r, 1, 1);
}
static void icon(SDL_Renderer *r, int sp, float x, float y, float scale) {
    SDL_Texture *t = tex(r, sp);
    SDL_FRect d = {x, y, SPR[sp].w * scale, SPR[sp].h * scale};
    SDL_RenderTexture(r, t, NULL, &d);
}
static void bar(SDL_Renderer *r, float x, float y, float w, float h, float v, u32 c) {
    fill(r, x, y, w, h, 0x000000, 150);
    fill(r, x, y, w * clampf(v, 0, 1), h, c, 230);
    fill(r, x, y, w * clampf(v, 0, 1), 1, 0xFFFFFF, 50);
}

/* ------------------------------------------------------------------ layout */

static const char *TOOL_NAME[TOOL_COUNT] = {"Select", "Chop", "Mine", "Harvest", "Stockpile", "Farm", "Build", "Tear down", "Cancel"};
static const char *TOOL_KEY[TOOL_COUNT] = {"Q", "C", "X", "H", "Z", "F", "B", "V", "R"};
static const char *TOOL_TIP[TOOL_COUNT] = {
    "Click a colonist to see inside their head.",
    "Mark trees. Only colonists who SEE the mark know about it.",
    "Mark boulders for mining.",
    "Mark berry bushes to be harvested whenever they fruit.",
    "Paint a stockpile. Colonists must remember where it is.",
    "Paint farm plots to sow vegetables.",
    "Place blueprints. Materials come from wherever colonists remember them.",
    "Mark structures to be torn down.",
    "Remove marks, zones and blueprints."};

static Box tool_box(int i) {
    float w = 54, gap = 6, total = TOOL_COUNT * w + (TOOL_COUNT - 1) * gap;
    return (Box){G.winw / 2.0f - total / 2 + i * (w + gap), G.winh - w - 12, w, w};
}
static Box build_box(int i) {
    float w = 54, gap = 6, total = B_COUNT * w + (B_COUNT - 1) * gap;
    return (Box){G.winw / 2.0f - total / 2 + i * (w + gap), G.winh - 2 * w - 34, w, w};
}
static Box roster_box(int i) { return (Box){10, 250 + i * 30.0f, 210, 28}; }
static Box mind_button(void) { return (Box){G.winw - 310 + 14, 0, 282, 26}; }
static float panel_h = 0;

void screen_to_tile(float sx, float sy, int *tx, int *ty) {
    int cx0 = (int)floorf(G.camx - FBW / 2.0f), cy0 = (int)floorf(G.camy - FBH / 2.0f);
    int wx = cx0 + (int)(sx / G.scale), wy = cy0 + (int)(sy / G.scale);
    *tx = wx >= 0 ? wx / TS : -1;
    *ty = wy >= 0 ? wy / TS : -1;
}

/* ------------------------------------------------------------------ pieces */

static void draw_topbar(SDL_Renderer *r) {
    panel(r, 0, 0, G.winw, 34);
    text(r, 12, 9, "HEARTHMIND", 0xFFD27F, 255, 2);
    float h = hour_of_day();
    char buf[128];
    snprintf(buf, sizeof buf, "Day %d   %02d:%02d", day_number(), (int)h, (int)((h - (int)h) * 60));
    float cx = G.winw / 2.0f - 120;
    bool night = is_night();
    fill(r, cx, 9, 16, 16, night ? 0xC8D0FF : 0xFFD050, 255);
    if (night) fill(r, cx + 5, 7, 14, 14, 0x0F121C, 255);
    text(r, cx + 24, 9, buf, 0xFFFFFF, 255, 2);
    const char *sp = G.paused ? "PAUSED" : G.speed == 1 ? ">" : G.speed == 2 ? ">>" : G.speed == 4 ? ">>>" : ">>>>";
    text(r, cx + 250, 9, sp, G.paused ? 0xFF8080 : 0x9FE0FF, 255, 2);
    int alive = 0;
    for (int i = 0; i < G.ncol; i++) alive += G.col[i].alive;
    float x = G.winw - 440;
    snprintf(buf, sizeof buf, "%d", alive);
    text(r, x, 13, "Settlers", 0xAAAAAA, 255, 1);
    text(r, x + 70, 9, buf, 0xFFFFFF, 255, 2);
    x += 110;
    int items[4] = {-1, I_WOOD, I_STONE, I_MEAL};
    int sps[4] = {SP_ITEM0 + I_BERRY, SP_ITEM0 + I_WOOD, SP_ITEM0 + I_STONE, SP_ITEM0 + I_MEAL};
    for (int k = 0; k < 4; k++) {
        icon(r, sps[k], x, 4, 2.6f);
        snprintf(buf, sizeof buf, "%d", stock_count(items[k]));
        text(r, x + 30, 9, buf, 0xFFFFFF, 255, 2);
        x += 80;
    }
}

static void draw_log(SDL_Renderer *r) {
    int n = G.nlog < 8 ? G.nlog : 8;
    if (!n) return;
    u32 now = SDL_GetTicks();
    float y = 44;
    panel(r, 10, y, 520, n * 20 + 12);
    for (int i = 0; i < n; i++) {
        LogMsg *m = &G.log[G.nlog - n + i];
        u32 age = now - m->tick;
        int a = age > 40000 ? 110 : 255;
        text(r, 18, y + 8 + i * 20, m->text, m->col & 0xFFFFFF, a, 1);
    }
}

static void update_portrait(Colonist *c) {
    memset(pbuf, 0, sizeof pbuf);
    int d = c->dir;
    c->dir = 1;
    draw_colonist_to(pbuf, 16, 18, 3, 1, c, 0, false);
    c->dir = d;
    SDL_UpdateTexture(portrait, NULL, pbuf, 16 * 4);
}

static void draw_roster(SDL_Renderer *r) {
    int k = 0;
    for (int i = 0; i < G.ncol; i++) {
        Colonist *c = &G.col[i];
        if (!c->alive) continue;
        Box b = roster_box(k++);
        bool hov = inbox(b, mx, my);
        fill(r, b.x, b.y, b.w, b.h, G.sel == i ? 0x3A3420 : 0x0F121C, hov ? 235 : 190);
        frame(r, b.x, b.y, b.w, b.h, G.sel == i ? 0xFFD27F : 0xFFFFFF, G.sel == i ? 200 : 25);
        update_portrait(c);
        SDL_FRect d = {b.x + 4, b.y + 1, 16 * 1.5f, 18 * 1.5f};
        SDL_RenderTexture(r, portrait, NULL, &d);
        text(r, b.x + 32, b.y + 4, c->name, 0xFFFFFF, 255, 1);
        text(r, b.x + 32, b.y + 16, c->state == S_SLEEP ? "asleep" : job_name(c->job), 0x9A9AA8, 255, 1);
        if (c->thought == TH_FORGET || c->thought == TH_QUESTION) icon(r, SP_TH0 + c->thought, b.x + b.w - 22, b.y + 5, 2);
        if (c->hunger > 75) fill(r, b.x + b.w - 30, b.y + 10, 5, 8, 0xFF5050, 255);
    }
}

static void draw_colonist_panel(SDL_Renderer *r) {
    if (G.sel < 0 || !G.col[G.sel].alive) return;
    Colonist *c = &G.col[G.sel];
    float x = G.winw - 310, y = 44, w = 300;
    panel(r, x, y, w, panel_h);
    update_portrait(c);
    SDL_FRect d = {x + 10, y + 10, 16 * 4, 18 * 4};
    fill(r, x + 10, y + 10, 64, 72, 0x252a38, 255);
    SDL_RenderTexture(r, portrait, NULL, &d);
    text(r, x + 86, y + 14, c->name, 0xFFD27F, 255, 2);
    text(r, x + 86, y + 36, c->trait, 0x9FD8FF, 255, 1);
    text(r, x + 86, y + 52, c->state == S_SLEEP ? "Sleeping" : c->status, 0xDDDDDD, 255, 1);
    char buf[96];
    snprintf(buf, sizeof buf, "Surprised %dx  Relearned %dx", c->surprises, c->lessons);
    text(r, x + 86, y + 68, buf, 0x808090, 255, 1);
    float yy = y + 96;
    const char *nn[3] = {"Fullness", "Energy", "Health"};
    float vv[3] = {1 - c->hunger / 100, c->energy / 100, c->health / 100};
    u32 cc[3] = {0xF0A040, 0x70B0FF, 0x70E070};
    for (int i = 0; i < 3; i++) {
        text(r, x + 12, yy, nn[i], 0xBBBBBB, 255, 1);
        bar(r, x + 100, yy, 186, 8, vv[i], vv[i] < 0.25f ? 0xFF4040 : cc[i]);
        yy += 16;
    }
    yy += 8;
    text(r, x + 12, yy, "KNOW-HOW", 0xFFD27F, 255, 1);
    text(r, x + 100, yy, "(fades unless practised)", 0x707080, 255, 1);
    yy += 16;
    for (int s = 0; s < SK_COUNT; s++) {
        float k = c->know[s];
        u32 col = k < 0.2f ? 0xFF5050 : k < 0.4f ? 0xF0C040 : 0x70E070;
        text(r, x + 12, yy, SKILL_NAME[s], k < 0.2f ? 0x9A7070 : 0xDDDDDD, 255, 1);
        bar(r, x + 110, yy, 130, 8, k, col);
        fill(r, x + 110 + 130 * 0.2f, yy - 1, 1, 10, 0xFFFFFF, 90);
        if (k < 0.2f) text(r, x + 248, yy, k < 0.01f ? "none" : "lost", 0xFF7070, 255, 1);
        yy += 15;
    }
    yy += 10;
    text(r, x + 12, yy, "MEMORY", 0xFFD27F, 255, 1);
    yy += 16;
    snprintf(buf, sizeof buf, "Remembers %.0f%% of the land", mem_known_frac(c) * 100);
    text(r, x + 12, yy, buf, 0xDDDDDD, 255, 1);
    yy += 15;
    struct { const char *n; int v; } kk[3] = {{"Stockpile", c->known_stock}, {"Campfire", c->known_fire}, {"Own bed", c->known_bed}};
    for (int i = 0; i < 3; i++) {
        text(r, x + 12, yy, kk[i].n, 0xBBBBBB, 255, 1);
        text(r, x + 110, yy, kk[i].v ? "remembered" : "doesn't know", kk[i].v ? 0x70E070 : 0xFF7070, 255, 1);
        yy += 15;
    }
    yy += 8;
    Box mb = mind_button();
    mb.x = x + 9; mb.y = yy;
    bool hov = inbox(mb, mx, my);
    fill(r, mb.x, mb.y, mb.w, mb.h, G.mindview ? 0x6A4A8A : 0x2A2F44, hov ? 255 : 220);
    frame(r, mb.x, mb.y, mb.w, mb.h, 0xC8A0FF, 160);
    text(r, mb.x + 12, mb.y + 9, G.mindview ? "[M] Leave mind view" : "[M] See through their memory", 0xFFFFFF, 255, 1);
    yy += 34;
    panel_h = yy - y;
}

static void draw_toolbar(SDL_Renderer *r) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        Box b = tool_box(i);
        bool on = G.tool == i, hov = inbox(b, mx, my);
        fill(r, b.x, b.y, b.w, b.h, on ? 0x3A3420 : 0x0F121C, hov ? 240 : 205);
        frame(r, b.x, b.y, b.w, b.h, on ? 0xFFD27F : 0xFFFFFF, on ? 230 : 40);
        icon(r, SP_TOOL0 + i, b.x + 9, b.y + 5, 3);
        text(r, b.x + 3, b.y + 3, TOOL_KEY[i], 0xFFD27F, 200, 1);
        if (hov) {
            float tw = strlen(TOOL_TIP[i]) * 8 + 16;
            panel(r, b.x + b.w / 2 - tw / 2, b.y - 50 - (G.tool == TOOL_BUILD ? 76 : 0), tw, 40);
            text(r, b.x + b.w / 2 - tw / 2 + 8, b.y - 44 - (G.tool == TOOL_BUILD ? 76 : 0), TOOL_NAME[i], 0xFFD27F, 255, 1);
            text(r, b.x + b.w / 2 - tw / 2 + 8, b.y - 28 - (G.tool == TOOL_BUILD ? 76 : 0), TOOL_TIP[i], 0xDDDDDD, 255, 1);
        }
    }
    if (G.tool == TOOL_BUILD)
        for (int i = 0; i < B_COUNT; i++) {
            Box b = build_box(i);
            bool on = G.build == i, hov = inbox(b, mx, my);
            fill(r, b.x, b.y, b.w, b.h, on ? 0x203A44 : 0x0F121C, hov ? 240 : 205);
            frame(r, b.x, b.y, b.w, b.h, on ? 0x9FE0FF : 0xFFFFFF, on ? 230 : 40);
            icon(r, SP_BUILD0 + i, b.x + 9, b.y + 3, 3);
            char k[4] = {(char)('1' + i), 0};
            text(r, b.x + 3, b.y + 3, k, 0x9FE0FF, 220, 1);
            char cost[32];
            snprintf(cost, sizeof cost, "%d", BUILDS[i].cost);
            icon(r, SP_ITEM0 + BUILDS[i].mat, b.x + 26, b.y + 40, 1.2f);
            text(r, b.x + 40, b.y + 43, cost, 0xFFFFFF, 255, 1);
            if (hov) {
                panel(r, b.x - 20, b.y - 26, 110, 22);
                text(r, b.x - 12, b.y - 19, BUILDS[i].name, 0xFFFFFF, 255, 1);
            }
        }
}

static void draw_tooltip(SDL_Renderer *r) {
    if (!inb(G.hovx, G.hovy) || G.dragging) return;
    for (int i = 0; i < TOOL_COUNT; i++) if (inbox(tool_box(i), mx, my)) return;
    int idx = IDX(G.hovx, G.hovy);
    if (!G.fog[idx] && !G.mindview) {
        panel(r, mx + 18, my + 12, 250, 22);
        text(r, mx + 26, my + 19, "Forgotten - no one remembers this", 0x9090A0, 255, 1);
        return;
    }
    Tile *t = &G.tiles[idx];
    char lines[8][96];
    int n = 0;
    const char *terr[4] = {"water", "sand", "grass", "rocky ground"};
    if (t->feat == F_BLUEPRINT) snprintf(lines[n++], 96, "Blueprint: %s (%d/%d %s)", BUILDS[t->aux].name, t->bpmat, BUILDS[t->aux].cost, ITEM_NAME[BUILDS[t->aux].mat]);
    else if (t->feat == F_BUSH) snprintf(lines[n++], 96, "Berry bush (%d berries)", t->aux);
    else if (t->feat == F_CROP) snprintf(lines[n++], 96, "Vegetables (%s)", t->aux >= 3 ? "ripe" : "growing");
    else if (t->feat == F_BED) snprintf(lines[n++], 96, "Bed%s%s", t->aux ? " of " : " (unclaimed)", t->aux ? G.col[t->aux - 1].name : "");
    else if (t->feat) snprintf(lines[n++], 96, "%c%s", FEAT_NAME[t->feat][0] - 32, FEAT_NAME[t->feat] + 1);
    else snprintf(lines[n++], 96, "%c%s%s", terr[t->terrain][0] - 32, terr[t->terrain] + 1, t->flags & FL_FLOOR ? " (wood floor)" : t->flags & FL_FARM ? " (farm plot)" : "");
    if (t->item) snprintf(lines[n++], 96, "%d %s%s", t->icount, ITEM_NAME[t->item], t->flags & FL_STOCK ? " in stockpile" : "");
    else if (t->flags & FL_STOCK) snprintf(lines[n++], 96, "Empty stockpile slot");
    snprintf(lines[n++], 96, "Remembered by:");
    int shown = 0;
    for (int i = 0; i < G.ncol && shown < 5; i++) {
        Colonist *c = &G.col[i];
        if (!c->alive || !c->mem[idx].str) continue;
        MemCell *m = &c->mem[idx];
        bool wrong = m->feat != t->feat || m->item != t->item || (t->feat == F_BUSH && (m->aux > 0) != (t->aux > 0));
        snprintf(lines[n++], 96, "  %-6s %3d%%%s", c->name, m->str * 100 / 255, wrong ? "  (outdated!)" : "");
        shown++;
    }
    if (!shown) snprintf(lines[n++], 96, "  nobody");
    float w = 0;
    for (int i = 0; i < n; i++) w = fmaxf(w, strlen(lines[i]) * 8.0f);
    float x = mx + 18, y = my + 12;
    if (x + w + 16 > G.winw) x = mx - w - 30;
    panel(r, x, y, w + 16, n * 13 + 10);
    for (int i = 0; i < n; i++) {
        u32 c = i == 0 ? 0xFFD27F : strstr(lines[i], "outdated") ? 0xFF9A7A : 0xDDDDDD;
        text(r, x + 8, y + 6 + i * 13, lines[i], c, 255, 1);
    }
}

static void draw_help(SDL_Renderer *r) {
    static const char *L[] = {
        "HEARTHMIND",
        "",
        "Nobody here has a map. Each settler knows only what they have SEEN,",
        "or what someone TOLD them - and memories fade.",
        "",
        "- Places they have not seen in a while blur, drift, and vanish.",
        "- Seeing a place again after a gap makes the memory last longer.",
        "- Sleep consolidates memories; exhaustion erodes them.",
        "- Skills (cooking, building...) fade too unless practised.",
        "  Forgotten skills are relearned by watching, being taught",
        "  around the fire in the evening, or clumsy trial and error.",
        "- Your orders are physical marks. Settlers learn of them only by",
        "  seeing them or hearing about them from others.",
        "- The fog is the colony's collective memory. When everyone",
        "  forgets a place, it fades from your view too.",
        "",
        "Mouse: left click/drag = use tool   right click = cancel",
        "       middle drag / WASD = pan     wheel = zoom",
        "Space pause  1-4 speed  Tab next settler  M mind view  F1 help",
        "",
        "Click anywhere to begin.",
    };
    int n = sizeof L / sizeof *L;
    float w = 600, h = n * 16 + 40, x = G.winw / 2.0f - w / 2, y = G.winh / 2.0f - h / 2;
    fill(r, 0, 0, G.winw, G.winh, 0x000000, 120);
    panel(r, x, y, w, h);
    frame(r, x + 3, y + 3, w - 6, h - 6, 0xFFD27F, 60);
    for (int i = 0; i < n; i++) {
        if (i == 0) text(r, x + w / 2 - 80, y + 18, L[i], 0xFFD27F, 255, 2);
        else text(r, x + 28, y + 24 + i * 16, L[i], i >= n - 5 ? 0x9FE0FF : 0xE0E0E0, 255, 1);
    }
}

void ui_draw(SDL_Renderer *r) {
    SDL_GetMouseState(&mx, &my);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    draw_topbar(r);
    draw_log(r);
    draw_roster(r);
    draw_colonist_panel(r);
    draw_toolbar(r);
    if (G.mindview && G.sel >= 0 && G.col[G.sel].alive) {
        char buf[96];
        snprintf(buf, sizeof buf, "MIND VIEW - the world as %s remembers it", G.col[G.sel].name);
        float w = strlen(buf) * 16 + 30;
        float by = G.winh - (G.tool == TOOL_BUILD ? 200 : 128);
        panel(r, G.winw / 2.0f - w / 2, by, w, 50);
        text(r, G.winw / 2.0f - w / 2 + 15, by + 8, buf, 0xC8A0FF, 255, 2);
        text(r, G.winw / 2.0f - w / 2 + 15, by + 32, "Faded = half-forgotten. Wobbling = unsure. Things may be gone... or never were.", 0x9A9AB0, 255, 1);
    }
    draw_tooltip(r);
    if (G.help) draw_help(r);
}

bool ui_event(SDL_Event *e) {
    if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN || e->button.button != SDL_BUTTON_LEFT) return false;
    float x = e->button.x, y = e->button.y;
    if (G.help) { G.help = false; return true; }
    if (y < 34) return true;
    for (int i = 0; i < TOOL_COUNT; i++)
        if (inbox(tool_box(i), x, y)) { G.tool = i; return true; }
    if (G.tool == TOOL_BUILD)
        for (int i = 0; i < B_COUNT; i++)
            if (inbox(build_box(i), x, y)) { G.build = i; return true; }
    int k = 0;
    for (int i = 0; i < G.ncol; i++) {
        if (!G.col[i].alive) continue;
        if (inbox(roster_box(k++), x, y)) {
            if (G.sel == i) { G.camx = G.col[i].x * TS; G.camy = G.col[i].y * TS; }
            G.sel = i;
            return true;
        }
    }
    if (G.sel >= 0 && G.col[G.sel].alive) {
        Box p = {G.winw - 310, 44, 300, panel_h};
        Box mb = {p.x + 9, p.y + panel_h - 34, 282, 26};
        if (inbox(mb, x, y)) { G.mindview = !G.mindview; return true; }
        if (inbox(p, x, y)) return true;
    }
    return false;
}
