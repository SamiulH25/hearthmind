// HEARTHMIND — a colony sim where nobody has a map but their own memory.
#ifndef GAME_H
#define GAME_H

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAP_W 128
#define MAP_H 96
#define TS 16
#define WORLD_PW (MAP_W * TS)
#define WORLD_PH (MAP_H * TS)
#define TPS 30
#define DAY_TICKS 7200 /* 4 real minutes per day at 1x */
#define MAX_COL 24
#define MAX_PART 6000
#define MAX_LOG 40
#define MAX_PATH 480
#define MAX_STACK 20
#define MAX_LIGHTS 256
#define IDX(x, y) ((y) * MAP_W + (x))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t i8;
typedef int16_t i16;

enum { T_WATER, T_SAND, T_GRASS, T_ROCKY };
enum { F_NONE, F_TREE, F_PINE, F_BUSH, F_ROCK, F_WALL, F_DOOR, F_BED, F_CAMPFIRE, F_TORCH, F_CROP, F_BLUEPRINT, F_COUNT };
enum { B_WALL, B_DOOR, B_BED, B_CAMPFIRE, B_TORCH, B_FLOOR, B_COUNT };
enum { I_NONE, I_WOOD, I_STONE, I_BERRY, I_VEG, I_MEAL, I_COUNT };
enum { FL_STOCK = 1, FL_FARM = 2, FL_FLOOR = 4, D_CHOP = 8, D_MINE = 16, D_HARVEST = 32, D_DECON = 64 };
enum { SK_FORAGE, SK_WOOD, SK_MINE, SK_BUILD, SK_FARM, SK_COOK, SK_COUNT };

enum {
    J_NONE, J_EAT, J_EAT_BUSH, J_SLEEP, J_SOCIAL, J_CHOP, J_MINE, J_HARVEST, J_HAUL, J_STORE,
    J_FETCH, J_DELIVER, J_BUILD, J_SOW, J_REAP, J_COOKFETCH, J_COOK, J_EXPLORE, J_WANDER, J_DECON,
    J_COUNT
};
enum { S_IDLE, S_MOVE, S_WORK, S_SLEEP, S_DEAD };
enum {
    TH_NONE, TH_QUESTION, TH_EXCLAIM, TH_BULB, TH_FORGET, TH_HEART, TH_ZZZ, TH_FOOD, TH_HUH,
    TH_TREE, TH_ROCK, TH_BUSH, TH_FIRE, TH_BED, TH_BOX, TH_HAMMER, TH_WOOD, TH_STONE, TH_BERRY,
    TH_VEG, TH_MEAL, TH_BOOK, TH_COUNT
};
enum { TOOL_SELECT, TOOL_CHOP, TOOL_MINE, TOOL_HARVEST, TOOL_STOCK, TOOL_FARM, TOOL_BUILD, TOOL_DECON, TOOL_CANCEL, TOOL_COUNT };

typedef struct {
    u8 terrain, feat, aux, flags, item, icount, bpmat;
    i8 resv;
    u16 hp, timer;
} Tile;

/* One remembered tile. str = how vividly it is remembered (0 = unknown),
   stab = how resistant it is to fading (grows with spaced re-exposure). */
typedef struct {
    u8 terr, feat, aux, flags, item, icount, str, stab;
} MemCell;

typedef struct {
    bool alive;
    int id;
    char name[16];
    const char *trait;
    float memq; /* >1 remembers longer */
    float x, y;
    int tx, ty, dir;
    u32 skin, hair, shirt, pants;
    int hairstyle;
    float hunger, energy, health;
    float know[SK_COUNT], kstab[SK_COUNT];
    bool forgot_flag[SK_COUNT];
    MemCell mem[MAP_W * MAP_H];

    i16 path[MAX_PATH][2];
    int plen, pidx;

    int state, job, jx, jy, bx, by, jitem, jcount;
    bool adj;
    float work;
    int timer;
    int carry, ccount;

    int thought, thought_timer;
    char status[64];
    float anim;
    int workanim;
    int chat_with, chat_timer;
    int vision;
    int black_x[6], black_y[6], black_t[6];
    int known_stock, known_fire, known_bed;
    int surprises, lessons;
    int idle_ticks;
    int sleep_ground;
} Colonist;

typedef struct {
    float x, y, vx, vy, life, max;
    u32 col;
    u8 type, size;
} Particle;
enum { P_SMOKE, P_EMBER, P_CHIP, P_SPARK, P_LEAF, P_DUST, P_SPARKLE, P_WISP, P_FIREFLY, P_ZZZ, P_STEAM };

typedef struct {
    char text[112];
    u32 col;
    u32 tick;
} LogMsg;

typedef struct { float x, y, r; float cr, cg, cb; float inten; } Light;

typedef struct {
    const char *name;
    int mat, cost, feat, work;
} BuildDef;

