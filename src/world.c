#include "game.h"

static const char *NAMES[] = {"Ada", "Bram", "Cora", "Dax", "Elin", "Finn", "Greta", "Hugo", "Ines", "Joss", "Kira", "Lars",
                              "Mira", "Nils", "Oona", "Pell", "Quin", "Rhea", "Sven", "Tove", "Ulla", "Vik", "Wren", "Yara",
                              "Arlo", "Bea", "Cyd", "Edda", "Fen", "Ivo", "Juno", "Otto"};
static const u32 SKINS[] = {0xFFF2C9A0, 0xFFE0A87A, 0xFFC68642, 0xFF8D5524, 0xFFF7D7C0, 0xFFB47A50};
static const u32 HAIRS[] = {0xFF2B1B12, 0xFF5A3A22, 0xFFA0522D, 0xFFE8C872, 0xFF1A1A1A, 0xFFC0C0C8, 0xFF8B2E1E, 0xFFD9A441};
static const u32 SHIRTS[] = {0xFFD9534F, 0xFF4A90D9, 0xFF5CB85C, 0xFFF0AD4E, 0xFF9B59B6, 0xFF1ABC9C, 0xFFE67E22, 0xFFECF0F1,
                             0xFF34495E, 0xFFE84393, 0xFF6C5CE7, 0xFF00B894};
static const u32 PANTS[] = {0xFF3B3B58, 0xFF5B4636, 0xFF2D3E50, 0xFF4A4A4A, 0xFF6B4E31};
static const struct { const char *name; float memq; int vision; } TRAITS[] = {
    {"Sharp memory", 1.6f, 7}, {"Forgetful", 0.55f, 7}, {"Steady", 1.0f, 7}, {"Absent-minded", 0.75f, 7},
    {"Keen-eyed", 1.0f, 9}, {"Daydreamer", 0.85f, 6}, {"Steady", 1.0f, 7}};
static int name_counter;

bool feat_blocks(int f) {
    return f == F_TREE || f == F_PINE || f == F_BUSH || f == F_ROCK || f == F_WALL || f == F_CAMPFIRE || f == F_TORCH;
}
bool feat_opaque(int f) { return f == F_WALL || f == F_ROCK; }
bool passable(int x, int y) {
    if (!inb(x, y)) return false;
    Tile *t = &G.tiles[IDX(x, y)];
    return t->terrain != T_WATER && !feat_blocks(t->feat);
}

static bool can_hold(Tile *t, int item) {
    if (t->terrain == T_WATER || feat_blocks(t->feat) || t->feat == F_BLUEPRINT) return false;
    return t->item == I_NONE || (t->item == item && t->icount < MAX_STACK);
}

int drop_item(int x, int y, int item, int count) {
    for (int r = 0; r <= 6 && count > 0; r++)
        for (int dy = -r; dy <= r && count > 0; dy++)
            for (int dx = -r; dx <= r && count > 0; dx++) {
                if (abs(dx) != r && abs(dy) != r) continue;
                int nx = x + dx, ny = y + dy;
                if (!inb(nx, ny)) continue;
                Tile *t = &G.tiles[IDX(nx, ny)];
                if (!can_hold(t, item)) continue;
                int put = MAX_STACK - (t->item == item ? t->icount : 0);
                if (put > count) put = count;
                t->item = item;
                t->icount += put;
                count -= put;
            }
    return count;
}

int take_item(int x, int y, int item, int maxc) {
    if (!inb(x, y)) return 0;
    Tile *t = &G.tiles[IDX(x, y)];
    if (t->item != item || !t->icount) return 0;
    int n = t->icount < maxc ? t->icount : maxc;
    t->icount -= n;
    if (!t->icount) t->item = I_NONE;
    return n;
}

int stock_count(int item) {
    int n = 0;
    for (int i = 0; i < MAP_W * MAP_H; i++) {
        Tile *t = &G.tiles[i];
        if (!(t->flags & FL_STOCK) || !t->item) continue;
        if (item == -1 ? ITEM_FOOD[t->item] > 0 : t->item == item) n += t->icount;
    }
    return n;
}

/* ---------------------------------------------------------------- colonists */

