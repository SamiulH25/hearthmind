// Software renderer: baked organic ground, animated water, y-sorted sprites,
// sun shadows, a coloured light map, emissive fire, and the memory fog.
#include "game.h"

u32 *FB;
int FBW, FBH;
static u32 *ground;
static u8 *wdepth;
static u8 cloud[256 * 256];
static u8 *vign;
static float *LR, *LG, *LB;
static int LW, LH;
static float SINL[1024];
static float T; /* animation time, seconds */
static int CX0, CY0;
static bool MIND;
static Colonist *MC;

#define SINF(v) SINL[(int)((v) * 162.974f) & 1023]

/* ------------------------------------------------------------------ helpers */

static inline void pset(int x, int y, u32 c) {
    if ((unsigned)x < (unsigned)FBW && (unsigned)y < (unsigned)FBH) FB[y * FBW + x] = c;
}
static inline void pblend(int x, int y, u32 c, float a) {
    if ((unsigned)x >= (unsigned)FBW || (unsigned)y >= (unsigned)FBH) return;
    u32 *d = &FB[y * FBW + x];
    *d = RGB((int)lerpf(CR(*d), CR(c), a), (int)lerpf(CG(*d), CG(c), a), (int)lerpf(CB(*d), CB(c), a));
}
static inline void padd(int x, int y, int r, int g, int b) {
    if ((unsigned)x >= (unsigned)FBW || (unsigned)y >= (unsigned)FBH) return;
    u32 *d = &FB[y * FBW + x];
    *d = RGB(CR(*d) + r, CG(*d) + g, CB(*d) + b);
}
static void rect(int x, int y, int w, int h, u32 c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) pset(i, j, c);
}
static void rect_blend(int x, int y, int w, int h, u32 c, float a) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) pblend(i, j, c, a);
}
static void glow(int cx, int cy, int R, int r, int g, int b) {
    for (int y = -R; y <= R; y++)
        for (int x = -R; x <= R; x++) {
            float d = sqrtf((float)(x * x + y * y)) / R;
            if (d >= 1) continue;
            float f = (1 - d) * (1 - d);
            padd(cx + x, cy + y, (int)(r * f), (int)(g * f), (int)(b * f));
        }
}
static void shadow(int cx, int cy, int rx, int ry, float a) {
    if (rx <= 0 || ry <= 0) return;
    for (int y = -ry; y <= ry; y++)
        for (int x = -rx; x <= rx; x++) {
            float d = (x * x) / (float)(rx * rx) + (y * y) / (float)(ry * ry);
            if (d >= 1) continue;
            int px = cx + x, py = cy + y;
            if ((unsigned)px >= (unsigned)FBW || (unsigned)py >= (unsigned)FBH) continue;
            u32 *p = &FB[py * FBW + px];
            *p = col_mul(*p, 1 - a * (1 - d));
        }
}

static void blit(Spr *s, int x, int y, bool flip, bool ghost) {
    for (int j = 0; j < s->h; j++) {
        int py = y + j;
        if ((unsigned)py >= (unsigned)FBH) continue;
        for (int i = 0; i < s->w; i++) {
            u32 c = s->p[j * s->w + (flip ? s->w - 1 - i : i)];
            if (!(c >> 24)) continue;
            int px = x + i;
            if ((unsigned)px >= (unsigned)FBW) continue;
            if (ghost) pblend(px, py, RGB(120 + CR(c) / 4, 200 + CG(c) / 8, 255), 0.55f);
            else FB[py * FBW + px] = c;
        }
    }
}
static void blit_sway(Spr *s, int x, int y, float sway, int trunk_rows) {
    for (int j = 0; j < s->h; j++) {
        float f = 1 - j / (float)(s->h - trunk_rows);
        int off = j < s->h - trunk_rows ? (int)roundf(sway * f * f) : 0;
        int py = y + j;
        if ((unsigned)py >= (unsigned)FBH) continue;
        for (int i = 0; i < s->w; i++) {
            u32 c = s->p[j * s->w + i];
            if (!(c >> 24)) continue;
            pset(x + i + off, py, c);
        }
    }
}

/* ------------------------------------------------------------------ ground */

static float bil(const float *f, float fx, float fy) {
    fx -= 0.5f; fy -= 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    int x1 = clampi(x0 + 1, 0, MAP_W - 1), y1 = clampi(y0 + 1, 0, MAP_H - 1);
    x0 = clampi(x0, 0, MAP_W - 1); y0 = clampi(y0, 0, MAP_H - 1);
    return lerpf(lerpf(f[IDX(x0, y0)], f[IDX(x1, y0)], tx), lerpf(f[IDX(x0, y1)], f[IDX(x1, y1)], tx), ty);
}
static float bilu8(const u8 *f, float fx, float fy) {
    fx -= 0.5f; fy -= 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    int x1 = clampi(x0 + 1, 0, MAP_W - 1), y1 = clampi(y0 + 1, 0, MAP_H - 1);
    x0 = clampi(x0, 0, MAP_W - 1); y0 = clampi(y0, 0, MAP_H - 1);
    return lerpf(lerpf(f[IDX(x0, y0)], f[IDX(x1, y0)], tx), lerpf(f[IDX(x0, y1)], f[IDX(x1, y1)], tx), ty);
}

