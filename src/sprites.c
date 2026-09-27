// Every sprite is generated at startup: no asset files.
#include "game.h"

Spr SPR[SP_COUNT];

static Spr mk(int w, int h) {
    Spr s = {w, h, calloc((size_t)w * h, 4)};
    return s;
}
static void put(Spr *s, int x, int y, u32 c) {
    if (x >= 0 && y >= 0 && x < s->w && y < s->h) s->p[y * s->w + x] = c;
}
static u32 get(Spr *s, int x, int y) {
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return 0;
    return s->p[y * s->w + x];
}
static void outline(Spr *s, u32 oc) {
    u32 *cp = malloc((size_t)s->w * s->h * 4);
    memcpy(cp, s->p, (size_t)s->w * s->h * 4);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            if (cp[y * s->w + x]) continue;
            bool n = false;
            for (int d = 0; d < 4 && !n; d++) {
                int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
                if (nx >= 0 && ny >= 0 && nx < s->w && ny < s->h && cp[ny * s->w + nx]) n = true;
            }
            if (n) s->p[y * s->w + x] = oc;
        }
    free(cp);
}

static u32 pal(char ch) {
    switch (ch) {
    case 'o': return 0xFF2A1C14;
    case 'k': return 0xFF2A2A36;
    case 'b': return 0xFF8A5A30;
    case 'B': return 0xFFB57D45;
    case 'r': return 0xFFD63A4A;
    case 'R': return 0xFFFFA0A8;
    case 'p': return 0xFF7A3490;
    case 'P': return 0xFFC080E0;
    case 'L': return 0xFFD2D2D8;
    case 'g': return 0xFF5AB84A;
    case 'G': return 0xFF9A9AA4;
    case 'd': return 0xFF6A6A74;
    case 'O': return 0xFFF0902A;
    case 'y': return 0xFFFFD84A;
    case 'Y': return 0xFFFFF0A0;
    case 'w': return 0xFFF4F0E8;
    case 'W': return 0xFFD0C0A0;
    case 'n': return 0xFF7A4E2A;
    case 'N': return 0xFFA87444;
    case 'u': return 0xFF4A7AD0;
    case 'U': return 0xFF9AC0FF;
    case 'c': return 0xFFE8C890;
    case 's': return 0xFFE8E8F0;
    case 'e': return 0xFF3A8A3A;
    case 'x': return 0xFFE04040;
    default: return 0;
    }
}

static Spr from_map(const char **rows, int h) {
    int w = (int)strlen(rows[0]);
    Spr s = mk(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w && rows[y][x]; x++) put(&s, x, y, pal(rows[y][x]));
    return s;
}
#define MAP(...) ({ static const char *r_[] = {__VA_ARGS__}; from_map(r_, (int)(sizeof r_ / sizeof *r_)); })

static u32 shade4(const u32 *p4, float l) {
    return l < -0.35f ? p4[0] : l < 0.1f ? p4[1] : l < 0.48f ? p4[2] : p4[3];
}

static void gen_tree(Spr *s, int v) {
    *s = mk(30, 36);
    static const u32 P[3][4] = {{0xFF1E5A2E, 0xFF2F7F3A, 0xFF4CA548, 0xFF8AD466},
                                {0xFF245222, 0xFF3A7428, 0xFF62A032, 0xFFA8D050},
                                {0xFF1B4F3A, 0xFF2A7250, 0xFF3E9A62, 0xFF7ACF8E}};
    const u32 *pl = P[v % 3];
    for (int y = 22; y < 36; y++)
        for (int x = 13; x <= 16; x++) {
            u32 c = x == 13 ? 0xFF9A6A40 : x == 16 ? 0xFF4A301C : 0xFF6E4A2A;
            if (hashf(x, y, v) < 0.15f) c = 0xFF5A3A22;
            put(s, x, y, c);
        }
    put(s, 12, 35, 0xFF6E4A2A); put(s, 17, 35, 0xFF4A301C);
    float B[7][3] = {{15, 13, 10}, {8.5f, 16, 7}, {21.5f, 16, 7}, {15, 6.5f, 7.5f}, {9, 9, 5.5f}, {21, 9, 5.5f}, {15, 19, 6}};
    for (int i = 0; i < 7; i++) {
        B[i][0] += (hashf(i, v, 1) - 0.5f) * 3;
        B[i][1] += (hashf(i, v, 2) - 0.5f) * 2;
        B[i][2] += (hashf(i, v, 3) - 0.5f) * 1.5f;
    }
    for (int y = 0; y < 30; y++)
        for (int x = 0; x < 30; x++) {
            float best = -1, nx = 0, ny = 0;
            for (int i = 0; i < 7; i++) {
                float dx = (x + 0.5f - B[i][0]) / B[i][2], dy = (y + 0.5f - B[i][1]) / B[i][2];
                float d = 1 - sqrtf(dx * dx + dy * dy);
                if (d > best) { best = d; nx = dx; ny = dy; }
            }
            if (best < 0) continue;
            if (best < 0.12f && hashf(x, y, v + 9) < 0.5f) continue; /* leafy ragged edge */
            float l = -nx * 0.55f - ny * 0.8f + (vnoise(x * 0.5f, y * 0.5f, v) - 0.5f) * 0.9f + (hashf(x, y, v) - 0.5f) * 0.35f;
            if (y > 20) l -= 0.35f;
            put(s, x, y, shade4(pl, l));
        }
    outline(s, 0xFF142818);
}

