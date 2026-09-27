// Colonist minds: every decision below is made from the colonist's own
// memory (c->mem), never from the true world. Reality is only consulted when
// they arrive somewhere and look — which is where surprises come from.
#include "game.h"

int job_skill(int j) {
    switch (j) {
    case J_CHOP: return SK_WOOD;
    case J_MINE: return SK_MINE;
    case J_HARVEST: return SK_FORAGE;
    case J_BUILD: case J_DECON: return SK_BUILD;
    case J_SOW: case J_REAP: return SK_FARM;
    case J_COOK: return SK_COOK;
    default: return -1;
    }
}

const char *job_name(int j) {
    static const char *n[J_COUNT] = {"Idle", "Eating", "Picking berries to eat", "Sleeping", "Gathering at the fire",
                                     "Chopping", "Mining", "Harvesting berries", "Hauling", "Storing", "Fetching materials",
                                     "Delivering materials", "Building", "Sowing", "Reaping", "Fetching food to cook",
                                     "Cooking", "Exploring", "Wandering", "Deconstructing"};
    return (j >= 0 && j < J_COUNT) ? n[j] : "?";
}

static float dist(Colonist *c, int x, int y) {
    float dx = x - c->tx, dy = y - c->ty;
    return sqrtf(dx * dx + dy * dy);
}

static bool blacklisted(Colonist *c, int x, int y) {
    for (int i = 0; i < 6; i++)
        if (c->black_t[i] > (int)G.tick && c->black_x[i] == x && c->black_y[i] == y) return true;
    return false;
}
static void blacklist(Colonist *c, int x, int y) {
    int k = 0;
    for (int i = 1; i < 6; i++)
        if (c->black_t[i] < c->black_t[k]) k = i;
    c->black_x[k] = x;
    c->black_y[k] = y;
    c->black_t[k] = G.tick + TPS * 25;
}

static bool resv_by_other(Colonist *c, int x, int y) {
    Tile *t = &G.tiles[IDX(x, y)];
    if (t->resv < 0 || t->resv == c->id) return false;
    Colonist *o = &G.col[(int)t->resv];
    if (!o->alive || o->job == J_NONE || o->jx != x || o->jy != y) { t->resv = -1; return false; }
    return true;
}

static void end_job(Colonist *c) {
    if (inb(c->jx, c->jy) && G.tiles[IDX(c->jx, c->jy)].resv == c->id) G.tiles[IDX(c->jx, c->jy)].resv = -1;
    c->job = J_NONE;
    c->state = S_IDLE;
    c->plen = c->pidx = 0;
    c->work = 0;
    c->timer = 0;
}

static bool start_job(Colonist *c, int job, int x, int y, bool adj, const char *status) {
    if (c->job != J_NONE) end_job(c);
    if (path_find(c, c->tx, c->ty, x, y, adj) < 0) {
        blacklist(c, x, y);
        return false;
    }
    c->job = job;
    c->jx = x;
    c->jy = y;
    c->adj = adj;
    c->work = 0;
    c->timer = 0;
    c->state = S_MOVE;
    if (job_skill(job) >= 0 || job == J_HAUL || job == J_FETCH || job == J_COOKFETCH || job == J_EAT || job == J_EAT_BUSH)
        G.tiles[IDX(x, y)].resv = c->id;
    snprintf(c->status, sizeof c->status, "%s", status);
    return true;
}

/* ------------------------------------------------------------------ memory scan */

typedef struct { int job, x, y, bx, by, item, count; float score; bool adj; const char *why; } Cand;

typedef struct {
    int foodx, foody, foodjob; float foodd;
    int bedx, bedy; float bedd;
    int firex, firey; float fired;
    int srcx[I_COUNT], srcy[I_COUNT]; float srcd[I_COUNT];
    int stock_tiles, meals, raw, rawitem;
    bool stock_empty, stock_space[I_COUNT];
    Cand work;
} Scan;

static void consider(Colonist *c, Cand *b, int job, int x, int y, float base, bool adj, int str, const char *why) {
    float s = base - dist(c, x, y);
    int sk = job_skill(job);
    if (sk >= 0) s += c->know[sk] < 0.2f ? -35 : c->know[sk] * 15;
    if (str < 70) s -= 8;
    if (s <= b->score) return;
    if (blacklisted(c, x, y)) return;
    /* only avoid a claimed target if I can actually see someone working it */
    if (resv_by_other(c, x, y) && sees_now(c, x, y)) return;
    b->score = s; b->job = job; b->x = x; b->y = y; b->adj = adj; b->why = why;
    b->bx = b->by = -1; b->item = 0; b->count = 0;
}