int colonist_spawn(int x, int y, bool founder) {
    int id = -1;
    for (int i = 0; i < MAX_COL; i++)
        if (!G.col[i].alive && (i >= G.ncol || G.col[i].state != S_DEAD)) { id = i; break; }
    if (id < 0) return -1;
    if (id >= G.ncol) G.ncol = id + 1;
    Colonist *c = &G.col[id];
    memset(c, 0, sizeof *c);
    c->alive = true;
    c->id = id;
    snprintf(c->name, sizeof c->name, "%s", NAMES[(name_counter++ + G.seed) % (sizeof NAMES / sizeof *NAMES)]);
    int tr = irand(sizeof TRAITS / sizeof *TRAITS);
    c->trait = TRAITS[tr].name;
    c->memq = TRAITS[tr].memq;
    c->vision = TRAITS[tr].vision;
    c->x = x + 0.5f;
    c->y = y + 0.5f;
    c->tx = x;
    c->ty = y;
    c->dir = 1;
    c->skin = SKINS[irand(6)];
    c->hair = HAIRS[irand(8)];
    c->shirt = SHIRTS[irand(12)];
    c->pants = PANTS[irand(5)];
    c->hairstyle = irand(3);
    c->hunger = 10 + frand() * 20;
    c->energy = 70 + frand() * 25;
    c->health = 100;
    c->chat_with = -1;
    c->carry = I_NONE;
    c->state = S_IDLE;
    snprintf(c->status, sizeof c->status, "Looking around");
    if (founder) {
        for (int dy = -12; dy <= 12; dy++)
            for (int dx = -12; dx <= 12; dx++) {
                if (dx * dx + dy * dy > 144 || !inb(x + dx, y + dy)) continue;
                mem_observe(c, x + dx, y + dy);
                c->mem[IDX(x + dx, y + dy)].stab = 40 + irand(30);
                c->mem[IDX(x + dx, y + dy)].str = 200 + irand(55);
            }
    } else {
        for (int s = 0; s < SK_COUNT; s++) c->know[s] = frand() < 0.35f ? 0.45f + frand() * 0.45f : frand() * 0.15f;
        c->know[irand(SK_COUNT)] = 0.8f;
    }
    for (int s = 0; s < SK_COUNT; s++) c->kstab[s] = 0.5f + frand();
    for (int i = 0; i < 6; i++) c->black_t[i] = 0;
    return id;
}

void wanderer_check(void) {
    if ((int)G.tick < G.next_wanderer) return;
    G.next_wanderer = G.tick + DAY_TICKS * 2 + irand(DAY_TICKS * 2);
    int alive = 0;
    for (int i = 0; i < G.ncol; i++) alive += G.col[i].alive;
    if (alive >= 14 || stock_count(-1) < alive * 3) return;
    int fx = -1, fy = -1;
    for (int i = 0; i < MAP_W * MAP_H; i++)
        if (G.tiles[i].feat == F_CAMPFIRE) { fx = i % MAP_W; fy = i / MAP_W; break; }
    if (fx < 0) { fx = MAP_W / 2; fy = MAP_H / 2; }
    for (int tries = 0; tries < 400; tries++) {
        float a = frand() * 6.2831f, r = 24 + frand() * 12;
        int x = fx + (int)(cosf(a) * r), y = fy + (int)(sinf(a) * r);
        if (!passable(x, y)) continue;
        int id = colonist_spawn(x, y, false);
        if (id < 0) return;
        Colonist *c = &G.col[id];
        /* They only heard a rumour: "there's a fire somewhere over there". */
        MemCell *m = &c->mem[IDX(fx, fy)];
        m->feat = F_CAMPFIRE;
        m->terr = T_GRASS;
        m->str = 150;
        m->stab = 20;
        set_thought(c, TH_QUESTION, 90);
        logmsg(0xFF9FD8FF, "A wanderer, %s (%s), arrives. They know nothing of this place.", c->name, c->trait);
        return;
    }
}

/* ---------------------------------------------------------------- generation */