static void gen_pine(Spr *s, int v) {
    *s = mk(24, 38);
    static const u32 P[2][4] = {{0xFF163F2E, 0xFF22583C, 0xFF357A4C, 0xFF5AA06A}, {0xFF1A4430, 0xFF285E3A, 0xFF3C8248, 0xFF6AB070}};
    const u32 *pl = P[v % 2];
    for (int y = 30; y < 38; y++)
        for (int x = 11; x <= 12; x++) put(s, x, y, x == 11 ? 0xFF7A5030 : 0xFF4A301C);
    for (int t = 0; t < 4; t++) {
        int y0 = 1 + t * 7, y1 = y0 + 12;
        float maxw = 4 + t * 2.4f;
        for (int y = y0; y <= y1 && y < 33; y++) {
            float f = (y - y0) / (float)(y1 - y0);
            float w = f * maxw + 0.8f;
            for (int x = 0; x < 24; x++) {
                float dx = x + 0.5f - 12;
                if (fabsf(dx) > w) continue;
                if (fabsf(dx) > w - 1 && f > 0.7f && hashf(x, y, v + t) < 0.5f) continue;
                float l = -dx / (w + 0.1f) * 0.7f + (1 - f) * 0.4f - 0.15f + (hashf(x, y, v) - 0.5f) * 0.4f;
                if (y >= y1 - 1) l -= 0.5f;
                put(s, x, y, shade4(pl, l));
            }
        }
    }
    put(s, 12, 0, pl[3]);
    outline(s, 0xFF0E2218);
}

static void gen_bush(Spr *s, int v, bool berries) {
    *s = mk(18, 15);
    static const u32 P[4] = {0xFF244F24, 0xFF356E30, 0xFF4E9440, 0xFF7EC060};
    float B[4][3] = {{9, 8, 6}, {4.5f, 9.5f, 4}, {13.5f, 9.5f, 4}, {9, 4.5f, 4}};
    for (int y = 0; y < 15; y++)
        for (int x = 0; x < 18; x++) {
            float best = -1, nx = 0, ny = 0;
            for (int i = 0; i < 4; i++) {
                float dx = (x + 0.5f - B[i][0]) / B[i][2], dy = (y + 0.5f - B[i][1]) / B[i][2];
                float d = 1 - sqrtf(dx * dx + dy * dy);
                if (d > best) { best = d; nx = dx; ny = dy; }
            }
            if (best < 0 || y > 13) continue;
            float l = -nx * 0.5f - ny * 0.8f + (hashf(x, y, v + 50) - 0.5f) * 0.6f;
            put(s, x, y, shade4(P, l));
        }
    if (berries)
        for (int k = 0; k < 9; k++) {
            int x = 3 + (int)(hashf(k, v, 7) * 11), y = 3 + (int)(hashf(k, v, 8) * 8);
            if (!get(s, x, y) || !get(s, x + 1, y + 1)) continue;
            put(s, x, y, 0xFFE0324A); put(s, x + 1, y, 0xFFB01E36);
            put(s, x, y + 1, 0xFFB01E36); put(s, x + 1, y + 1, 0xFF8A1428);
            put(s, x, y, 0xFFFF9AA8);
        }
    outline(s, 0xFF132A14);
}