static void scan(Colonist *c, Scan *S) {
    memset(S, 0, sizeof *S);
    S->foodd = S->bedd = S->fired = 1e9f;
    for (int i = 0; i < I_COUNT; i++) { S->srcd[i] = 1e9f; S->srcx[i] = -1; }
    S->foodx = S->bedx = S->firex = -1;
    S->work.score = -1e9f;
    int alive = 0;
    for (int i = 0; i < G.ncol; i++) alive += G.col[i].alive;
    /* pass 1: where are things (as far as I remember)? */
    for (int i = 0; i < MAP_W * MAP_H; i++) {
        MemCell *m = &c->mem[i];
        if (!m->str) continue;
        int x = i % MAP_W, y = i / MAP_W;
        float d = dist(c, x, y);
        if (m->item && m->icount && m->feat != F_BLUEPRINT) {
            float sd = d - ((m->flags & FL_STOCK) ? 6 : 0);
            if (sd < S->srcd[m->item] && !blacklisted(c, x, y)) { S->srcd[m->item] = sd; S->srcx[m->item] = x; S->srcy[m->item] = y; }
            if (ITEM_FOOD[m->item]) {
                float fd = d - (m->item == I_MEAL ? 8 : 0);
                if (fd < S->foodd && !blacklisted(c, x, y) && !resv_by_other(c, x, y)) { S->foodd = fd; S->foodx = x; S->foody = y; S->foodjob = J_EAT; }
                if (m->flags & FL_STOCK) { if (m->item == I_MEAL) S->meals += m->icount; else S->raw += m->icount; }
            }
        }
        if (m->feat == F_BUSH && m->aux > 0 && d + 6 < S->foodd && !blacklisted(c, x, y)) { S->foodd = d + 6; S->foodx = x; S->foody = y; S->foodjob = J_EAT_BUSH; }
        if (m->feat == F_BED && (m->aux == 0 || m->aux == c->id + 1)) {
            float bd = d - (m->aux ? 30 : 0);
            if (bd < S->bedd && !blacklisted(c, x, y)) { S->bedd = bd; S->bedx = x; S->bedy = y; }
        }
        if (m->feat == F_CAMPFIRE && d < S->fired && !blacklisted(c, x, y)) { S->fired = d; S->firex = x; S->firey = y; }
        if ((m->flags & FL_STOCK) && !feat_blocks(m->feat) && m->feat != F_BLUEPRINT) {
            S->stock_tiles++;
            if (!m->item) S->stock_empty = true;
            else if (m->icount < MAX_STACK) S->stock_space[m->item] = true;
        }
    }
    int rawitem = S->srcd[I_VEG] < S->srcd[I_BERRY] ? I_VEG : I_BERRY;
    S->rawitem = rawitem;
    bool food_low = S->meals + S->raw < alive * 4;
    /* pass 2: what could I do? */
    Cand *b = &S->work;
    for (int i = 0; i < MAP_W * MAP_H; i++) {
        MemCell *m = &c->mem[i];
        if (!m->str) continue;
        int x = i % MAP_W, y = i / MAP_W;
        switch (m->feat) {
        case F_TREE: case F_PINE: if (m->flags & D_CHOP) consider(c, b, J_CHOP, x, y, 40, true, m->str, "chop"); break;
        case F_ROCK: if (m->flags & D_MINE) consider(c, b, J_MINE, x, y, 40, true, m->str, "mine"); break;
        case F_BUSH:
            if (m->aux > 0 && ((m->flags & D_HARVEST) || food_low))
                consider(c, b, J_HARVEST, x, y, (m->flags & D_HARVEST) ? 44 : 30, true, m->str, "harvest");
            break;
        case F_BLUEPRINT: {
            const BuildDef *bd = &BUILDS[m->aux];
            if (m->icount >= bd->cost) consider(c, b, J_BUILD, x, y, 52, true, m->str, "build");
            else if (S->srcx[bd->mat] >= 0) {
                int sx = S->srcx[bd->mat], sy = S->srcy[bd->mat];
                float extra = sqrtf((float)((sx - x) * (sx - x) + (sy - y) * (sy - y)));
                float before = b->score;
                consider(c, b, J_FETCH, sx, sy, 48 + dist(c, x, y) - dist(c, sx, sy) - (dist(c, sx, sy) + extra) * 0.7f, false, m->str, "fetch");
                if (b->score != before && b->job == J_FETCH && b->x == sx && b->y == sy) {
                    b->bx = x; b->by = y; b->item = bd->mat; b->count = bd->cost - m->icount;
                }
            }
            break;
        }
        case F_CROP: if (m->aux >= 3) consider(c, b, J_REAP, x, y, 50, false, m->str, "reap"); break;
        case F_NONE:
            if ((m->flags & FL_FARM) && !m->item) consider(c, b, J_SOW, x, y, 34, false, m->str, "sow");
            break;
        default: break;
        }
        if (m->flags & D_DECON) consider(c, b, J_DECON, x, y, 45, true, m->str, "decon");
        if (m->item && m->icount && !(m->flags & FL_STOCK) && m->feat != F_BLUEPRINT && (S->stock_empty || S->stock_space[m->item])) {
            float before = b->score;
            consider(c, b, J_HAUL, x, y, ITEM_FOOD[m->item] ? 34 : 28, false, m->str, "haul");
            if (b->score != before && b->job == J_HAUL) b->item = m->item;
        }
    }
    int cooks_seen = 0;
    for (int i = 0; i < G.ncol; i++) {
        Colonist *o = &G.col[i];
        if (o != c && o->alive && (o->job == J_COOK || o->job == J_COOKFETCH) && sees_now(c, o->tx, o->ty)) cooks_seen++;
    }
    if (S->firex >= 0 && S->srcx[rawitem] >= 0 && S->meals < alive * 2 && !cooks_seen) {
        float before = b->score;
        consider(c, b, J_COOKFETCH, S->srcx[rawitem], S->srcy[rawitem], 38 - S->fired * 0.5f, false, 200, "cook");
        if (b->score != before && b->job == J_COOKFETCH) { b->item = rawitem; b->bx = S->firex; b->by = S->firey; }
    }
}