void world_gen(u32 seed) {
    G.seed = seed;
    G.rng = seed * 2654435761u + 12345;
    name_counter = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            float nx = x / (float)MAP_W - 0.5f, ny = y / (float)MAP_H - 0.5f;
            float d = sqrtf(nx * nx + ny * ny) * 2.0f;
            float h = (fbm(x * 0.042f, y * 0.042f, 5, seed) - 0.5f) * 2.3f + 0.5f;
            h += 0.16f * (0.55f - d * d);
            float m = (fbm(x * 0.05f + 40, y * 0.05f, 4, seed + 77) - 0.5f) * 2.2f + 0.5f;
            G.height[IDX(x, y)] = h;
            G.moist[IDX(x, y)] = m;
        }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            Tile *t = &G.tiles[IDX(x, y)];
            memset(t, 0, sizeof *t);
            t->resv = -1;
            float h = G.height[IDX(x, y)], m = G.moist[IDX(x, y)];
            t->terrain = h < 0.36f ? T_WATER : h < 0.405f ? T_SAND : h > 0.74f ? T_ROCKY : T_GRASS;
            float forest = fbm(x * 0.07f, y * 0.07f + 99, 3, seed + 5);
            if (t->terrain == T_GRASS) {
                if (forest > 0.56f && frand() < 0.6f) t->feat = (h > 0.62f || m < 0.42f) ? F_PINE : F_TREE;
                else if (frand() < 0.025f) t->feat = frand() < 0.5f ? F_TREE : F_PINE;
                else if (m > 0.38f && frand() < (forest > 0.45f ? 0.05f : 0.018f)) t->feat = F_BUSH;
                else if (h > 0.68f && frand() < 0.08f) t->feat = F_ROCK;
            } else if (t->terrain == T_ROCKY) {
                if (frand() < 0.42f) t->feat = F_ROCK;
            } else if (t->terrain == T_SAND && frand() < 0.01f) {
                t->feat = F_ROCK;
            }
            if (t->feat == F_TREE || t->feat == F_PINE) t->hp = 320;
            if (t->feat == F_ROCK) t->hp = 420;
            if (t->feat == F_BUSH) t->aux = irand(5);
        }
    /* find a pleasant start: grassy, dry, open */
    int sx = MAP_W / 2, sy = MAP_H / 2, best = -1;
    for (int y = 14; y < MAP_H - 14; y++)
        for (int x = 14; x < MAP_W - 14; x++) {
            if (G.tiles[IDX(x, y)].terrain != T_GRASS) continue;
            int score = 0;
            for (int dy = -7; dy <= 7; dy++)
                for (int dx = -7; dx <= 7; dx++) {
                    Tile *t = &G.tiles[IDX(x + dx, y + dy)];
                    if (t->terrain == T_WATER) score -= (abs(dx) < 4 && abs(dy) < 4) ? 30 : -1;
                    else if (t->terrain == T_GRASS) score += 2;
                    if (t->feat == F_TREE || t->feat == F_PINE) score += 1;
                    if (t->feat == F_BUSH) score += 3;
                }
            score -= (abs(x - MAP_W / 2) + abs(y - MAP_H / 2)) / 2;
            if (score > best) { best = score; sx = x; sy = y; }
        }
    for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 5; dx++) {
            Tile *t = &G.tiles[IDX(sx + dx, sy + dy)];
            if (dx * dx + dy * dy <= 12 || (dx >= 2 && abs(dy) <= 2)) {
                t->feat = F_NONE;
                if (t->terrain == T_WATER) t->terrain = T_GRASS;
            }
        }
    G.tiles[IDX(sx, sy)].feat = F_CAMPFIRE;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = 3; dx <= 5; dx++) G.tiles[IDX(sx + dx, sy + dy)].flags |= FL_STOCK;
    drop_item(sx + 3, sy - 1, I_WOOD, 14);
    drop_item(sx + 4, sy - 1, I_WOOD, 6);
    drop_item(sx + 3, sy, I_BERRY, 16);
    drop_item(sx + 4, sy, I_BERRY, 8);
    drop_item(sx + 3, sy + 1, I_STONE, 6);

    G.ncol = 0;
    int ids[6];
    for (int i = 0; i < 6; i++) {
        float a = i * 1.047f;
        int x = sx + (int)roundf(cosf(a) * 2.2f), y = sy + (int)roundf(sinf(a) * 2.2f);
        if (!passable(x, y)) { x = sx - 1; y = sy + 1; }
        ids[i] = colonist_spawn(x, y, true);
    }
    /* each craft is known well by two founders; others half-remember or never learnt */
    for (int s = 0; s < SK_COUNT; s++) {
        for (int i = 0; i < 6; i++) G.col[ids[i]].know[s] = frand() < 0.4f ? 0.22f + frand() * 0.25f : 0.0f;
        int a = irand(6), b = (a + 1 + irand(5)) % 6;
        G.col[ids[a]].know[s] = 0.75f + frand() * 0.22f;
        G.col[ids[b]].know[s] = 0.6f + frand() * 0.3f;
    }
    G.next_wanderer = DAY_TICKS * 2;
    G.camx = (sx + 0.5f) * TS;
    G.camy = (sy + 0.5f) * TS;
    G.sel = -1;
    G.hovx = G.hovy = -1;
    G.speed = 1;
    world_update_lights();
    world_update_fog();
    logmsg(0xFFFFD27F, "Six settlers light a fire in the wilderness. Day 1.");
    logmsg(0xFFBFBFBF, "No one keeps a map. Each remembers only what they have seen or been told.");
}