static void gen_rock(Spr *s, int v) {
    *s = mk(18, 15);
    static const u32 P[3][4] = {{0xFF5E5E6A, 0xFF82828E, 0xFFA8A8B2, 0xFFD4D4DC},
                                {0xFF6A5E54, 0xFF8E8074, 0xFFB2A496, 0xFFD8CCBE},
                                {0xFF585E66, 0xFF7A828C, 0xFF9EA6B0, 0xFFC8D0D8}};
    const u32 *pl = P[v % 3];
    for (int y = 0; y < 15; y++)
        for (int x = 0; x < 18; x++) {
            float dx = (x + 0.5f - 9) / 8, dy = (y + 0.5f - 8.5f) / 6;
            float ang = atan2f(dy, dx);
            float rr = 0.85f + 0.15f * sinf(ang * 3 + v) + (hashf((int)(ang * 3), v, 4) - 0.5f) * 0.2f;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > rr) continue;
            float facet = vnoise(x * 0.35f + v * 7, y * 0.35f, v) - 0.5f;
            float l = -dx * 0.6f - dy * 0.9f + facet * 1.2f + (hashf(x, y, v) - 0.5f) * 0.2f;
            if (dy > 0.55f) l -= 0.6f;
            put(s, x, y, shade4(pl, l));
        }
    outline(s, 0xFF22222A);
}

static void gen_crop(Spr *s, int stage) {
    *s = mk(14, 12);
    int hts[4] = {2, 4, 6, 8};
    for (int p = 0; p < 3; p++) {
        int bx = 2 + p * 5, h = hts[stage];
        for (int y = 0; y < h; y++) put(s, bx, 11 - y, 0xFF3E8A30);
        for (int y = 1; y < h; y += 2) {
            put(s, bx - 1 - (y > 3), 11 - y, 0xFF5AB84A);
            put(s, bx + 1 + (y > 3), 11 - y - 1, 0xFF6ACC58);
        }
        put(s, bx, 11 - h, 0xFF8EE070);
        if (stage == 3) { put(s, bx, 11, 0xFFF0902A); put(s, bx + 1, 11, 0xFFD07020); put(s, bx - 1, 11, 0xFFF0A040); }
    }
}

int thought_for_feat(int f) {
    switch (f) {
    case F_TREE: case F_PINE: return TH_TREE;
    case F_BUSH: return TH_BUSH;
    case F_ROCK: return TH_ROCK;
    case F_CAMPFIRE: case F_TORCH: return TH_FIRE;
    case F_BED: return TH_BED;
    case F_BLUEPRINT: case F_WALL: case F_DOOR: return TH_HAMMER;
    case F_CROP: return TH_VEG;
    default: return TH_QUESTION;
    }
}
int thought_for_item(int i) {
    switch (i) {
    case I_WOOD: return TH_WOOD;
    case I_STONE: return TH_STONE;
    case I_BERRY: return TH_BERRY;
    case I_VEG: return TH_VEG;
    case I_MEAL: return TH_MEAL;
    default: return TH_QUESTION;
    }
}

static Spr copy(Spr *a) {
    Spr s = mk(a->w, a->h);
    memcpy(s.p, a->p, (size_t)a->w * a->h * 4);
    return s;
}