static bool find_stock(Colonist *c, int item, int *ox, int *oy) {
    float best = 1e9f;
    *ox = -1;
    for (int i = 0; i < MAP_W * MAP_H; i++) {
        MemCell *m = &c->mem[i];
        if (!m->str || !(m->flags & FL_STOCK) || feat_blocks(m->feat) || m->feat == F_BLUEPRINT) continue;
        if (m->item && (m->item != item || m->icount >= MAX_STACK)) continue;
        int x = i % MAP_W, y = i / MAP_W;
        float d = dist(c, x, y) - (m->item == item ? 3 : 0);
        if (d < best && !blacklisted(c, x, y)) { best = d; *ox = x; *oy = y; }
    }
    if (*ox >= 0) return true;
    return false;
}

static bool random_known_tile(Colonist *c, int cx, int cy, int r, bool prefer_unknown, int *ox, int *oy) {
    float best = -1e9f;
    *ox = -1;
    for (int k = 0; k < 70; k++) {
        int x = cx + irand(2 * r + 1) - r, y = cy + irand(2 * r + 1) - r;
        if (!inb(x, y) || (x == c->tx && y == c->ty)) continue;
        MemCell *m = &c->mem[IDX(x, y)];
        if (mem_blocks(m) || blacklisted(c, x, y)) continue;
        float s;
        if (prefer_unknown) s = (255 - m->str) * 0.3f - dist(c, x, y) * 1.2f + frand() * 20;
        else s = m->str ? 100 - dist(c, x, y) + frand() * 30 : -50;
        if (s > best) { best = s; *ox = x; *oy = y; }
    }
    return *ox >= 0;
}

static void drop_carry(Colonist *c) {
    if (!c->carry) return;
    int left = drop_item(c->tx, c->ty, c->carry, c->ccount);
    c->carry = left ? c->carry : I_NONE;
    c->ccount = left;
}

static void eat(Colonist *c, int item) {
    c->hunger = clampf(c->hunger - ITEM_FOOD[item], 0, 100);
    for (int k = 0; k < 4; k++) part_spawn(P_CHIP, c->x * TS, c->y * TS - 8, (frand() - 0.5f), -1 - frand(), 20, item == I_MEAL ? 0xFFC8A060 : 0xFFD03050, 1);
}

static bool is_social_time(void) { float h = hour_of_day(); return h >= 18.0f && h < 21.5f; }
static bool is_sleep_time(void) { float h = hour_of_day(); return h >= 21.5f || h < 5.5f; }