static void ground_pixel(int px, int py) {
    int tx = px / TS, ty = py / TS, lx = px % TS, ly = py % TS;
    Tile *t = &G.tiles[IDX(tx, ty)];
    u32 s = G.seed;
    float hn = hashf(px, py, s + 11);
    u32 col;
    int depth = 0;
    if (t->flags & FL_FLOOR) {
        int row = ly / 4;
        float pk = 0.88f + hashf(tx * 4 + row, ty, s + 5) * 0.2f;
        int seam = (row * 7 + (int)(hash2(tx, ty + row, s) % 16)) % 16;
        col = RGB((int)(170 * pk), (int)(124 * pk), (int)(76 * pk));
        if (ly % 4 == 3 || lx == seam) col = RGB(98, 66, 40);
        else col = col_mul(col, 0.94f + hn * 0.1f + (vnoise(px * 0.9f, py * 0.12f, s) - 0.5f) * 0.12f);
        bool l = tx > 0 && (G.tiles[IDX(tx - 1, ty)].flags & FL_FLOOR), r = tx < MAP_W - 1 && (G.tiles[IDX(tx + 1, ty)].flags & FL_FLOOR);
        bool u = ty > 0 && (G.tiles[IDX(tx, ty - 1)].flags & FL_FLOOR), d = ty < MAP_H - 1 && (G.tiles[IDX(tx, ty + 1)].flags & FL_FLOOR);
        if ((lx == 0 && !l) || (lx == 15 && !r) || (ly == 0 && !u) || (ly == 15 && !d)) col = RGB(76, 52, 32);
    } else if (t->flags & FL_FARM) {
        int k = ly % 4;
        col = k == 0 ? RGB(66, 44, 28) : k == 1 ? RGB(122, 86, 56) : RGB(96, 66, 42);
        col = col_mul(col, 0.92f + hn * 0.16f);
        if (lx == 0 || lx == 15 || ly == 0 || ly == 15) col = RGB(78, 54, 34);
    } else {
        float fx = px / (float)TS, fy = py / (float)TS;
        float h = bil(G.height, fx, fy) + (vnoise(px * 0.12f, py * 0.12f, s + 3) - 0.5f) * 0.02f;
        if (t->terrain != T_WATER && h < 0.366f) {
            float dx = lx - 7.5f, dy = ly - 7.5f, d = sqrtf(dx * dx + dy * dy);
            if (d < 7.5f + (vnoise(px * 0.3f, py * 0.3f, s + 7) - 0.5f) * 4) h = 0.366f + hn * 0.004f;
        }
        float m = bil(G.moist, fx, fy);
        if (h < 0.36f) {
            depth = clampi((int)((0.36f - h) / 0.14f * 255) + 1, 1, 255);
            col = 0;
        } else if (h < 0.405f) {
            float wet = clampf((0.378f - h) / 0.012f, 0, 1);
            col = col_lerp(RGB(222, 202, 146), RGB(170, 150, 106), wet);
            col = col_mul(col, 0.94f + hn * 0.1f);
            if (hn > 0.985f) col = RGB(240, 230, 200);
        } else {
            float dry = clampf((0.64f - m) * 1.7f, 0, 1);
            u32 lush = RGB(58, 124, 56), dryc = RGB(140, 156, 74);
            col = col_lerp(lush, dryc, dry);
            float patch = vnoise(px * 0.035f, py * 0.035f, s + 21);
            col = col_mul(col, 0.86f + patch * 0.24f);
            if (hn < 0.07f) col = col_mul(col, 0.78f);
            else if (hn > 0.965f) col = col_mul(col, 1.18f);
            float fl = vnoise(px * 0.025f, py * 0.025f, s + 31);
            if (fl > 0.6f && hashf(px, py, s + 41) > 0.975f) {
                static const u32 FL[5] = {0xFFFFFFFF, 0xFFFFE066, 0xFFFF8FB8, 0xFF9FC8FF, 0xFFE0A0FF};
                col = FL[hash2(px / 3, py / 3, s) % 5];
            }
            if (h < 0.42f) col = col_lerp(RGB(200, 190, 130), col, (h - 0.405f) / 0.015f);
            if (h > 0.72f) {
                float r = clampf((h - 0.72f) / 0.04f, 0, 1);
                u32 rock = col_mul(RGB(128, 124, 114), 0.9f + hn * 0.15f);
                if (hn < 0.05f) rock = RGB(92, 88, 82);
                col = col_lerp(col, rock, r);
            }
            float w = bilu8(G.wear, fx, fy) / 255.0f * 1.7f + (vnoise(px * 0.3f, py * 0.3f, s + 51) - 0.5f) * 0.35f;
            if (w > 0.45f) {
                u32 dirt = col_mul(RGB(146, 114, 76), 0.9f + hn * 0.18f);
                col = col_lerp(col, dirt, clampf((w - 0.45f) * 5, 0, 0.95f));
            }
        }
    }
    ground[py * WORLD_PW + px] = col;
    wdepth[py * WORLD_PW + px] = (u8)depth;
}

void render_ground_build(void) {
    for (int py = 0; py < WORLD_PH; py++)
        for (int px = 0; px < WORLD_PW; px++) ground_pixel(px, py);
}

void ground_repaint(int tx, int ty) {
    for (int y = (ty - 1) * TS; y < (ty + 2) * TS; y++)
        for (int x = (tx - 1) * TS; x < (tx + 2) * TS; x++)
            if (x >= 0 && y >= 0 && x < WORLD_PW && y < WORLD_PH) ground_pixel(x, y);
}

void render_init(void) {
    ground = malloc((size_t)WORLD_PW * WORLD_PH * 4);
    wdepth = malloc((size_t)WORLD_PW * WORLD_PH);
    for (int i = 0; i < 1024; i++) SINL[i] = sinf(i * 6.2831853f / 1024);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            /* tileable cloud noise */
            float v = 0, a = 0.5f, n = 0;
            for (int o = 0; o < 4; o++) {
                int p = 8 << o;
                float fx = x * p / 256.0f, fy = y * p / 256.0f;
                int x0 = (int)fx, y0 = (int)fy;
                float tx = fx - x0, ty = fy - y0;
                tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
                float h00 = hashf(x0 % p, y0 % p, o), h10 = hashf((x0 + 1) % p, y0 % p, o);
                float h01 = hashf(x0 % p, (y0 + 1) % p, o), h11 = hashf((x0 + 1) % p, (y0 + 1) % p, o);
                v += lerpf(lerpf(h00, h10, tx), lerpf(h01, h11, tx), ty) * a;
                n += a; a *= 0.5f;
            }
            cloud[y * 256 + x] = (u8)clampi((int)((v / n - 0.5f) * 2.2f * 255 + 128), 0, 255);
        }
}

void render_resize(int w, int h) {
    FBW = (w + G.scale - 1) / G.scale;
    FBH = (h + G.scale - 1) / G.scale;
    free(FB); free(vign); free(LR); free(LG); free(LB);
    FB = malloc((size_t)FBW * FBH * 4);
    vign = malloc((size_t)FBW * FBH);
    for (int y = 0; y < FBH; y++)
        for (int x = 0; x < FBW; x++) {
            float dx = (x - FBW / 2.0f) / (FBW / 2.0f), dy = (y - FBH / 2.0f) / (FBH / 2.0f);
            float d = dx * dx * 0.7f + dy * dy * 0.9f;
            vign[y * FBW + x] = (u8)clampi((int)(255 * (1 - 0.42f * d * d)), 120, 255);
        }
    LW = FBW / 4 + 3;
    LH = FBH / 4 + 3;
    LR = malloc(sizeof(float) * LW * LH);
    LG = malloc(sizeof(float) * LW * LH);
    LB = malloc(sizeof(float) * LW * LH);
}

/* ------------------------------------------------------------------ view source */

typedef struct { u8 feat, aux, flags, item, icount, str; } View;
static View vget(int x, int y) {
    View v = {0};
    if (!inb(x, y)) return v;
    int i = IDX(x, y);
    if (MIND) {
        MemCell *m = &MC->mem[i];
        v = (View){m->feat, m->aux, m->flags, m->item, m->icount, m->str};
    } else {
        Tile *t = &G.tiles[i];
        v = (View){t->feat, t->aux, t->flags, t->item, t->feat == F_BLUEPRINT ? t->bpmat : t->icount, 255};
    }
    return v;
}

/* ------------------------------------------------------------------ lighting model */