void sprites_init(void) {
    for (int i = 0; i < 3; i++) gen_tree(&SPR[SP_TREE0 + i], i);
    for (int i = 0; i < 2; i++) gen_pine(&SPR[SP_PINE0 + i], i);
    for (int i = 0; i < 2; i++) { gen_bush(&SPR[SP_BUSH0 + i], i, false); gen_bush(&SPR[SP_BERRYBUSH0 + i], i, true); }
    for (int i = 0; i < 3; i++) gen_rock(&SPR[SP_ROCK0 + i], i);
    for (int i = 0; i < 4; i++) gen_crop(&SPR[SP_CROP0 + i], i);

    SPR[SP_ITEM0 + I_NONE] = mk(1, 1);
    SPR[SP_ITEM0 + I_WOOD] = MAP("..........", "...ooooooo", "..oBBBBBoc", "..obbbbboc", "oooooooooo",
                                 "oBBBBBBoc.", "obbbbbboc.", "obbbbbboc.", "ooooooooo.", "..........");
    SPR[SP_ITEM0 + I_STONE] = MAP("..........", "....ooo...", "...oLLGo..", "..ooLGGoo.", ".oLLGoLLGo",
                                  ".oLGGoLGGo", ".oGGdoGGdo", "..oooooooo", "..........", "..........");
    SPR[SP_ITEM0 + I_BERRY] = MAP("..........", "...rr.....", "..rRrr.p..", ".rrrrppPp.", "..rrpppp..",
                                  ".oooooooo.", ".oNNNNNNo.", "..onnnno..", "...oooo...", "..........");
    SPR[SP_ITEM0 + I_VEG] = MAP("..g..g....", "..gg.gg...", "...g..g...", "..oOo.oOo.", "..oOo.oOo.",
                                "..oOOooOo.", "...oOo.oO.", "...oOo..o.", "....o.....", "..........");
    SPR[SP_ITEM0 + I_MEAL] = MAP("..........", "..s...s...", "...s.s....", ".oooooooo.", "oyYyyyyyyo",
                                 "oNNNNNNNNo", ".onnnnnno.", "..oooooo..", "..........", "..........");

    Spr *T = &SPR[SP_TH0];
    T[TH_NONE] = mk(1, 1);
    T[TH_QUESTION] = MAP("..kkkk...", ".kk..kk..", ".....kk..", "....kk...", "...kk....", "...kk....", ".........", "...kk....", ".........");
    T[TH_EXCLAIM] = MAP("...xx....", "...xx....", "...xx....", "...xx....", "...xx....", ".........", "...xx....", "...xx....", ".........");
    T[TH_BULB] = MAP("...yyy...", "..yyyyy..", ".yyyYyyy.", ".yyyyyyy.", "..yyyyy..", "...yyy...", "...GGG...", "...ddd...", "....d....");
    T[TH_FORGET] = MAP(".........", "..uuu....", ".uuuuuuu.", "uuUuuUuuu", "uuuuuuuuu", ".uuuuuuu.", ".........", "..u.u.u..", ".........");
    T[TH_HEART] = MAP(".........", ".rr...rr.", "rrrr.rrrr", "rRrrrrrrr", ".rrrrrrr.", "..rrrrr..", "...rrr...", "....r....", ".........");
    T[TH_ZZZ] = MAP(".kkkk....", "...k.....", "..k......", ".kkkk....", ".....uuu.", "......u..", ".....uuu.", ".........", ".........");
    T[TH_FOOD] = MAP("....n....", "...ng....", ".rrrrrr..", "rrRrrrrr.", "rRrrrrrr.", "rrrrrrrr.", ".rrrrrr..", "..rr.rr..", ".........");
    T[TH_HUH] = MAP(".........", ".OO..OO..", "O..OO..O.", ".........", ".OO..OO..", "O..OO..O.", ".........", ".........", ".........");
    T[TH_TREE] = MAP("...ggg...", "..ggggg..", ".ggggggg.", ".geggegg.", "..ggggg..", "....n....", "....n....", "...nnn...", ".........");
    T[TH_ROCK] = MAP(".........", ".........", "...GGG...", "..GLGGG..", ".GLGGGGd.", ".GGGGGdd.", "..ddddd..", ".........", ".........");
    T[TH_BUSH] = MAP(".........", "..ggggg..", ".grgggrg.", "ggggrgggg", "grgggggrg", ".ggggggg.", ".........", ".........", ".........");
    T[TH_FIRE] = MAP("....O....", "...OO....", "...OyO...", "..OyyOO..", "..OyYyO..", ".OOyyyOO.", "..nnnnn..", ".n.....n.", ".........");
    T[TH_BED] = MAP(".........", ".........", "ww.......", "wwuuuuuuu", "nuuuuuuun", "nnnnnnnnn", "n.......n", ".........", ".........");
    T[TH_BOX] = MAP(".........", ".nnnnnnn.", ".nNNNNNn.", ".nnnnnnn.", ".nNNNNNn.", ".nNNNNNn.", ".nnnnnnn.", ".........", ".........");
    T[TH_HAMMER] = MAP(".........", ".GGGG....", ".GGGGn...", ".....n...", "......n..", ".......n.", "........n", ".........", ".........");
    T[TH_BOOK] = MAP(".........", ".uuu.uuu.", "uwwwuwwwu", "uwkwuwkwu", "uwwwuwwwu", "uwkwuwkwu", "uuuuuuuuu", ".........", ".........");
    T[TH_WOOD] = copy(&SPR[SP_ITEM0 + I_WOOD]);
    T[TH_STONE] = copy(&SPR[SP_ITEM0 + I_STONE]);
    T[TH_BERRY] = copy(&SPR[SP_ITEM0 + I_BERRY]);
    T[TH_VEG] = copy(&SPR[SP_ITEM0 + I_VEG]);
    T[TH_MEAL] = copy(&SPR[SP_ITEM0 + I_MEAL]);

    Spr *O = &SPR[SP_TOOL0];
    O[TOOL_SELECT] = MAP("w...........", "ww..........", "wkw.........", "wkkw........", "wkkkw.......", "wkkkkw......",
                         "wkkkkkw.....", "wkkkkkkw....", "wkkkwwww....", "wkwkw.......", "ww.wkw......", "....ww......");
    O[TOOL_CHOP] = MAP("............", "....LLL.....", "...LLLLLb...", "..LLLLLbb...", "...LLL.b....", "......b.....",
                       ".....b......", "....b.......", "...b........", "..b.........", ".b..........", "............");
    O[TOOL_MINE] = MAP("............", "..LLLLL.....", ".L.....LL...", "L......bL...", ".......b.L..", "......b...L.",
                       ".....b......", "....b.......", "...b........", "..b.........", ".b..........", "............");
    O[TOOL_HARVEST] = MAP("............", "...gggg.....", "..ggrggg....", ".ggggggrg...", ".grgggggg...", "..gggrgg....",
                          "...gggg.....", "............", "..rr..rr....", ".rRrr.rRr...", ".rrrr.rrr...", "..rr...r....");
    O[TOOL_STOCK] = MAP("............", ".nnnnnnnnnn.", ".nNNNNNNNNn.", ".nnnnnnnnnn.", ".nNNNNNNNNn.", ".nNnNNNNnNn.",
                        ".nNNnNNnNNn.", ".nNNNnnNNNn.", ".nNNnNNnNNn.", ".nnnnnnnnnn.", "............", "............");
    O[TOOL_FARM] = MAP("............", ".....g......", "....ggg.....", "...g.g.g....", ".....g......", ".....g......",
                       "............", "nnnnnnnnnnnn", "bbbbbbbbbbbb", "nnnnnnnnnnnn", "bbbbbbbbbbbb", "............");
    O[TOOL_BUILD] = MAP("............", ".LLLLL......", ".LLLLLL.....", ".LLLLLLb....", "......bb....", ".......b....",
                        "........b...", ".........b..", "..........b.", "...........b", "............", "............");
    O[TOOL_DECON] = MAP("............", ".GGGG.GGGG..", ".dddd.dddd..", "............", "GG.GGGG.GGG.", "dd.dddd.ddd.",
                        "............", ".xx.....xx..", "..xx...xx...", "...xx.xx....", "....xxx.....", "...xx.xx....");
    O[TOOL_CANCEL] = MAP("............", ".xx......xx.", "..xx....xx..", "...xx..xx...", "....xxxx....", ".....xx.....",
                         "....xxxx....", "...xx..xx...", "..xx....xx..", ".xx......xx.", "............", "............");

    Spr *Bd = &SPR[SP_BUILD0];
    Bd[B_WALL] = MAP("LLLLLLLLLLLL", "GGGdGGGGGdGG", "GGGdGGGGGdGG", "dddddddddddd", "GdGGGGGdGGGG", "GdGGGGGdGGGG",
                     "dddddddddddd", "GGGdGGGGGdGG", "GGGdGGGGGdGG", "dddddddddddd", "GdGGGGGdGGGG", "dddddddddddd");
    Bd[B_DOOR] = MAP("..oooooooo..", ".oNNNNNNNNo.", ".oNbNNNNbNo.", ".oNbNNNNbNo.", ".oNbNNNNbNo.", ".oNbNNNNbNo.",
                     ".oNbNNNNbyo.", ".oNbNNNNbNo.", ".oNbNNNNbNo.", ".oNbNNNNbNo.", ".oNNNNNNNNo.", ".oooooooooo.");
    Bd[B_BED] = MAP("............", ".nnnnnnnnnn.", ".nwwwwwwwwn.", ".nwwwwwwwwn.", ".nuuuuuuuun.", ".nuUuuuuuun.",
                    ".nuuuuuuuun.", ".nuuuuuuuun.", ".nuuuuuuuun.", ".nnnnnnnnnn.", ".n........n.", "............");
    Bd[B_CAMPFIRE] = MAP(".....O......", "....OO......", "....OyO.....", "...OyyOO....", "...OyYyO....", "..OOyyyOO...",
                         "..OyyyyyO...", "..nnnnnnn...", ".GnGnGnGnG..", ".GGGGGGGGG..", "............", "............");
    Bd[B_TORCH] = MAP(".....O......", "....OyO.....", "....yYy.....", ".....y......", ".....n......", ".....n......",
                      ".....n......", ".....n......", ".....n......", ".....n......", "....nnn.....", "............");
    Bd[B_FLOOR] = MAP("NNNNNbNNNNNN", "NNNNNbNNNNNN", "bbbbbbbbbbbb", "NNbNNNNNNbNN", "NNbNNNNNNbNN", "bbbbbbbbbbbb",
                      "NNNNNNbNNNNN", "NNNNNNbNNNNN", "bbbbbbbbbbbb", "NbNNNNNNbNNN", "NbNNNNNNbNNN", "bbbbbbbbbbbb");
}