static void decide(Colonist *c) {
    if (c->carry && ITEM_FOOD[c->carry] && c->hunger > 45) {
        eat(c, c->carry);
        if (--c->ccount <= 0) c->carry = I_NONE;
        snprintf(c->status, sizeof c->status, "Eating what they carry");
        return;
    }
    Scan S;
    scan(c, &S);
    /* hunger */
    if (c->hunger > 55) {
        if (S.foodx >= 0 && start_job(c, S.foodjob, S.foodx, S.foody, S.foodjob == J_EAT_BUSH, "Going to eat")) {
            set_thought(c, TH_FOOD, 40);
            return;
        }
        int x, y;
        if (random_known_tile(c, c->tx, c->ty, 26, true, &x, &y) && start_job(c, J_EXPLORE, x, y, false, "Searching for food - can't remember any")) {
            set_thought(c, TH_QUESTION, 60);
            return;
        }
    }
    /* sleep */
    if (c->energy < 22 || (is_sleep_time() && c->energy < 80)) {
        if (S.bedx >= 0 && start_job(c, J_SLEEP, S.bedx, S.bedy, false, "Going to bed")) { c->jitem = 0; return; }
        int x = c->tx, y = c->ty;
        if (S.firex >= 0 && S.fired < 30) random_known_tile(c, S.firex, S.firey, 2, false, &x, &y);
        if (x >= 0 && start_job(c, J_SLEEP, x, y, false, S.firex >= 0 ? "Going to sleep by the fire" : "Going to sleep rough")) { c->jitem = 1; return; }
        c->job = J_SLEEP; c->jitem = 1; c->jx = c->tx; c->jy = c->ty; c->state = S_WORK;
        return;
    }
    /* carrying something: put it away */
    if (c->carry) {
        int x, y;
        if (find_stock(c, c->carry, &x, &y) && start_job(c, J_STORE, x, y, false, "Carrying to the stockpile")) return;
        drop_carry(c);
        blacklist(c, c->tx, c->ty);
        set_thought(c, TH_QUESTION, 60);
        snprintf(c->status, sizeof c->status, "Dropped their load - where's the stockpile?");
        return;
    }
    /* evening: gather at the fire to talk */
    if (is_social_time() && S.firex >= 0 && c->energy > 25 && c->hunger < 70) {
        int x, y;
        if (random_known_tile(c, S.firex, S.firey, 2, false, &x, &y) && start_job(c, J_SOCIAL, x, y, false, "Gathering at the fire")) return;
    }
    /* work */
    Cand *w = &S.work;
    if (w->score > -1e8f) {
        static char buf[64];
        const char *label = job_name(w->job);
        if (w->job == J_FETCH || w->job == J_HAUL || w->job == J_COOKFETCH)
            snprintf(buf, sizeof buf, "%s %s", label, ITEM_NAME[w->item]);
        else snprintf(buf, sizeof buf, "%s", label);
        int sk = job_skill(w->job);
        if (sk >= 0 && c->know[sk] < 0.2f) snprintf(buf, sizeof buf, "Trying to figure out %s", SKILL_NAME[sk]);
        if (start_job(c, w->job, w->x, w->y, w->adj, buf)) {
            c->bx = w->bx; c->by = w->by; c->jitem = w->item; c->jcount = w->count;
            return;
        }
    }
    /* nothing to do: roam or explore */
    int x, y;
    int hx = S.firex >= 0 ? S.firex : c->tx, hy = S.firex >= 0 ? S.firey : c->ty;
    if (frand() < 0.45f && random_known_tile(c, hx, hy, 20, true, &x, &y) && start_job(c, J_EXPLORE, x, y, false, "Exploring")) return;
    if (random_known_tile(c, hx, hy, 3, false, &x, &y) && start_job(c, J_WANDER, x, y, false, "Idling")) {
        c->jcount = 60 + irand(120);
        return;
    }
    c->timer = 20;
}

/* ------------------------------------------------------------------ reality checks */

static bool can_hold_real(Tile *t, int item) {
    return !feat_blocks(t->feat) && t->feat != F_BLUEPRINT && (t->item == I_NONE || (t->item == item && t->icount < MAX_STACK));
}

static bool valid_real(Colonist *c) {
    Tile *t = &G.tiles[IDX(c->jx, c->jy)];
    switch (c->job) {
    case J_EAT: return t->item && ITEM_FOOD[t->item] && t->icount;
    case J_EAT_BUSH: return t->feat == F_BUSH && t->aux > 0;
    case J_SLEEP: return c->jitem == 1 ? true : (t->feat == F_BED && (t->aux == 0 || t->aux == c->id + 1));
    case J_CHOP: return (t->feat == F_TREE || t->feat == F_PINE) && (t->flags & D_CHOP);
    case J_MINE: return t->feat == F_ROCK && (t->flags & D_MINE);
    case J_HARVEST: return t->feat == F_BUSH && t->aux > 0;
    case J_HAUL: return t->item == c->jitem && t->icount && !(t->flags & FL_STOCK);
    case J_STORE: return (t->flags & FL_STOCK) && can_hold_real(t, c->carry);
    case J_FETCH: case J_COOKFETCH: return t->item == c->jitem && t->icount;
    case J_DELIVER: return t->feat == F_BLUEPRINT && BUILDS[t->aux].mat == c->carry && t->bpmat < BUILDS[t->aux].cost;
    case J_BUILD: return t->feat == F_BLUEPRINT && t->bpmat >= BUILDS[t->aux].cost;
    case J_SOW: return (t->flags & FL_FARM) && t->feat == F_NONE && !t->item;
    case J_REAP: return t->feat == F_CROP && t->aux >= 3;
    case J_COOK: return t->feat == F_CAMPFIRE;
    case J_DECON: return (t->flags & D_DECON) && (t->feat != F_NONE || (t->flags & FL_FLOOR));
    default: return true;
    }
}

static void surprise(Colonist *c) {
    mem_observe(c, c->jx, c->jy);
    c->surprises++;
    set_thought(c, TH_EXCLAIM, 60);
    static const char *msg[] = {"Huh? It's not how I remembered", "Wasn't it here...?", "Someone got here first"};
    snprintf(c->status, sizeof c->status, "%s", msg[irand(3)]);
    if ((c->job == J_EAT || c->job == J_EAT_BUSH) && c->hunger > 70 && irand(5) == 0)
        logmsg(0xFFE0C090, "%s was sure there was food at (%d,%d). There wasn't.", c->name, c->jx, c->jy);
    blacklist(c, c->jx, c->jy);
    end_job(c);
    c->timer = 10;
}

/* ------------------------------------------------------------------ movement */