static void ambient(float h, float *r, float *g, float *b) {
    static const float K[][4] = {{0, .20f, .24f, .46f}, {4.6f, .20f, .24f, .46f}, {6.0f, .95f, .60f, .55f}, {7.6f, 1, .96f, .90f},
                                 {16.5f, 1, .97f, .90f}, {18.6f, 1.0f, .62f, .44f}, {20.0f, .48f, .36f, .62f}, {21.4f, .20f, .24f, .46f},
                                 {24, .20f, .24f, .46f}};
    int i = 0;
    while (K[i + 1][0] < h) i++;
    float t = (h - K[i][0]) / (K[i + 1][0] - K[i][0]);
    *r = lerpf(K[i][1], K[i + 1][1], t);
    *g = lerpf(K[i][2], K[i + 1][2], t);
    *b = lerpf(K[i][3], K[i + 1][3], t);
}
static float sun_strength(float h) {
    if (h < 5.5f || h > 20.5f) return 0;
    if (h < 7) return (h - 5.5f) / 1.5f;
    if (h > 19) return (20.5f - h) / 1.5f;
    return 1;
}

/* ------------------------------------------------------------------ colonists */

static const char *CBODY[11] = {"..ooooo..", ".ohhhhho.", "ohhhhhhho", "ohhhsssso", "ohhssseso", ".ohsssso.",
                                "..oosoo..", ".otttttto", "ottttttTo", "osttttTso", ".oTTTTTo."};
static const char *CLEGS[3][4] = {{"..opppo..", "..opopo..", "..obobo..", "..oo.oo.."},
                                  {"..opppo..", ".opo.opo.", ".obo.obo.", ".oo...oo."},
                                  {"..opppo..", "...opo...", "...obo...", "...ooo..."}};

static u32 ccol(Colonist *c, char ch, int row, int col) {
    u32 hair = c->hairstyle == 2 && row <= 2 ? 0xFF8A5A34 : c->hair;
    (void)col;
    switch (ch) {
    case 'o': return 0xFF1E161A;
    case 'h': return hair;
    case 's': return c->skin;
    case 'e': return 0xFF1E1E2A;
    case 't': return c->shirt;
    case 'T': return col_mul(c->shirt, 0.72f);
    case 'p': return c->pants;
    case 'b': return 0xFF3C281C;
    default: return 0;
    }
}

void draw_colonist_to(u32 *buf, int bw, int bh, int ox, int oy, Colonist *c, int frame, bool lying) {
    int rows = frame == 3 ? 7 : 15;
    for (int r = 0; r < rows; r++) {
        const char *line = r < 11 ? CBODY[r] : CLEGS[frame == 3 ? 0 : frame][r - 11];
        for (int k = 0; k < 9; k++) {
            char ch = line[k];
            if (c->hairstyle == 1 && k == 1 && r >= 5 && r <= 8) ch = 'h';
            if (c->hairstyle == 1 && k == 0 && r >= 5 && r <= 7) ch = 'o';
            u32 col = ccol(c, ch, r, k);
            if (!col) continue;
            int kk = c->dir < 0 ? 8 - k : k;
            int x = lying ? ox + r : ox + kk, y = lying ? oy + kk : oy + r;
            if ((unsigned)x < (unsigned)bw && (unsigned)y < (unsigned)bh) buf[y * bw + x] = col;
        }
    }
}

static void draw_colonist(Colonist *c) {
    int fx = (int)roundf(c->x * TS) - CX0, fy = (int)roundf(c->y * TS) - CY0 + 4;
    if (c->state == S_SLEEP) {
        if (!c->sleep_ground) {
            /* in bed: head on the pillow, blanket drawn by the bed */
            draw_colonist_to(FB, FBW, FBH, fx - 4, fy - 12, c, 3, false);
            rect(fx - 6, fy - 6, 12, 8, c->shirt);
            rect(fx - 6, fy - 6, 12, 1, col_mul(c->shirt, 1.25f));
        } else {
            shadow(fx, fy, 8, 3, 0.35f);
            draw_colonist_to(FB, FBW, FBH, fx - 7, fy - 6, c, 0, true);
        }
        return;
    }
    shadow(fx, fy, 5, 2, 0.4f);
    bool moving = c->state == S_MOVE;
    int frame = moving ? ((int)(c->anim * 4) % 4 == 1 ? 1 : (int)(c->anim * 4) % 4 == 3 ? 2 : 0) : 0;
    int bob = moving && frame ? -1 : 0;
    int ox = fx - 4, oy = fy - 15 + bob;
    draw_colonist_to(FB, FBW, FBH, ox, oy, c, frame, false);
    if (c->state == S_WORK && c->workanim) {
        int ph = (c->workanim / 7) % 2;
        int hx = c->dir > 0 ? ox + 8 : ox;
        u32 tool = (c->job == J_CHOP || c->job == J_MINE) ? 0xFFB8B8C4 : 0xFF8A5A30;
        if (ph) { pset(hx + c->dir, oy + 6, c->skin); pset(hx + c->dir * 2, oy + 4, tool); pset(hx + c->dir * 2, oy + 3, tool); pset(hx + c->dir * 3, oy + 3, tool); }
        else { pset(hx + c->dir, oy + 9, c->skin); pset(hx + c->dir * 2, oy + 9, tool); pset(hx + c->dir * 3, oy + 10, tool); pset(hx + c->dir * 3, oy + 11, tool); }
    }
    if (c->carry) blit(&SPR[SP_ITEM0 + c->carry], fx - 5, oy - 9, false, false);
}

/* ------------------------------------------------------------------ features */

static void draw_wall(int x, int y, int sx, int sy, bool ghost, bool door) {
    View b = vget(x, y + 1), a = vget(x, y - 1), l = vget(x - 1, y), r = vget(x + 1, y);
    bool wb = b.feat == F_WALL || b.feat == F_DOOR, wa = a.feat == F_WALL || a.feat == F_DOOR;
    bool wl = l.feat == F_WALL || l.feat == F_DOOR, wr = r.feat == F_WALL || r.feat == F_DOOR;
    u32 s = G.seed;
    for (int j = -8; j < 8; j++)
        for (int i = 0; i < 16; i++) {
            int wx = x * 16 + i, wy = y * 16 + j;
            u32 c;
            if (door) {
                c = (i % 5 == 0) ? RGB(96, 62, 34) : col_mul(RGB(156, 106, 60), 0.9f + hashf(i / 5, x, s) * 0.2f);
                if (j == -8 || j == 7) c = RGB(80, 52, 30);
            } else {
                int cell = (int)(hash2(wx / 8, (wy + 64) / 8, s) % 100);
                c = col_mul(RGB(182, 176, 166), 0.9f + cell * 0.0012f + hashf(wx, wy, s) * 0.06f);
                if ((wx % 8 == 0) || ((wy + 64) % 8 == 0)) c = col_mul(c, 0.88f);
                if (j == -8 && !wa) c = RGB(214, 208, 198);
            }
            if ((j == -8 && !wa) || (i == 0 && !wl) || (i == 15 && !wr)) c = RGB(38, 34, 38);
            if (ghost) pblend(sx + i, sy + j, RGB(130, 210, 255), 0.5f);
            else pset(sx + i, sy + j, c);
        }
    if (!wb)
        for (int j = 8; j < 16; j++)
            for (int i = 0; i < 16; i++) {
                int wx = x * 16 + i;
                int row = (j - 8) / 4;
                u32 c;
                if (door) {
                    c = (i % 4 == 0) ? RGB(70, 44, 26) : RGB(122, 80, 44);
                    if (i == 12 && j == 11) c = RGB(230, 200, 90);
                } else {
                    bool mortar = ((j - 8) % 4 == 3) || ((wx + row * 4) % 8 == 0);
                    c = mortar ? RGB(86, 80, 76) : col_mul(RGB(132, 124, 116), 0.9f + hashf((wx + row * 4) / 8, y * 4 + row, s) * 0.2f);
                }
                if (j == 15 || (i == 0 && !wl) || (i == 15 && !wr)) c = RGB(30, 26, 30);
                if (ghost) pblend(sx + i, sy + j, RGB(90, 170, 230), 0.5f);
                else pset(sx + i, sy + j, c);
            }
}