/* ---------------------------------------------------------------- designations */

static bool remembered(int x, int y) { return G.fog[IDX(x, y)] > 0; }

void designate(int x0, int y0, int x1, int y1, int tool, int build) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x0 = clampi(x0, 0, MAP_W - 1); x1 = clampi(x1, 0, MAP_W - 1);
    y0 = clampi(y0, 0, MAP_H - 1); y1 = clampi(y1, 0, MAP_H - 1);
    bool single = (tool == TOOL_BUILD && build != B_WALL && build != B_FLOOR);
    if (single) { x1 = x0; y1 = y0; }
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (!remembered(x, y) && tool != TOOL_CANCEL) continue;
            Tile *t = &G.tiles[IDX(x, y)];
            switch (tool) {
            case TOOL_CHOP: if (t->feat == F_TREE || t->feat == F_PINE) { t->flags |= D_CHOP; n++; } break;
            case TOOL_MINE: if (t->feat == F_ROCK) { t->flags |= D_MINE; n++; } break;
            case TOOL_HARVEST: if (t->feat == F_BUSH) { t->flags |= D_HARVEST; n++; } break;
            case TOOL_STOCK:
                if (t->terrain != T_WATER && !feat_blocks(t->feat) && t->feat != F_CROP) { t->flags = (t->flags | FL_STOCK) & ~FL_FARM; n++; }
                break;
            case TOOL_FARM:
                if ((t->terrain == T_GRASS || t->terrain == T_SAND) && t->feat == F_NONE && !(t->flags & FL_FLOOR)) {
                    t->flags = (t->flags | FL_FARM) & ~FL_STOCK;
                    ground_repaint(x, y);
                    n++;
                }
                break;
            case TOOL_BUILD: {
                if (build == B_WALL && x != x0 && x != x1 && y != y0 && y != y1) break;
                if (t->terrain == T_WATER || t->feat != F_NONE || t->item) break;
                if (build == B_FLOOR && (t->flags & FL_FLOOR)) break;
                if (build != B_FLOOR && (t->flags & FL_FARM)) break;
                t->feat = F_BLUEPRINT;
                t->aux = build;
                t->bpmat = 0;
                t->hp = BUILDS[build].work;
                n++;
                break;
            }
            case TOOL_DECON:
                if (t->feat == F_WALL || t->feat == F_DOOR || t->feat == F_BED || t->feat == F_CAMPFIRE || t->feat == F_TORCH ||
                    (t->feat == F_NONE && (t->flags & FL_FLOOR))) {
                    t->flags |= D_DECON;
                    t->hp = 150;
                    n++;
                }
                break;
            case TOOL_CANCEL:
                if (t->feat == F_BLUEPRINT) {
                    int mat = BUILDS[t->aux].mat, got = t->bpmat;
                    t->feat = F_NONE;
                    t->aux = 0;
                    if (got) drop_item(x, y, mat, got);
                }
                if (t->feat == F_CROP) { t->feat = F_NONE; t->aux = 0; }
                if (t->flags & FL_FARM) { t->flags &= ~FL_FARM; ground_repaint(x, y); }
                t->flags &= ~(D_CHOP | D_MINE | D_HARVEST | D_DECON | FL_STOCK);
                break;
            }
        }
    (void)n;
}

/* ---------------------------------------------------------------- lights & fog */