static void replan(Colonist *c) {
    if (path_find(c, c->tx, c->ty, c->jx, c->jy, c->adj) < 0) {
        set_thought(c, TH_QUESTION, 50);
        snprintf(c->status, sizeof c->status, "No way through...");
        blacklist(c, c->jx, c->jy);
        end_job(c);
        c->timer = 10;
    }
}

static void arrive(Colonist *c) {
    c->state = S_WORK;
    c->timer = 0;
    c->work = 0;
    if (!valid_real(c)) surprise(c);
}

static void move_tick(Colonist *c) {
    if (c->pidx >= c->plen) { arrive(c); return; }
    int nx = c->path[c->pidx][0], ny = c->path[c->pidx][1];
    bool diag_blocked = nx != c->tx && ny != c->ty && (!passable(nx, c->ty) || !passable(c->tx, ny));
    if (!passable(nx, ny) || diag_blocked) {
        mem_observe(c, nx, ny); /* bumped into it */
        mem_observe(c, nx, c->ty);
        mem_observe(c, c->tx, ny);
        set_thought(c, TH_EXCLAIM, 30);
        replan(c);
        return;
    }
    float gx = nx + 0.5f, gy = ny + 0.5f, dx = gx - c->x, dy = gy - c->y;
    float d = sqrtf(dx * dx + dy * dy);
    Tile *t = &G.tiles[IDX(c->tx, c->ty)];
    float speed = 3.3f;
    if (t->flags & FL_FLOOR) speed *= 1.35f;
    else if (G.wear[IDX(c->tx, c->ty)] > 96) speed *= 1.15f;
    if (t->terrain == T_SAND) speed *= 0.8f;
    if (c->carry == I_STONE || c->carry == I_WOOD) speed *= 0.85f;
    if (c->hunger > 85 || c->energy < 12) speed *= 0.7f;
    float step = speed / TPS;
    if (fabsf(dx) > 0.01f) c->dir = dx > 0 ? 1 : -1;
    c->anim += step;
    if (d <= step) {
        c->x = gx; c->y = gy;
        c->tx = nx; c->ty = ny;
        c->pidx++;
        int i = IDX(nx, ny);
        int b0 = G.wear[i] >> 5;
        if (G.tiles[i].terrain == T_GRASS && !(G.tiles[i].flags & (FL_FLOOR | FL_FARM))) {
            G.wear[i] = (u8)clampi(G.wear[i] + 4, 0, 255);
            if ((G.wear[i] >> 5) != b0) ground_repaint(nx, ny);
        }
        if (G.tiles[i].terrain == T_SAND || G.wear[i] > 96)
            part_spawn(P_DUST, c->x * TS, c->y * TS + 4, (frand() - 0.5f) * 0.4f, -0.2f, 18, 0xFFB09870, 1);
        if (c->pidx < c->plen && mem_blocks(&c->mem[IDX(c->path[c->pidx][0], c->path[c->pidx][1])])) replan(c);
    } else {
        c->x += dx / d * step;
        c->y += dy / d * step;
    }
}

/* ------------------------------------------------------------------ work */

static float skill_speed(Colonist *c, int sk) { return sk < 0 ? 1.0f : 0.25f + 1.5f * c->know[sk]; }
static bool botch(Colonist *c, int sk) { return sk >= 0 && c->know[sk] < 0.35f && frand() < (0.35f - c->know[sk]) * 2.5f; }

static void face(Colonist *c, int x) { if (x != c->tx) c->dir = x > c->tx ? 1 : -1; }

static void chat_tick(Colonist *c) {
    if (c->chat_timer > 0) {
        Colonist *o = &G.col[c->chat_with];
        float dx = o->x - c->x, dy = o->y - c->y;
        if (!o->alive || o->state == S_SLEEP || dx * dx + dy * dy > 9) { c->chat_timer = 0; c->chat_with = -1; return; }
        c->chat_timer--;
        c->dir = dx > 0 ? 1 : -1;
        if (c->chat_timer % TPS == 0) mem_share(c, o);
        if (c->chat_timer == 0) c->chat_with = -1;
        snprintf(c->status, sizeof c->status, "Chatting with %s", o->name);
        return;
    }
    if ((G.tick + c->id) % 30) return;
    for (int i = 0; i < G.ncol; i++) {
        Colonist *o = &G.col[i];
        if (i == c->id || !o->alive || o->chat_timer || o->state != S_WORK) continue;
        if (o->job != J_SOCIAL && o->job != J_WANDER) continue;
        float dx = o->x - c->x, dy = o->y - c->y;
        if (dx * dx + dy * dy > 6.25f) continue;
        int len = 120 + irand(150);
        c->chat_with = i; c->chat_timer = len;
        o->chat_with = c->id; o->chat_timer = len + 1;
        set_thought(c, TH_HEART, 30);
        return;
    }
}

static void finish_resource(Colonist *c, Tile *t, int item, int good, int bad, int sk) {
    bool b = botch(c, sk);
    t->feat = F_NONE;
    t->aux = 0;
    t->flags &= ~(D_CHOP | D_MINE | D_HARVEST);
    t->resv = -1;
    drop_item(c->jx, c->jy, item, b ? bad : good);
    practice(c, sk, b);
    if (b) { set_thought(c, TH_HUH, 50); snprintf(c->status, sizeof c->status, "Botched it - wasted most of it"); }
    mem_observe(c, c->jx, c->jy);
    end_job(c);
}

