// The heart of the game: every colonist carries their own fading picture of
// the world and their own fading knowledge of how to do things.
#include "game.h"

bool mem_blocks(const MemCell *m) {
    if (!m->str) return false;
    return m->terr == T_WATER || feat_blocks(m->feat);
}

void set_thought(Colonist *c, int th, int ticks) {
    c->thought = th;
    c->thought_timer = ticks;
}

void mem_observe(Colonist *c, int x, int y) {
    Tile *t = &G.tiles[IDX(x, y)];
    MemCell *m = &c->mem[IDX(x, y)];
    /* spacing effect: re-seeing something half-forgotten strengthens it most */
    int gain = m->str ? (255 - m->str) / 40 : 2;
    m->stab = (u8)clampi(m->stab + gain, 0, 255);
    m->str = 255;
    m->terr = t->terrain;
    m->feat = t->feat;
    m->aux = t->aux;
    m->flags = t->flags;
    m->item = t->item;
    m->icount = t->feat == F_BLUEPRINT ? t->bpmat : t->icount;
}

void mem_reinforce(Colonist *c, int x, int y, int amount) {
    MemCell *m = &c->mem[IDX(x, y)];
    int gain = amount + (255 - m->str) / 24;
    m->stab = (u8)clampi(m->stab + gain, 0, 255);
    m->str = 255;
}

