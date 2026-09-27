#include "game.h"
#include <stdarg.h>

Game G;

const char *ITEM_NAME[I_COUNT] = {"nothing", "wood", "stone", "berries", "vegetables", "meal"};
const int ITEM_FOOD[I_COUNT] = {0, 0, 0, 30, 36, 72};
const char *SKILL_NAME[SK_COUNT] = {"Foraging", "Woodcutting", "Mining", "Building", "Farming", "Cooking"};
const char *FEAT_NAME[F_COUNT] = {"ground", "oak tree", "pine tree", "berry bush", "boulder", "stone wall",
                                  "door", "bed", "campfire", "torch", "crop", "blueprint"};
const BuildDef BUILDS[B_COUNT] = {
    {"Stone wall", I_STONE, 2, F_WALL, 260},
    {"Door", I_WOOD, 2, F_DOOR, 220},
    {"Bed", I_WOOD, 3, F_BED, 300},
    {"Campfire", I_WOOD, 3, F_CAMPFIRE, 200},
    {"Torch", I_WOOD, 1, F_TORCH, 120},
    {"Wood floor", I_WOOD, 1, F_NONE, 110},
};

u32 rng_u32(void) {
    u32 x = G.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return G.rng = x ? x : 0x9E3779B9u;
}
float frand(void) { return (rng_u32() >> 8) / 16777216.0f; }
int irand(int n) { return n > 0 ? (int)(rng_u32() % (u32)n) : 0; }

u32 hash2(int x, int y, u32 s) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float hashf(int x, int y, u32 s) { return (hash2(x, y, s) & 0xFFFFFF) / 16777216.0f; }

float vnoise(float x, float y, u32 s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hashf(xi, yi, s), b = hashf(xi + 1, yi, s);
    float c = hashf(xi, yi + 1, s), d = hashf(xi + 1, yi + 1, s);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
float fbm(float x, float y, int oct, u32 s) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < oct; i++) {
        sum += vnoise(x, y, s + i * 101) * amp;
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
    }
    return sum / norm;
}

u32 col_lerp(u32 a, u32 b, float t) {
    t = clampf(t, 0, 1);
    return RGB((int)lerpf(CR(a), CR(b), t), (int)lerpf(CG(a), CG(b), t), (int)lerpf(CB(a), CB(b), t));
}
u32 col_mul(u32 c, float f) { return RGB((int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }

void logmsg(u32 col, const char *fmt, ...) {
    if (G.nlog == MAX_LOG) {
        memmove(G.log, G.log + 1, sizeof(LogMsg) * (MAX_LOG - 1));
        G.nlog--;
    }
    LogMsg *m = &G.log[G.nlog++];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->text, sizeof m->text, fmt, ap);
    va_end(ap);
    m->col = col;
    m->tick = SDL_GetTicks();
}

bool inb(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
float hour_of_day(void) { return fmodf((G.tick % DAY_TICKS) * 24.0f / DAY_TICKS + 7.0f, 24.0f); }
int day_number(void) { return 1 + (int)((G.tick + DAY_TICKS * 7 / 24) / DAY_TICKS); }
bool is_night(void) {
    float h = hour_of_day();
    return h < 5.5f || h >= 20.5f;
}