static void work_tick(Colonist *c) {
    Tile *t = &G.tiles[IDX(c->jx, c->jy)];
    int sk = job_skill(c->job);
    float sf = skill_speed(c, sk);
    c->timer++;
    face(c, c->jx);
    if (c->job != J_SOCIAL && c->job != J_WANDER && c->job != J_EXPLORE && c->timer % TPS == 0 && !valid_real(c)) { surprise(c); return; }
    switch (c->job) {
    case J_CHOP: case J_MINE: {
        c->workanim++;
        c->work += 2.2f * sf;
        while (c->work >= 1 && t->hp > 0) { t->hp--; c->work -= 1; }
        if (c->timer % 9 == 0) {
            float px = (c->jx + 0.5f) * TS, py = (c->jy + 0.5f) * TS;
            for (int k = 0; k < 3; k++)
                part_spawn(P_CHIP, px - c->dir * 4, py - 4, (frand() - 0.5f) * 1.6f - c->dir * 0.4f, -1 - frand(), 25,
                           c->job == J_CHOP ? 0xFFC89A5A : 0xFFB8B8C0, 1);
            if (c->job == J_CHOP && frand() < 0.5f) part_spawn(P_LEAF, px + (frand() - 0.5f) * 14, py - 22, 0, 0.3f, 90, 0xFF4E9A3E, 1);
            if (c->job == J_MINE && frand() < 0.4f) part_spawn(P_SPARK, px, py - 4, (frand() - 0.5f) * 2, -frand() * 2, 12, 0xFFFFE0A0, 1);
        }
        if (sk >= 0 && c->know[sk] < 0.2f && c->timer % 90 == 0) { set_thought(c, TH_HUH, 40); c->know[sk] += 0.004f; }
        if (t->hp == 0) {
            if (c->job == J_CHOP) finish_resource(c, t, I_WOOD, 4, 2, sk);
            else finish_resource(c, t, I_STONE, 3, 1, sk);
        }
        break;
    }
    case J_HARVEST:
        c->workanim++;
        c->work += sf;
        if (c->work >= 60) {
            bool b = botch(c, sk);
            int n = b ? (t->aux + 1) / 2 : t->aux;
            t->aux = 0; t->timer = 0;
            drop_item(c->tx, c->ty, I_BERRY, n);
            practice(c, sk, b);
            mem_observe(c, c->jx, c->jy);
            end_job(c);
        }
        break;
    case J_EAT:
        if (c->timer == 1) {
            int item = t->item;
            if (!take_item(c->jx, c->jy, item, 1)) { surprise(c); return; }
            c->jitem = item;
            mem_reinforce(c, c->jx, c->jy, 3);
            mem_observe(c, c->jx, c->jy);
        }
        if (c->timer >= 50) { eat(c, c->jitem); end_job(c); }
        break;
    case J_EAT_BUSH:
        c->workanim++;
        if (c->timer == 1) { t->aux--; mem_observe(c, c->jx, c->jy); }
        if (c->timer >= 45) { c->hunger = clampf(c->hunger - 24, 0, 100); end_job(c); }
        break;
    case J_SLEEP:
        if (c->jitem == 0 && t->feat == F_BED) {
            if (t->aux == 0) {
                t->aux = c->id + 1;
                logmsg(0xFFC8C8FF, "%s claimed a bed.", c->name);
            }
            mem_observe(c, c->jx, c->jy);
            mem_reinforce(c, c->jx, c->jy, 12);
        }
        c->sleep_ground = c->jitem == 1;
        c->state = S_SLEEP;
        snprintf(c->status, sizeof c->status, c->sleep_ground ? "Sleeping on the ground" : "Sleeping in bed");
        break;
    case J_SOCIAL: case J_WANDER:
        chat_tick(c);
        if (c->chat_timer == 0) snprintf(c->status, sizeof c->status, c->job == J_SOCIAL ? "Warming by the fire" : "Idling");
        if (c->job == J_SOCIAL && !c->chat_timer && (c->timer > TPS * 25 || !is_social_time())) end_job(c);
        else if (c->job == J_WANDER && !c->chat_timer && c->timer > c->jcount) end_job(c);
        break;
    case J_EXPLORE:
        if (c->timer > 40) end_job(c);
        break;
    case J_HAUL: {
        int n = take_item(c->jx, c->jy, c->jitem, MAX_STACK);
        mem_observe(c, c->jx, c->jy);
        if (!n) { surprise(c); return; }
        c->carry = c->jitem; c->ccount = n;
        int x, y;
        end_job(c);
        if (find_stock(c, c->carry, &x, &y)) start_job(c, J_STORE, x, y, false, "Carrying to the stockpile");
        break;
    }
    case J_STORE: {
        int put = MAX_STACK - (t->item == c->carry ? t->icount : 0);
        if (put > c->ccount) put = c->ccount;
        t->item = c->carry;
        t->icount += put;
        c->ccount -= put;
        if (!c->ccount) c->carry = I_NONE;
        mem_observe(c, c->jx, c->jy);
        mem_reinforce(c, c->jx, c->jy, 4);
        end_job(c);
        break;
    }
    case J_FETCH: {
        int n = take_item(c->jx, c->jy, c->jitem, c->jcount > 0 ? c->jcount : 1);
        mem_observe(c, c->jx, c->jy);
        if (!n) { surprise(c); return; }
        c->carry = c->jitem; c->ccount = n;
        int bx = c->bx, by = c->by;
        end_job(c);
        if (!inb(bx, by) || !start_job(c, J_DELIVER, bx, by, true, "Delivering materials")) break;
        break;
    }
    case J_DELIVER: {
        int need = BUILDS[t->aux].cost - t->bpmat;
        int n = need < c->ccount ? need : c->ccount;
        t->bpmat += n;
        c->ccount -= n;
        if (!c->ccount) c->carry = I_NONE;
        mem_observe(c, c->jx, c->jy);
        int x = c->jx, y = c->jy;
        end_job(c);
        if (t->bpmat >= BUILDS[t->aux].cost && c->know[SK_BUILD] >= 0.2f) start_job(c, J_BUILD, x, y, true, "Building");
        break;
    }
    case J_BUILD:
        c->workanim++;
        c->work += 1.6f * sf;
        while (c->work >= 1 && t->hp > 0) { t->hp--; c->work -= 1; }
        if (c->timer % 12 == 0)
            part_spawn(P_DUST, (c->jx + 0.5f) * TS, (c->jy + 0.5f) * TS, (frand() - 0.5f), -0.5f, 20, 0xFFD8C8A8, 1);
        if (sk >= 0 && c->know[sk] < 0.2f && c->timer % 90 == 0) { set_thought(c, TH_HUH, 40); c->know[sk] += 0.004f; }
        if (t->hp == 0) {
            int b = t->aux;
            if (botch(c, sk)) {
                t->bpmat--;
                t->hp = BUILDS[b].work / 2;
                practice(c, sk, true);
                set_thought(c, TH_HUH, 60);
                snprintf(c->status, sizeof c->status, "It fell apart! Wasted some %s", ITEM_NAME[BUILDS[b].mat]);
                end_job(c);
                break;
            }
            if (b == B_FLOOR) {
                t->feat = F_NONE; t->aux = 0;
                t->flags |= FL_FLOOR;
                ground_repaint(c->jx, c->jy);
            } else {
                t->feat = BUILDS[b].feat; t->aux = 0; t->hp = 0;
                if (feat_blocks(t->feat))
                    for (int i = 0; i < G.ncol; i++) {
                        Colonist *o = &G.col[i];
                        if (!o->alive || o->tx != c->jx || o->ty != c->jy) continue;
                        o->x = c->x; o->y = c->y; o->tx = c->tx; o->ty = c->ty;
                        o->plen = o->pidx = 0;
                    }
            }
            t->bpmat = 0;
            practice(c, sk, false);
            mem_observe(c, c->jx, c->jy);
            end_job(c);
        }
        break;
    case J_SOW:
        c->workanim++;
        c->work += sf;
        if (c->work >= 70) {
            bool b = botch(c, sk);
            if (!b) { t->feat = F_CROP; t->aux = 0; t->timer = 0; }
            else set_thought(c, TH_HUH, 50);
            practice(c, sk, b);
            mem_observe(c, c->jx, c->jy);
            end_job(c);
        }
        break;
    case J_REAP:
        c->workanim++;
        c->work += sf;
        if (c->work >= 70) {
            bool b = botch(c, sk);
            t->feat = F_NONE; t->aux = 0;
            drop_item(c->jx, c->jy, I_VEG, b ? 2 : 4);
            practice(c, sk, b);
            mem_observe(c, c->jx, c->jy);
            end_job(c);
        }
        break;
    case J_COOKFETCH: {
        int n = take_item(c->jx, c->jy, c->jitem, 3);
        mem_observe(c, c->jx, c->jy);
        if (!n) { surprise(c); return; }
        c->carry = c->jitem; c->ccount = n;
        int bx = c->bx, by = c->by;
        end_job(c);
        start_job(c, J_COOK, bx, by, true, "Cooking");
        break;
    }
    case J_COOK:
        c->workanim++;
        c->work += sf;
        if (c->timer % 20 == 0) part_spawn(P_STEAM, (c->jx + 0.5f) * TS, (c->jy + 0.2f) * TS, 0, -0.4f, 50, 0xFFE8E8E8, 1);
        if (sk >= 0 && c->know[sk] < 0.2f && c->timer % 90 == 0) { set_thought(c, TH_HUH, 40); c->know[sk] += 0.004f; }
        if (c->work >= 130) {
            bool b = botch(c, sk);
            practice(c, sk, b);
            if (b) {
                c->carry = I_NONE; c->ccount = 0;
                set_thought(c, TH_HUH, 60);
                snprintf(c->status, sizeof c->status, "Burnt the food to a crisp");
                for (int k = 0; k < 8; k++) part_spawn(P_SMOKE, (c->jx + 0.5f) * TS, c->jy * TS, (frand() - 0.5f) * 0.3f, -0.5f, 80, 0xFF303030, 2);
            } else c->carry = I_MEAL;
            end_job(c);
        }
        break;
    case J_DECON:
        c->workanim++;
        c->work += 1.5f * sf;
        while (c->work >= 1 && t->hp > 0) { t->hp--; c->work -= 1; }
        if (t->hp == 0) {
            if (t->feat != F_NONE) {
                int f = t->feat;
                for (int b = 0; b < B_COUNT; b++)
                    if (BUILDS[b].feat == f && f != F_NONE) { drop_item(c->jx, c->jy, BUILDS[b].mat, (BUILDS[b].cost + 1) / 2); break; }
                t->feat = F_NONE; t->aux = 0;
            } else if (t->flags & FL_FLOOR) {
                t->flags &= ~FL_FLOOR;
                ground_repaint(c->jx, c->jy);
            }
            t->flags &= ~D_DECON;
            practice(c, sk, false);
            mem_observe(c, c->jx, c->jy);
            end_job(c);
        }
        break;
    default:
        end_job(c);
    }
}