static void draw_bed(int sx, int sy, int owner, bool ghost, bool occupied) {
    if (ghost) { blit(&SPR[SP_BUILD0 + B_BED], sx + 2, sy + 2, false, true); return; }
    shadow(sx + 8, sy + 15, 8, 2, 0.35f);
    rect(sx + 1, sy, 14, 16, RGB(60, 38, 22));
    rect(sx + 2, sy + 1, 12, 14, RGB(128, 84, 46));
    rect(sx + 3, sy + 2, 10, 4, RGB(236, 232, 222));
    rect(sx + 3, sy + 5, 10, 1, RGB(200, 196, 186));
    if (!occupied) {
        u32 bl = owner > 0 && owner <= G.ncol ? G.col[owner - 1].shirt : RGB(80, 110, 170);
        rect(sx + 2, sy + 6, 12, 9, bl);
        rect(sx + 2, sy + 6, 12, 2, col_mul(bl, 1.25f));
        rect(sx + 2, sy + 14, 12, 1, col_mul(bl, 0.7f));
    }
}

static void draw_campfire(int sx, int sy, bool ghost) {
    int cx = sx + 8, cy = sy + 10;
    if (ghost) { blit(&SPR[SP_BUILD0 + B_CAMPFIRE], sx + 2, sy + 2, false, true); return; }
    shadow(cx, cy + 1, 8, 4, 0.3f);
    rect(cx - 3, cy - 1, 7, 3, RGB(40, 34, 32));
    for (int k = 0; k < 9; k++) {
        float a = k * 6.2831f / 9;
        int x = cx + (int)roundf(cosf(a) * 6.5f), y = cy + (int)roundf(sinf(a) * 3.8f);
        rect(x - 1, y - 1, 3, 2, RGB(120, 116, 112));
        pset(x - 1, y - 1, RGB(170, 166, 160));
        pset(x + 1, y, RGB(70, 66, 64));
    }
    for (int i = -4; i <= 4; i++) { pset(cx + i, cy - 1 + i / 3, RGB(110, 70, 40)); pset(cx + i, cy + 1 - i / 3, RGB(90, 58, 34)); }
}

static void draw_torch(int sx, int sy, bool ghost) {
    if (ghost) { blit(&SPR[SP_BUILD0 + B_TORCH], sx + 2, sy + 2, false, true); return; }
    shadow(sx + 8, sy + 13, 3, 1, 0.35f);
    rect(sx + 7, sy - 4, 2, 17, RGB(110, 72, 40));
    rect(sx + 7, sy - 4, 1, 17, RGB(150, 100, 58));
    rect(sx + 6, sy - 6, 4, 3, RGB(60, 50, 46));
}

static void sun_offset(float *dx, float *a) {
    float h = hour_of_day(), st = sun_strength(h);
    float t = clampf((h - 6) / 12, 0, 1);
    *dx = (t - 0.5f) * 14;
    *a = 0.12f + 0.2f * st;
}

static void draw_feature(int x, int y, View v, bool ghostmode) {
    int sx = x * TS - CX0, sy = y * TS - CY0;
    int jit = 0;
    if (MIND && v.str < 90) jit = (int)roundf(SINF(T * 3 + x * 1.7f + y) * (90 - v.str) / 40.0f);
    sx += jit;
    float sdx, sa;
    sun_offset(&sdx, &sa);
    u32 h = hash2(x, y, G.seed);
    switch (v.feat) {
    case F_TREE: case F_PINE: {
        Spr *s = v.feat == F_TREE ? &SPR[SP_TREE0 + h % 3] : &SPR[SP_PINE0 + h % 2];
        shadow(sx + 8 + (int)(sdx * 0.6f), sy + 14, 9 + (int)fabsf(sdx * 0.5f), 4, sa + 0.08f);
        float sway = (SINF(T * 1.3f + x * 0.7f + y * 1.3f) + 0.5f * SINF(T * 2.9f + x)) * 1.1f;
        blit_sway(s, sx + 8 - s->w / 2, sy + 15 - s->h, sway, 10);
        if (ghostmode) blit(s, sx + 8 - s->w / 2, sy + 15 - s->h, false, true);
        break;
    }
    case F_BUSH: {
        Spr *s = &SPR[(v.aux > 0 ? SP_BERRYBUSH0 : SP_BUSH0) + h % 2];
        shadow(sx + 8 + (int)(sdx * 0.3f), sy + 13, 8, 3, sa);
        blit(s, sx + 8 - s->w / 2, sy + 15 - s->h, h & 4, false);
        break;
    }
    case F_ROCK: {
        Spr *s = &SPR[SP_ROCK0 + h % 3];
        shadow(sx + 8 + (int)(sdx * 0.3f), sy + 13, 8, 3, sa);
        blit(s, sx + 8 - s->w / 2, sy + 15 - s->h, h & 8, false);
        break;
    }
    case F_WALL: draw_wall(x, y, sx, sy, false, false); break;
    case F_DOOR: draw_wall(x, y, sx, sy, false, true); break;
    case F_BED: {
        bool occ = false;
        for (int i = 0; i < G.ncol && !MIND; i++)
            if (G.col[i].alive && G.col[i].state == S_SLEEP && !G.col[i].sleep_ground && G.col[i].tx == x && G.col[i].ty == y) occ = true;
        draw_bed(sx, sy, v.aux, false, occ);
        break;
    }
    case F_CAMPFIRE: draw_campfire(sx, sy, false); break;
    case F_TORCH: draw_torch(sx, sy, false); break;
    case F_CROP: blit(&SPR[SP_CROP0 + clampi(v.aux, 0, 3)], sx + 1, sy + 3, false, false); break;
    case F_BLUEPRINT: {
        int b = v.aux;
        if (b == B_WALL) draw_wall(x, y, sx, sy, true, false);
        else if (b == B_DOOR) draw_wall(x, y, sx, sy, true, true);
        else if (b == B_FLOOR) blit(&SPR[SP_BUILD0 + B_FLOOR], sx + 2, sy + 2, false, true);
        else if (b == B_BED) draw_bed(sx, sy, 0, true, false);
        else if (b == B_CAMPFIRE) draw_campfire(sx, sy, true);
        else draw_torch(sx, sy, true);
        break;
    }
    }
}