typedef struct {
    Tile tiles[MAP_W * MAP_H];
    float height[MAP_W * MAP_H], moist[MAP_W * MAP_H];
    u8 wear[MAP_W * MAP_H];
    u8 fog[MAP_W * MAP_H];
    float tlight[MAP_W * MAP_H];
    Colonist col[MAX_COL];
    int ncol;
    Particle part[MAX_PART];
    int npart;
    LogMsg log[MAX_LOG];
    int nlog;
    Light lights[MAX_LIGHTS];
    int nlights;
    u32 tick, rng, seed;
    int speed;
    bool paused;
    int next_wanderer;
    int deaths;
    /* view */
    float camx, camy;
    int scale;
    int sel;
    bool mindview, help;
    int tool, build;
    bool dragging;
    int dragx0, dragy0, dragx1, dragy1;
    int hovx, hovy;
    int winw, winh;
    bool quit;
} Game;

extern Game G;
extern const BuildDef BUILDS[B_COUNT];
extern const char *ITEM_NAME[I_COUNT];
extern const char *SKILL_NAME[SK_COUNT];
extern const char *FEAT_NAME[F_COUNT];
extern const int ITEM_FOOD[I_COUNT];

/* util.c */
u32 rng_u32(void);
float frand(void);
int irand(int n);
u32 hash2(int x, int y, u32 s);
float hashf(int x, int y, u32 s);
float vnoise(float x, float y, u32 s);
float fbm(float x, float y, int oct, u32 s);
static inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline u32 RGB(int r, int g, int b) {
    return 0xFF000000u | ((u32)clampi(r, 0, 255) << 16) | ((u32)clampi(g, 0, 255) << 8) | (u32)clampi(b, 0, 255);
}
static inline int CR(u32 c) { return (c >> 16) & 255; }
static inline int CG(u32 c) { return (c >> 8) & 255; }
static inline int CB(u32 c) { return c & 255; }
u32 col_lerp(u32 a, u32 b, float t);
u32 col_mul(u32 c, float f);
void logmsg(u32 col, const char *fmt, ...);
bool inb(int x, int y);
float hour_of_day(void);
int day_number(void);
bool is_night(void);

/* world.c */
void world_gen(u32 seed);
bool feat_blocks(int f);
bool feat_opaque(int f);
bool passable(int x, int y);
int drop_item(int x, int y, int item, int count);
int take_item(int x, int y, int item, int maxc);
void world_update(void);
void world_update_lights(void);
void world_update_fog(void);
int stock_count(int item);
void designate(int x0, int y0, int x1, int y1, int tool, int build);
void part_spawn(int type, float x, float y, float vx, float vy, float life, u32 col, int size);
void particles_update(void);
void wanderer_check(void);
int colonist_spawn(int x, int y, bool founder);

/* memory.c */
void mem_observe(Colonist *c, int x, int y);
void mem_reinforce(Colonist *c, int x, int y, int amount);
void perceive(Colonist *c);
bool los(int x0, int y0, int x1, int y1);
bool sees_now(Colonist *c, int x, int y);
void mem_decay(Colonist *c, int sec);
void mem_consolidate(Colonist *c, int sec);
void mem_share(Colonist *spk, Colonist *lis);
void skills_tick(Colonist *c);
void practice(Colonist *c, int sk, bool botched);
void observe_learning(void);
float mem_known_frac(Colonist *c);
void mem_check_losses(Colonist *c);
void set_thought(Colonist *c, int th, int ticks);
bool mem_blocks(const MemCell *m);

/* path.c */
int path_find(Colonist *c, int sx, int sy, int gx, int gy, bool adj);

/* colonist.c */
void colonist_update(Colonist *c);
const char *job_name(int j);
int job_skill(int j);

/* sprites.c */
typedef struct { int w, h; u32 *p; } Spr;
enum {
    SP_TREE0, SP_TREE1, SP_TREE2, SP_PINE0, SP_PINE1, SP_BUSH0, SP_BUSH1, SP_BERRYBUSH0, SP_BERRYBUSH1,
    SP_ROCK0, SP_ROCK1, SP_ROCK2, SP_ITEM0, /* + item index */
    SP_CROP0 = SP_ITEM0 + I_COUNT, SP_CROP1, SP_CROP2, SP_CROP3,
    SP_TH0, /* + thought index */
    SP_TOOL0 = SP_TH0 + TH_COUNT, /* + tool index */
    SP_BUILD0 = SP_TOOL0 + TOOL_COUNT, /* + build index */
    SP_COUNT = SP_BUILD0 + B_COUNT
};
extern Spr SPR[SP_COUNT];
void sprites_init(void);
int thought_for_feat(int feat);
int thought_for_item(int item);

/* render.c */
void render_init(void);
void render_resize(int w, int h);
void render_ground_build(void);
void ground_repaint(int tx, int ty);
void render_frame(void);
void draw_colonist_to(u32 *buf, int bw, int bh, int ox, int oy, Colonist *c, int frame, bool lying);
extern u32 *FB;
extern int FBW, FBH;

/* ui.c */
void ui_init(SDL_Renderer *r);
void ui_draw(SDL_Renderer *r);
bool ui_event(SDL_Event *e);
void screen_to_tile(float sx, float sy, int *tx, int *ty);

#endif