/* ------------------------------------------------------------------ main update */

static void die(Colonist *c) {
    drop_carry(c);
    end_job(c);
    c->alive = false;
    c->state = S_DEAD;
    G.deaths++;
    logmsg(0xFFFF6060, "%s has died of hunger. Everything they knew is gone with them.", c->name);
    for (int k = 0; k < 40; k++)
        part_spawn(P_WISP, c->x * TS + (frand() - 0.5f) * 8, c->y * TS - 6, (frand() - 0.5f) * 0.6f, -0.2f - frand() * 0.6f, 100, 0xFFBFD0FF, 1);
    if (G.sel == c->id) { G.sel = -1; G.mindview = false; }
}

void colonist_update(Colonist *c) {
    if (!c->alive) return;
    bool sleeping = c->state == S_SLEEP;
    /* needs */
    c->hunger += 100.0f / (DAY_TICKS * 1.1f);
    if (sleeping) c->energy += 100.0f / (DAY_TICKS * 0.28f) * (c->sleep_ground ? 0.75f : 1.0f);
    else c->energy -= 100.0f / (DAY_TICKS * 0.95f);
    c->energy = clampf(c->energy, 0, 100);
    if (c->hunger >= 100) { c->hunger = 100; c->health -= 100.0f / (DAY_TICKS * 0.9f); }
    else if (c->hunger < 60) c->health = clampf(c->health + 0.01f, 0, 100);
    if (c->health <= 0) { die(c); return; }
    /* senses & memory */
    if (!sleeping && (G.tick + c->id) % 4 == 0) perceive(c);
    if ((G.tick + c->id * 3) % TPS == 0) {
        int sec = G.tick / TPS;
        mem_decay(c, sec);
        skills_tick(c);
        mem_check_losses(c);
        if (sleeping) mem_consolidate(c, sec);
    }
    if (c->thought_timer > 0 && --c->thought_timer == 0) c->thought = TH_NONE;
    /* urgent interrupts */
    if (c->state == S_MOVE || c->state == S_WORK) {
        bool eating = c->job == J_EAT || c->job == J_EAT_BUSH;
        if (c->energy < 8 && c->job != J_SLEEP && !eating && c->carry == I_NONE && c->state == S_WORK && c->job != J_SOCIAL) {
            end_job(c);
            snprintf(c->status, sizeof c->status, "Too tired to go on");
        } else if ((c->hunger > 85 && !eating && c->carry == I_NONE && c->job != J_EXPLORE) || (c->energy <= 0 && c->job != J_SLEEP && !eating)) {
            end_job(c);
            if (c->energy <= 0) {
                c->job = J_SLEEP; c->jitem = 1; c->jx = c->tx; c->jy = c->ty; c->state = S_WORK;
                logmsg(0xFFC0A0FF, "%s collapsed from exhaustion.", c->name);
            }
        }
    }
    switch (c->state) {
    case S_IDLE:
        if (c->timer > 0) { c->timer--; break; }
        decide(c);
        break;
    case S_MOVE: move_tick(c); break;
    case S_WORK: work_tick(c); break;
    case S_SLEEP: {
        if (G.tick % 50 == (u32)c->id) part_spawn(P_ZZZ, c->x * TS + 3, c->y * TS - 8, 0.15f, -0.3f, 70, 0xFFFFFFFF, 1);
        bool wake = c->energy >= 100 || (c->energy > 70 && !is_sleep_time() && hour_of_day() > 6) || (c->hunger > 88 && c->energy > 15);
        if (wake) { end_job(c); snprintf(c->status, sizeof c->status, "Just woke up"); }
        break;
    }
    }
}