static void draw_items(int x, int y, View v) {
    if (!v.item || !v.icount || v.feat == F_BLUEPRINT) return;
    int sx = x * TS - CX0, sy = y * TS - CY0;
    Spr *s = &SPR[SP_ITEM0 + v.item];
    shadow(sx + 8, sy + 13, 5, 2, 0.3f);
    if (v.icount > 13) blit(s, sx + 1, sy + 1, true, false);
    if (v.icount > 6) blit(s, sx + 5, sy + 2, false, false);
    blit(s, sx + 3, sy + 4, false, false);
}

/* ------------------------------------------------------------------ emissive */

static void draw_flame(int cx, int cy, float size, int seed) {
    int W = (int)(size * 1.2f) + 1, H = (int)(size * 2.6f);
    for (int y = -H; y <= 0; y++)
        for (int x = -W; x <= W; x++) {
            float f = -y / (float)H;
            float n = vnoise(x * 0.35f + seed, y * 0.3f + T * 7, seed);
            float w = (1 - f) * size * (0.55f + 0.6f * n);
            float xs = x + SINF(T * 5 + y * 0.4f + seed) * f * 1.5f;
            if (fabsf(xs) > w) continue;
            float core = 1 - fabsf(xs) / (w + 0.01f);
            float heat = core * (1 - f) * 1.4f + n * 0.3f;
            int r, g, b;
            if (heat > 0.9f) { r = 255; g = 245; b = 190; }
            else if (heat > 0.55f) { r = 255; g = 190; b = 70; }
            else if (heat > 0.3f) { r = 250; g = 120; b = 30; }
            else { r = 190; g = 50; b = 20; }
            padd(cx + x, cy + y, r, g, b);
        }
}

/* ------------------------------------------------------------------ overlays */

static void bubble(int cx, int by, int th) {
    Spr *s = &SPR[SP_TH0 + th];
    int w = s->w + 4, h = s->h + 4, x = cx - w / 2, y = by - h;
    rect(x + 1, y, w - 2, h, 0xFFF8F4EC);
    rect(x, y + 1, w, h - 2, 0xFFF8F4EC);
    for (int i = 1; i < w - 1; i++) { pset(x + i, y - 1, 0xFF2A2430); pset(x + i, y + h, 0xFF2A2430); }
    for (int j = 1; j < h - 1; j++) { pset(x - 1, y + j, 0xFF2A2430); pset(x + w, y + j, 0xFF2A2430); }
    pset(x, y, 0xFF2A2430); pset(x + w - 1, y, 0xFF2A2430); pset(x, y + h - 1, 0xFF2A2430); pset(x + w - 1, y + h - 1, 0xFF2A2430);
    pset(cx, y + h, 0xFFF8F4EC); pset(cx - 1, y + h, 0xFF2A2430); pset(cx + 1, y + h, 0xFF2A2430);
    pset(cx, y + h + 1, 0xFF2A2430);
    blit(s, x + 2, y + 2, false, false);
}

static void dashed_rect(int x, int y, int w, int h, u32 c, float a) {
    int ph = (int)(T * 10);
    for (int i = 0; i < w; i++) {
        if (((i + ph) / 3) % 2) { pblend(x + i, y, c, a); pblend(x + w - 1 - i, y + h - 1, c, a); }
    }
    for (int j = 0; j < h; j++) {
        if (((j + ph) / 3) % 2) { pblend(x, y + h - 1 - j, c, a); pblend(x + w - 1, y + j, c, a); }
    }
}

/* ------------------------------------------------------------------ frame */

typedef struct { int key, kind, a; } DrawE;
static int cmp_draw(const void *a, const void *b) { return ((const DrawE *)a)->key - ((const DrawE *)b)->key; }