bool los(int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    int x = x0, y = y0;
    for (;;) {
        if (x == x1 && y == y1) return true;
        if (!(x == x0 && y == y0) && feat_opaque(G.tiles[IDX(x, y)].feat)) return false;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

static float daylight(void) {
    float h = hour_of_day();
    if (h >= 6.5f && h <= 19.5f) return 1;
    if (h > 5.5f && h < 6.5f) return h - 5.5f;
    if (h > 19.5f && h < 20.5f) return 20.5f - h;
    return 0;
}

static bool visible_rule(Colonist *c, int dx, int dy, int rn, int idx) {
    int d2 = dx * dx + dy * dy;
    if (d2 > c->vision * c->vision) return false;
    return d2 <= rn * rn || G.tlight[idx] > 0.3f;
}

void perceive(Colonist *c) {
    int R = c->vision, rn = 2 + (int)((R - 2) * daylight());
    for (int dy = -R; dy <= R; dy++)
        for (int dx = -R; dx <= R; dx++) {
            int x = c->tx + dx, y = c->ty + dy;
            if (!inb(x, y)) continue;
            if (!visible_rule(c, dx, dy, rn, IDX(x, y))) continue;
            if (abs(dx) + abs(dy) > 1 && !los(c->tx, c->ty, x, y)) continue;
            mem_observe(c, x, y);
        }
}

bool sees_now(Colonist *c, int x, int y) {
    int R = c->vision, rn = 2 + (int)((R - 2) * daylight());
    if (!inb(x, y) || !visible_rule(c, x - c->tx, y - c->ty, rn, IDX(x, y))) return false;
    return los(c->tx, c->ty, x, y);
}

static bool driftable(const MemCell *m) {
    return m->feat == F_TREE || m->feat == F_PINE || m->feat == F_BUSH || m->feat == F_ROCK || (m->feat == F_NONE && m->item);
}

/* Once per second: every memory fades a little. Weakly-stabilised memories
   fade fast; fading memories may drift to the wrong tile before vanishing. */
void mem_decay(Colonist *c, int sec) {
    float q = c->memq;
    if (c->energy < 15) q *= 0.5f; /* exhaustion ruins memory */
    for (int i = 0; i < MAP_W * MAP_H; i++) {
        MemCell *m = &c->mem[i];
        if (!m->str) continue;
        int k = (int)((2 + m->stab / 10) * q);
        if (k < 1) k = 1;
        if ((u32)(sec + i * 7) % (u32)k) continue;
        m->str--;
        if (m->str == 55 && driftable(m) && hash2(i, sec, c->id) % 100 < 30) {
            int d = hash2(i, sec, 77) % 8;
            static const int DX[8] = {1, -1, 0, 0, 1, -1, 1, -1}, DY[8] = {0, 0, 1, -1, 1, 1, -1, -1};
            int x = i % MAP_W + DX[d], y = i / MAP_W + DY[d];
            if (inb(x, y)) {
                MemCell *n = &c->mem[IDX(x, y)];
                if (n->str < 55 && n->terr != T_WATER) {
                    u8 terr = n->str ? n->terr : m->terr;
                    *n = *m;
                    n->terr = terr;
                    m->feat = F_NONE;
                    m->item = I_NONE;
                    m->icount = 0;
                    m->flags &= FL_FLOOR | FL_FARM | FL_STOCK;
                }
            }
        }
        if (!m->str) {
            u8 stab = m->stab / 2; /* "savings": relearning is quicker */
            memset(m, 0, sizeof *m);
            m->stab = stab;
        }
    }
}

/* Sleep consolidates vivid memories into durable ones. */
void mem_consolidate(Colonist *c, int sec) {
    for (int i = sec % 10; i < MAP_W * MAP_H; i += 10) {
        MemCell *m = &c->mem[i];
        if (m->str > 110 && m->stab < 255) m->stab++;
    }
}

static int topic_value(const MemCell *m) {
    if (!m->str) return 0;
    if (m->feat == F_CAMPFIRE) return 9;
    if (m->flags & FL_STOCK) return 7;
    if (m->feat == F_BLUEPRINT) return 6;
    if (m->flags & (D_CHOP | D_MINE | D_HARVEST | D_DECON)) return 6;
    if (m->item && ITEM_FOOD[m->item]) return 6;
    if (m->feat == F_BUSH && m->aux > 0) return 5;
    if (m->feat == F_BED) return 5;
    if (m->flags & FL_FARM) return 4;
    if (m->item) return 3;
    if (m->feat == F_TREE || m->feat == F_PINE || m->feat == F_ROCK) return 1;
    return 0;
}

static int topic_icon(const MemCell *m) {
    if (m->flags & FL_STOCK) return TH_BOX;
    if (m->item) return thought_for_item(m->item);
    return thought_for_feat(m->feat);
}

static void teach_skill(Colonist *spk, Colonist *lis) {
    int best = -1;
    float gap = 0.1f;
    for (int s = 0; s < SK_COUNT; s++) {
        float g = spk->know[s] - lis->know[s];
        if (spk->know[s] >= 0.5f && g > gap) { gap = g; best = s; }
    }
    if (best < 0) return;
    float before = lis->know[best];
    lis->know[best] = clampf(before + 0.02f * spk->know[best], 0, 1);
    set_thought(spk, TH_BOOK, 40);
    if (before < 0.2f && lis->know[best] >= 0.2f) {
        lis->know[best] = fmaxf(lis->know[best], 0.34f);
        lis->forgot_flag[best] = false;
        lis->lessons++;
        set_thought(lis, TH_BULB, 70);
        logmsg(0xFFFFE08A, "%s taught %s how %s works.", spk->name, lis->name, SKILL_NAME[best]);
        for (int k = 0; k < 10; k++)
            part_spawn(P_SPARKLE, lis->x * TS, lis->y * TS - 14, (frand() - 0.5f) * 1.2f, -frand() * 1.2f, 30, 0xFFFFE680, 1);
    }
}

void mem_share(Colonist *spk, Colonist *lis) {
    static bool flip;
    flip = !flip;
    if (flip) { teach_skill(spk, lis); if (spk->thought == TH_BOOK) return; }
    int best = -1, bestv = 0;
    for (int k = 0; k < 220; k++) {
        int i = irand(MAP_W * MAP_H);
        MemCell *m = &spk->mem[i];
        if (m->str < 70) continue;
        MemCell *l = &lis->mem[i];
        if (l->str >= m->str * 0.8f && l->feat == m->feat && l->item == m->item) continue;
        int v = topic_value(m) * m->str;
        if (v > bestv) { bestv = v; best = i; }
    }
    if (best < 0) { teach_skill(spk, lis); return; }
    int bx = best % MAP_W, by = best / MAP_W;
    bool stock_news = (spk->mem[best].flags & FL_STOCK) && lis->known_stock == 0;
    bool fire_news = spk->mem[best].feat == F_CAMPFIRE && lis->known_fire == 0;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int x = bx + dx, y = by + dy;
            if (!inb(x, y)) continue;
            MemCell *m = &spk->mem[IDX(x, y)], *l = &lis->mem[IDX(x, y)];
            if (m->str < 40) continue;
            u8 told = (u8)(m->str * 0.65f);
            if (l->str >= told) continue;
            u8 stab = l->stab;
            *l = *m;
            l->str = told;
            l->stab = (u8)clampi(stab + 3, 0, 255);
        }
    set_thought(spk, topic_icon(&spk->mem[best]), 45);
    if (stock_news) logmsg(0xFFB5E7A0, "%s reminded %s where the stockpile is.", spk->name, lis->name);
    if (fire_news) logmsg(0xFFB5E7A0, "%s told %s about the campfire.", spk->name, lis->name);
}

void skills_tick(Colonist *c) {
    for (int s = 0; s < SK_COUNT; s++) {
        float before = c->know[s];
        c->know[s] = clampf(before - 0.0011f / ((1 + c->kstab[s]) * sqrtf(c->memq)), 0, 1);
        if (before >= 0.2f && c->know[s] < 0.2f && !c->forgot_flag[s]) {
            c->forgot_flag[s] = true;
            set_thought(c, TH_FORGET, 90);
            logmsg(0xFFFF8A8A, "%s has forgotten how %s works.", c->name, SKILL_NAME[s]);
            for (int k = 0; k < 14; k++)
                part_spawn(P_WISP, c->x * TS + (frand() - 0.5f) * 6, c->y * TS - 14, (frand() - 0.5f) * 0.3f, -0.3f - frand() * 0.4f, 60, 0xFF9FB8FF, 1);
        }
    }
}

void practice(Colonist *c, int sk, bool botched) {
    if (sk < 0) return;
    float before = c->know[sk];
    c->know[sk] = clampf(before + (botched ? 0.03f : 0.02f), 0, 1);
    c->kstab[sk] += 0.02f + 0.08f * (1 - before);
    if (before < 0.2f && c->know[sk] >= 0.2f) {
        c->know[sk] = fmaxf(c->know[sk], 0.34f);
        c->forgot_flag[sk] = false;
        c->lessons++;
        set_thought(c, TH_BULB, 70);
        logmsg(0xFFFFE08A, "%s worked out %s by trial and error.", c->name, SKILL_NAME[sk]);
    }
}

void observe_learning(void) {
    for (int a = 0; a < G.ncol; a++) {
        Colonist *A = &G.col[a];
        if (!A->alive || A->state != S_WORK) continue;
        int sk = job_skill(A->job);
        if (sk < 0 || A->know[sk] < 0.4f) continue;
        for (int b = 0; b < G.ncol; b++) {
            Colonist *B = &G.col[b];
            if (b == a || !B->alive || B->state == S_SLEEP) continue;
            float dx = A->x - B->x, dy = A->y - B->y;
            if (dx * dx + dy * dy > 16 || B->know[sk] > A->know[sk] - 0.1f) continue;
            float before = B->know[sk];
            B->know[sk] += 0.004f;
            if (before < 0.2f && B->know[sk] >= 0.2f) {
                B->know[sk] = fmaxf(B->know[sk], 0.34f);
                B->forgot_flag[sk] = false;
                B->lessons++;
                set_thought(B, TH_BULB, 70);
                logmsg(0xFFFFE08A, "%s picked up %s by watching %s.", B->name, SKILL_NAME[sk], A->name);
            }
        }
    }
}

float mem_known_frac(Colonist *c) {
    int n = 0;
    for (int i = 0; i < MAP_W * MAP_H; i++) n += c->mem[i].str > 0;
    return n / (float)(MAP_W * MAP_H);
}

void mem_check_losses(Colonist *c) {
    int st = 0, fi = 0, be = 0;
    for (int i = 0; i < MAP_W * MAP_H; i++) {
        MemCell *m = &c->mem[i];
        if (!m->str) continue;
        if (m->flags & FL_STOCK) st++;
        if (m->feat == F_CAMPFIRE) fi++;
        if (m->feat == F_BED && m->aux == c->id + 1) be++;
    }
    const char *lost = NULL;
    if (c->known_stock && !st) lost = "where the stockpile is";
    else if (c->known_fire && !fi) lost = "where the campfire is";
    else if (c->known_bed && !be) lost = "where their bed is";
    if (lost) {
        set_thought(c, TH_FORGET, 90);
        logmsg(0xFFFF9F7A, "%s can no longer remember %s.", c->name, lost);
        for (int k = 0; k < 10; k++)
            part_spawn(P_WISP, c->x * TS + (frand() - 0.5f) * 6, c->y * TS - 14, (frand() - 0.5f) * 0.3f, -0.3f - frand() * 0.4f, 60, 0xFF9FB8FF, 1);
    }
    c->known_stock = st;
    c->known_fire = fi;
    c->known_bed = be;
}