void world_update_lights(void) {
    G.nlights = 0;
    for (int i = 0; i < MAP_W * MAP_H; i++) G.tlight[i] = 0;
    for (int i = 0; i < MAP_W * MAP_H && G.nlights < MAX_LIGHTS; i++) {
        int f = G.tiles[i].feat;
        if (f != F_CAMPFIRE && f != F_TORCH) continue;
        Light *l = &G.lights[G.nlights++];
        l->x = i % MAP_W + 0.5f;
        l->y = i / MAP_W + 0.5f;
        if (f == F_CAMPFIRE) { l->r = 8.5f; l->cr = 1.0f; l->cg = 0.62f; l->cb = 0.3f; l->inten = 1.5f; }
        else { l->r = 5.5f; l->cr = 1.0f; l->cg = 0.72f; l->cb = 0.4f; l->inten = 1.15f; }
        int r = (int)l->r + 1;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int x = (int)l->x + dx, y = (int)l->y + dy;
                if (!inb(x, y)) continue;
                float d = sqrtf((float)(dx * dx + dy * dy)) / l->r;
                if (d < 1) G.tlight[IDX(x, y)] += (1 - d) * l->inten;
            }
    }
}

void world_update_fog(void) {
    for (int i = 0; i < MAP_W * MAP_H; i++) {
        u8 m = 0;
        for (int c = 0; c < G.ncol; c++)
            if (G.col[c].alive && G.col[c].mem[i].str > m) m = G.col[c].mem[i].str;
        G.fog[i] = m;
    }
}

/* ---------------------------------------------------------------- simulation */

void world_update(void) {
    int slice = G.tick % TPS;
    for (int i = slice; i < MAP_W * MAP_H; i += TPS) {
        Tile *t = &G.tiles[i];
        int x = i % MAP_W, y = i / MAP_W;
        if (t->feat == F_BUSH && t->aux < 4) {
            if (++t->timer > 75) { t->aux++; t->timer = 0; }
        } else if (t->feat == F_CROP && t->aux < 3) {
            if (++t->timer > 95) { t->aux++; t->timer = 0; }
        } else if (t->feat == F_NONE && t->terrain == T_GRASS && !t->flags && !t->item && hash2(i, G.tick / TPS, 3) % 60000 == 0) {
            int trees = 0;
            for (int d = 0; d < 4; d++) {
                int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
                if (inb(nx, ny) && (G.tiles[IDX(nx, ny)].feat == F_TREE || G.tiles[IDX(nx, ny)].feat == F_PINE)) trees++;
            }
            if (trees) {
                bool occupied = false;
                for (int c = 0; c < G.ncol; c++)
                    if (G.col[c].alive && G.col[c].tx == x && G.col[c].ty == y) occupied = true;
                if (!occupied) { t->feat = (hash2(x, y, 9) & 1) ? F_TREE : F_PINE; t->hp = 320; }
            }
        }
        if (G.wear[i] && (G.tick / TPS) % 12 == (u32)(i % 12)) {
            int b0 = G.wear[i] >> 5;
            G.wear[i]--;
            if ((G.wear[i] >> 5) != b0) ground_repaint(x, y);
        }
    }
    if (G.tick % 10 == 0) world_update_lights();
    if (G.tick % 10 == 5) world_update_fog();
    wanderer_check();
}

/* ---------------------------------------------------------------- particles */

void part_spawn(int type, float x, float y, float vx, float vy, float life, u32 col, int size) {
    if (G.npart >= MAX_PART) return;
    Particle *p = &G.part[G.npart++];
    p->type = type; p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->life = p->max = life; p->col = col; p->size = size;
}

void particles_update(void) {
    float t = G.tick * 0.05f;
    for (int i = 0; i < G.npart;) {
        Particle *p = &G.part[i];
        p->life -= 1;
        if (p->life <= 0) { G.part[i] = G.part[--G.npart]; continue; }
        switch (p->type) {
        case P_SMOKE: p->vx += 0.004f + sinf(t + p->y * 0.1f) * 0.003f; p->vy *= 0.995f; break;
        case P_EMBER: p->vx += (frand() - 0.5f) * 0.05f; p->vy *= 0.99f; break;
        case P_CHIP: case P_DUST: p->vy += 0.06f; if (p->type == P_DUST) { p->vy -= 0.055f; p->vx *= 0.9f; } break;
        case P_LEAF: p->vx = sinf(t * 2 + p->max) * 0.3f + 0.1f; p->vy = 0.25f; break;
        case P_FIREFLY: p->vx += (frand() - 0.5f) * 0.06f; p->vy += (frand() - 0.5f) * 0.06f; p->vx *= 0.96f; p->vy *= 0.96f; break;
        case P_WISP: p->vx += sinf(t * 3 + p->max) * 0.02f; break;
        default: break;
        }
        p->x += p->vx;
        p->y += p->vy;
        i++;
    }
}