void render_frame(void) {
    T = SDL_GetTicks() / 1000.0f;
    CX0 = (int)floorf(G.camx - FBW / 2.0f);
    CY0 = (int)floorf(G.camy - FBH / 2.0f);
    MIND = G.mindview && G.sel >= 0 && G.col[G.sel].alive;
    MC = MIND ? &G.col[G.sel] : NULL;
    float hr = hour_of_day();

    /* 1. ground + water */
    for (int y = 0; y < FBH; y++) {
        int wy = y + CY0;
        u32 *row = FB + y * FBW;
        if (wy < 0 || wy >= WORLD_PH) { for (int x = 0; x < FBW; x++) row[x] = 0xFF0A0C14; continue; }
        const u32 *gr = ground + (size_t)wy * WORLD_PW;
        const u8 *wd = wdepth + (size_t)wy * WORLD_PW;
        for (int x = 0; x < FBW; x++) {
            int wx = x + CX0;
            if (wx < 0 || wx >= WORLD_PW) { row[x] = 0xFF0A0C14; continue; }
            int d = wd[wx];
            if (!d) { row[x] = gr[wx]; continue; }
            float df = d / 255.0f;
            float sd = sqrtf(df);
            int r = (int)lerpf(70, 16, sd), g = (int)lerpf(170, 54, sd), b = (int)lerpf(178, 112, sd);
            float w = SINF(wx * 0.21f + T * 1.3f + SINF(wy * 0.13f + T * 0.7f) * 2) + SINF(wy * 0.27f - T * 1.1f + SINF(wx * 0.09f) * 2);
            if (w > 1.35f) { float k = (w - 1.35f) * 70; r += (int)(k * 0.6f); g += (int)k; b += (int)k; }
            else if (w < -1.3f) { r -= 8; g -= 10; b -= 8; }
            float foam = 0.07f + 0.035f * SINF(T * 1.6f + wx * 0.05f + wy * 0.04f);
            if (df < foam) {
                float k = 1 - df / foam;
                r = (int)lerpf(r, 226, k * 0.8f); g = (int)lerpf(g, 240, k * 0.8f); b = (int)lerpf(b, 236, k * 0.8f);
            }
            row[x] = RGB(r, g, b);
        }
    }

    int tx0 = clampi(CX0 / TS - 2, 0, MAP_W - 1), tx1 = clampi((CX0 + FBW) / TS + 2, 0, MAP_W - 1);
    int ty0 = clampi(CY0 / TS - 1, 0, MAP_H - 1), ty1 = clampi((CY0 + FBH) / TS + 4, 0, MAP_H - 1);

    /* 2. flat things: stockpile tint, items */
    for (int y = ty0; y <= ty1; y++)
        for (int x = tx0; x <= tx1; x++) {
            View v = vget(x, y);
            if (MIND && !v.str) continue;
            if (v.flags & FL_STOCK) rect_blend(x * TS - CX0, y * TS - CY0, TS, TS, 0xFFE8D8A0, 0.1f);
            draw_items(x, y, v);
        }

    /* 3. y-sorted objects & people */
    static DrawE list[20000];
    int n = 0;
    for (int y = ty0; y <= ty1; y++)
        for (int x = tx0; x <= tx1; x++) {
            View v = vget(x, y);
            if (!v.feat || (MIND && !v.str)) continue;
            if (n < 19000) list[n++] = (DrawE){(y * TS + 15) * 4 + (v.feat == F_BED || v.feat == F_CROP ? 0 : 2), 0, IDX(x, y)};
        }
    for (int i = 0; i < G.ncol; i++) {
        Colonist *c = &G.col[i];
        if (!c->alive) continue;
        if (MIND && c != MC && !sees_now(MC, c->tx, c->ty)) continue;
        list[n++] = (DrawE){(int)(c->y * TS + 4) * 4 + (c->state == S_SLEEP && !c->sleep_ground ? 1 : 3), 1, i};
    }
    qsort(list, n, sizeof *list, cmp_draw);
    for (int i = 0; i < n; i++) {
        if (list[i].kind == 0) {
            int x = list[i].a % MAP_W, y = list[i].a / MAP_W;
            draw_feature(x, y, vget(x, y), false);
        } else draw_colonist(&G.col[list[i].a]);
    }

    /* 4. non-emissive particles */
    for (int i = 0; i < G.npart; i++) {
        Particle *p = &G.part[i];
        int x = (int)p->x - CX0, y = (int)p->y - CY0;
        float f = p->life / p->max;
        switch (p->type) {
        case P_SMOKE: {
            int r = p->size + (int)((1 - f) * 4);
            for (int j = -r; j <= r; j++)
                for (int k = -r; k <= r; k++)
                    if (j * j + k * k <= r * r) pblend(x + k, y + j, p->col, 0.22f * f);
            break;
        }
        case P_STEAM: pblend(x, y, p->col, 0.5f * f); pblend(x + 1, y, p->col, 0.3f * f); break;
        case P_CHIP: case P_LEAF: pset(x, y, p->col); break;
        case P_DUST: pblend(x, y, p->col, 0.6f * f); break;
        default: break;
        }
    }

    /* 5. lighting */
    float ar, ag, ab;
    ambient(hr, &ar, &ag, &ab);
    if (MIND) { ar = ag = ab = 1; }
    float dayl = sun_strength(hr);
    for (int i = 0; i < LW * LH; i++) { LR[i] = ar; LG[i] = ag; LB[i] = ab; }
    if (!MIND)
        for (int li = 0; li < G.nlights; li++) {
            Light *l = &G.lights[li];
            float lx = l->x * TS - CX0, ly = l->y * TS - CY0 - 4, R = l->r * TS;
            if (lx < -R || ly < -R || lx > FBW + R || ly > FBH + R) continue;
            float fl = 1 + 0.07f * SINF(T * 9 + li) + 0.05f * SINF(T * 23 + li * 3);
            float k = l->inten * fl * (1 - dayl * 0.75f);
            int gx0 = clampi((int)((lx - R) / 4), 0, LW - 1), gx1 = clampi((int)((lx + R) / 4) + 1, 0, LW - 1);
            int gy0 = clampi((int)((ly - R) / 4), 0, LH - 1), gy1 = clampi((int)((ly + R) / 4) + 1, 0, LH - 1);
            for (int gy = gy0; gy <= gy1; gy++)
                for (int gx = gx0; gx <= gx1; gx++) {
                    float dx = gx * 4 - lx, dy = (gy * 4 - ly) * 1.15f;
                    float d = sqrtf(dx * dx + dy * dy) / (R * fl);
                    if (d >= 1) continue;
                    float f = (1 - d) * (1 - d) * k;
                    LR[gy * LW + gx] += l->cr * f;
                    LG[gy * LW + gx] += l->cg * f;
                    LB[gy * LW + gx] += l->cb * f;
                }
        }
    {
        static int rowL[3][1024];
        for (int y = 0; y < FBH; y++) {
            int gy = y >> 2, fy = y & 3;
            for (int gx = 0; gx < LW && gx < 1024; gx++) {
                int a = gy * LW + gx, b = (gy + 1 < LH ? gy + 1 : gy) * LW + gx;
                rowL[0][gx] = (int)((LR[a] * (4 - fy) + LR[b] * fy) * 64);
                rowL[1][gx] = (int)((LG[a] * (4 - fy) + LG[b] * fy) * 64);
                rowL[2][gx] = (int)((LB[a] * (4 - fy) + LB[b] * fy) * 64);
            }
            u32 *row = FB + y * FBW;
            for (int x = 0; x < FBW; x++) {
                int gx = x >> 2, fx = x & 3;
                int lr = (rowL[0][gx] * (4 - fx) + rowL[0][gx + 1] * fx) >> 2;
                int lg = (rowL[1][gx] * (4 - fx) + rowL[1][gx + 1] * fx) >> 2;
                int lb = (rowL[2][gx] * (4 - fx) + rowL[2][gx + 1] * fx) >> 2;
                u32 c = row[x];
                int r = (CR(c) * lr) >> 8, g = (CG(c) * lg) >> 8, b = (CB(c) * lb) >> 8;
                if (lr > 300) r += (lr - 300) >> 3;
                if (lg > 300) g += (lg - 300) >> 3;
                if (lb > 300) b += (lb - 300) >> 3;
                row[x] = RGB(r, g, b);
            }
        }
    }

    /* 6. emissive: fire, embers, fireflies, sparks */
    if (!MIND) {
        for (int li = 0; li < G.nlights; li++) {
            Light *l = &G.lights[li];
            int lx = (int)(l->x * TS) - CX0, ly = (int)(l->y * TS) - CY0;
            if (lx < -40 || ly < -40 || lx > FBW + 40 || ly > FBH + 40) continue;
            bool fire = l->r > 7;
            float night = 1 - dayl * 0.7f;
            if (fire) {
                glow(lx, ly - 2, 22, (int)(70 * night), (int)(34 * night), (int)(8 * night));
                draw_flame(lx, ly + 1, 4.2f, li * 13);
                if (frand() < 0.25f) part_spawn(P_EMBER, (float)(lx + CX0) + (frand() - 0.5f) * 6, (float)(ly + CY0) - 4, (frand() - 0.5f) * 0.3f, -0.4f - frand() * 0.5f, 50 + frand() * 40, 0xFFFFA040, 1);
                if (frand() < 0.12f) part_spawn(P_SMOKE, (float)(lx + CX0) + (frand() - 0.5f) * 3, (float)(ly + CY0) - 12, 0, -0.35f, 110, 0xFF50505A, 1);
            } else {
                glow(lx, ly - 8 - 4, 12, (int)(60 * night), (int)(34 * night), (int)(10 * night));
                draw_flame(lx, ly - 9 - 4, 2.0f, li * 13);
            }
        }
        if (dayl < 0.5f && frand() < 0.35f && G.npart < MAX_PART - 100) {
            int x = tx0 + irand(tx1 - tx0 + 1), y = ty0 + irand(ty1 - ty0 + 1);
            Tile *t = &G.tiles[IDX(x, y)];
            if (t->terrain == T_GRASS && G.fog[IDX(x, y)])
                part_spawn(P_FIREFLY, x * TS + frand() * 16, y * TS + frand() * 16, 0, 0, 200 + frand() * 200, 0xFFD8FF70, 1);
        }
    }
    for (int i = 0; i < G.npart; i++) {
        Particle *p = &G.part[i];
        int x = (int)p->x - CX0, y = (int)p->y - CY0;
        float f = p->life / p->max;
        switch (p->type) {
        case P_EMBER: { int k = (int)(255 * f); padd(x, y, k, k * 2 / 3, k / 5); break; }
        case P_SPARK: padd(x, y, 255, 220, 140); break;
        case P_SPARKLE: { int k = (int)(255 * f); padd(x, y, k, k, k / 2); padd(x - 1, y, k / 3, k / 3, 0); padd(x + 1, y, k / 3, k / 3, 0); padd(x, y - 1, k / 3, k / 3, 0); padd(x, y + 1, k / 3, k / 3, 0); break; }
        case P_WISP: glow(x, y, 3, (int)(60 * f), (int)(90 * f), (int)(160 * f)); break;
        case P_FIREFLY: {
            if (MIND) break;
            float pulse = 0.5f + 0.5f * SINF(T * 3 + p->max);
            float fade = fminf(1, fminf(p->life, p->max - p->life) / 30.0f) * pulse * (1 - dayl);
            glow(x, y, 4, (int)(90 * fade), (int)(120 * fade), (int)(20 * fade));
            padd(x, y, (int)(200 * fade), (int)(255 * fade), (int)(90 * fade));
            break;
        }
        default: break;
        }
    }

    /* 7. memory fog (collective) or mind view (one colonist) */
    {
        int shx = (int)(T * 9), shy = (int)(T * 5);
        int vr = MIND ? MC->vision * TS : 0;
        int mcx = MIND ? (int)(MC->x * TS) - CX0 : 0, mcy = MIND ? (int)(MC->y * TS) - CY0 : 0;
        for (int y = 0; y < FBH; y++) {
            int wy = y + CY0;
            u32 *row = FB + y * FBW;
            int fy = wy - 8, ty = fy >> 4, fry = fy & 15;
            int ta = clampi(ty, 0, MAP_H - 1), tb = clampi(ty + 1, 0, MAP_H - 1);
            for (int x = 0; x < FBW; x++) {
                int wx = x + CX0;
                int fx = wx - 8, tx = fx >> 4, frx = fx & 15;
                int xa = clampi(tx, 0, MAP_W - 1), xb = clampi(tx + 1, 0, MAP_W - 1);
                int a, b, c, d;
                if (MIND) {
                    a = MC->mem[IDX(xa, ta)].str; b = MC->mem[IDX(xb, ta)].str;
                    c = MC->mem[IDX(xa, tb)].str; d = MC->mem[IDX(xb, tb)].str;
                } else {
                    a = G.fog[IDX(xa, ta)]; b = G.fog[IDX(xb, ta)];
                    c = G.fog[IDX(xa, tb)]; d = G.fog[IDX(xb, tb)];
                }
                if (wx < 0 || wy < 0 || wx >= WORLD_PW || wy >= WORLD_PH) a = b = c = d = 0;
                int v = ((a * (16 - frx) + b * frx) * (16 - fry) + (c * (16 - frx) + d * frx) * fry) >> 8;
                int cl = cloud[((wy + shy) & 255) * 256 + ((wx + shx) & 255)];
                u32 px = row[x];
                if (MIND) {
                    int dx = x - mcx, dy = y - mcy;
                    int d2 = dx * dx + dy * dy;
                    if (d2 < vr * vr) {
                        float e = sqrtf((float)d2) / vr;
                        if (e < 0.85f) continue;
                        float t = (e - 0.85f) / 0.15f;
                        v = (int)(255 * (1 - t) + v * t);
                    }
                    int lum = (CR(px) * 30 + CG(px) * 59 + CB(px) * 11) / 100;
                    float s = clampf((v + (cl - 128) / 3) / 255.0f, 0, 1);
                    u32 sep = RGB(lum * 110 / 100 + 18, lum * 92 / 100 + 8, lum * 70 / 100);
                    u32 ink = RGB(16 + cl / 20, 12 + cl / 24, 26 + cl / 12);
                    row[x] = col_lerp(ink, sep, s * s * (3 - 2 * s));
                } else {
                    int vv = v + (cl - 128) / 2;
                    if (vv >= 170) continue;
                    float k = clampf((170 - vv) / 170.0f, 0, 1);
                    int lum = (CR(px) * 30 + CG(px) * 59 + CB(px) * 11) / 100;
                    u32 desat = RGB(lum * 70 / 100 + 6, lum * 72 / 100 + 8, lum * 90 / 100 + 18);
                    u32 mixed = col_lerp(px, desat, fminf(1, k * 1.4f));
                    u32 fogc = RGB(12 + cl / 18, 14 + cl / 16, 24 + cl / 10);
                    row[x] = col_lerp(mixed, fogc, k * k);
                }
            }
        }
    }

    /* 8. overlays (unlit) */
    for (int y = ty0; y <= ty1; y++)
        for (int x = tx0; x <= tx1; x++) {
            View v = vget(x, y);
            if (MIND && !v.str) continue;
            int sx = x * TS - CX0, sy = y * TS - CY0;
            if (v.flags & FL_STOCK) {
                bool u = y > 0 && (vget(x, y - 1).flags & FL_STOCK), dn = y < MAP_H - 1 && (vget(x, y + 1).flags & FL_STOCK);
                bool l = x > 0 && (vget(x - 1, y).flags & FL_STOCK), r = x < MAP_W - 1 && (vget(x + 1, y).flags & FL_STOCK);
                for (int i = 0; i < 16; i += 2) {
                    if (!u) pblend(sx + i, sy, 0xFFF0DC90, 0.6f);
                    if (!dn) pblend(sx + i, sy + 15, 0xFFF0DC90, 0.6f);
                    if (!l) pblend(sx, sy + i, 0xFFF0DC90, 0.6f);
                    if (!r) pblend(sx + 15, sy + i, 0xFFF0DC90, 0.6f);
                }
            }
            u32 flag = 0;
            if (v.flags & D_CHOP) flag = 0xFFFF5A4A;
            else if (v.flags & D_MINE) flag = 0xFF7FC8FF;
            else if (v.flags & D_HARVEST) flag = 0xFFFF8FD0;
            else if (v.flags & D_DECON) flag = 0xFFFFA030;
            if (flag) {
                /* painted marker flag: the only way settlers learn of an order is to see it */
                int fx = sx + 12, fy = sy + 2 + (v.feat == F_TREE || v.feat == F_PINE ? 0 : 4);
                int wave = (int)(T * 6 + x) % 3;
                for (int j = 0; j < 8; j++) pset(fx, fy + j, 0xFF2A2020);
                for (int j = 0; j < 3; j++)
                    for (int i = 0; i < 4 - j + (wave == 1); i++) pset(fx + 1 + i, fy + j, j == 0 ? col_mul(flag, 1.2f) : flag);
                for (int j = 3; j < 5; j++)
                    for (int i = 0; i < 4 - (4 - j) + (wave == 1); i++) pset(fx + 1 + i, fy + j, col_mul(flag, 0.75f));
            }
            if (v.feat == F_BLUEPRINT && !MIND) {
                const BuildDef *bd = &BUILDS[v.aux];
                int need = bd->cost;
                for (int k = 0; k < need; k++) rect(sx + 2 + k * 4, sy + 13, 3, 2, k < v.icount ? 0xFFFFD27F : 0xFF404050);
                Tile *t = &G.tiles[IDX(x, y)];
                if (v.icount >= need && t->hp < bd->work) rect(sx + 1, sy + 11, (14 * (bd->work - t->hp)) / bd->work, 1, 0xFF7FFF9F);
            }
        }
    /* selection */
    if (G.sel >= 0 && G.col[G.sel].alive) {
        Colonist *c = &G.col[G.sel];
        int fx = (int)(c->x * TS) - CX0, fy = (int)(c->y * TS) - CY0 + 4;
        float pulse = 0.6f + 0.4f * SINF(T * 5);
        for (int k = 0; k < 40; k++) {
            float a = k * 6.2831f / 40;
            pblend(fx + (int)roundf(cosf(a) * 7), fy + (int)roundf(sinf(a) * 3), 0xFFFFE070, pulse);
        }
        for (int k = c->pidx; k < c->plen; k++) {
            if ((k + (int)(T * 8)) % 3) continue;
            int px = c->path[k][0] * TS + 8 - CX0, py = c->path[k][1] * TS + 8 - CY0;
            rect_blend(px - 1, py - 1, 2, 2, 0xFFFFE070, 0.7f);
        }
        if (c->job != J_NONE && inb(c->jx, c->jy)) dashed_rect(c->jx * TS - CX0, c->jy * TS - CY0, 16, 16, 0xFFFFE070, 0.8f);
    }
    /* chat links, thoughts, zzz */
    for (int i = 0; i < G.ncol; i++) {
        Colonist *c = &G.col[i];
        if (!c->alive) continue;
        if (MIND && c != MC && !sees_now(MC, c->tx, c->ty)) continue;
        int fx = (int)(c->x * TS) - CX0, fy = (int)(c->y * TS) - CY0;
        if (c->chat_timer > 0 && c->chat_with > i) {
            Colonist *o = &G.col[c->chat_with];
            int ox = (int)(o->x * TS) - CX0, oy = (int)(o->y * TS) - CY0;
            for (int k = 1; k < 12; k++) {
                float t = k / 12.0f;
                int px = (int)lerpf(fx, ox, t), py = (int)lerpf(fy, oy, t) - 16 - (int)(sinf(t * 3.14159f) * 6);
                if (((k + (int)(T * 6)) % 3) == 0) pblend(px, py, 0xFFFFFFFF, 0.8f);
            }
        }
        int th = c->thought;
        if (!th && c->state == S_SLEEP) th = TH_NONE;
        if (th) bubble(fx, fy - 17 - (c->state == S_SLEEP ? -6 : 0), th);
    }
    for (int i = 0; i < G.npart; i++) {
        Particle *p = &G.part[i];
        if (p->type != P_ZZZ) continue;
        int x = (int)p->x - CX0, y = (int)p->y - CY0;
        float f = p->life / p->max;
        pblend(x, y, 0xFFFFFFFF, f); pblend(x + 1, y, 0xFFFFFFFF, f); pblend(x + 2, y, 0xFFFFFFFF, f);
        pblend(x + 1, y + 1, 0xFFFFFFFF, f);
        pblend(x, y + 2, 0xFFFFFFFF, f); pblend(x + 1, y + 2, 0xFFFFFFFF, f); pblend(x + 2, y + 2, 0xFFFFFFFF, f);
    }
    /* hover & drag preview */
    if (inb(G.hovx, G.hovy) && !G.dragging) {
        int sx = G.hovx * TS - CX0, sy = G.hovy * TS - CY0;
        u32 c = G.tool == TOOL_SELECT ? 0xFFFFFFFF : 0xFF9FE0FF;
        dashed_rect(sx, sy, 16, 16, c, 0.5f);
        if (G.tool == TOOL_BUILD) {
            if (G.build == B_WALL) draw_wall(G.hovx, G.hovy, sx, sy, true, false);
            else blit(&SPR[SP_BUILD0 + G.build], sx + 2, sy + 2, false, true);
        }
    }
    if (G.dragging) {
        int x0 = G.dragx0 < G.dragx1 ? G.dragx0 : G.dragx1, x1 = G.dragx0 < G.dragx1 ? G.dragx1 : G.dragx0;
        int y0 = G.dragy0 < G.dragy1 ? G.dragy0 : G.dragy1, y1 = G.dragy0 < G.dragy1 ? G.dragy1 : G.dragy0;
        u32 c = G.tool == TOOL_CANCEL || G.tool == TOOL_DECON ? 0xFFFF7070 : G.tool == TOOL_STOCK ? 0xFFF0DC90 : 0xFF9FE0FF;
        rect_blend(x0 * TS - CX0, y0 * TS - CY0, (x1 - x0 + 1) * TS, (y1 - y0 + 1) * TS, c, 0.15f);
        dashed_rect(x0 * TS - CX0, y0 * TS - CY0, (x1 - x0 + 1) * TS, (y1 - y0 + 1) * TS, c, 0.9f);
        if (G.tool == TOOL_BUILD && G.build == B_WALL)
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                    if (x == x0 || x == x1 || y == y0 || y == y1) rect_blend(x * TS - CX0 + 1, y * TS - CY0 + 1, 14, 14, 0xFF9FE0FF, 0.3f);
    }

    /* 9. vignette */
    for (int i = 0; i < FBW * FBH; i++) {
        u32 c = FB[i];
        int v = vign[i];
        FB[i] = 0xFF000000u | ((((c >> 16) & 255) * v >> 8) << 16) | ((((c >> 8) & 255) * v >> 8) << 8) | ((c & 255) * v >> 8);
    }
}
