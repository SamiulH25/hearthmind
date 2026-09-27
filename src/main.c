#include "game.h"

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *fbtex;
static int fbtw, fbth;

static void sim_tick(void) {
    world_update();
    for (int i = 0; i < G.ncol; i++) colonist_update(&G.col[i]);
    if (G.tick % TPS == 0) observe_learning();
    G.tick++;
}

static void ensure_fb(void) {
    int w, h;
    SDL_GetWindowSizeInPixels(win, &w, &h);
    int fw = (w + G.scale - 1) / G.scale, fh = (h + G.scale - 1) / G.scale;
    if (fw != FBW || fh != FBH || !FB) render_resize(w, h);
    if (!fbtex || fbtw != FBW || fbth != FBH) {
        if (fbtex) SDL_DestroyTexture(fbtex);
        fbtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, FBW, FBH);
        SDL_SetTextureScaleMode(fbtex, SDL_SCALEMODE_NEAREST);
        fbtw = FBW; fbth = FBH;
    }
    SDL_GetWindowSize(win, &G.winw, &G.winh);
}

static void clamp_cam(void) {
    G.camx = clampf(G.camx, FBW / 2.0f - 64, WORLD_PW - FBW / 2.0f + 64);
    G.camy = clampf(G.camy, FBH / 2.0f - 64, WORLD_PH - FBH / 2.0f + 64);
    if (FBW > WORLD_PW) G.camx = WORLD_PW / 2.0f;
    if (FBH > WORLD_PH) G.camy = WORLD_PH / 2.0f;
}

static void draw(void) {
    ensure_fb();
    clamp_cam();
    render_frame();
    SDL_UpdateTexture(fbtex, NULL, FB, FBW * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    int pw, ph;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    float k = G.winw / (float)pw;
    SDL_FRect d = {0, 0, FBW * G.scale * k, FBH * G.scale * k};
    SDL_RenderTexture(ren, fbtex, NULL, &d);
    ui_draw(ren);
}

static void pick_colonist(float sx, float sy) {
    int cx0 = (int)floorf(G.camx - FBW / 2.0f), cy0 = (int)floorf(G.camy - FBH / 2.0f);
    float wx = cx0 + sx / G.scale, wy = cy0 + sy / G.scale;
    int best = -1;
    float bd = 14 * 14;
    for (int i = 0; i < G.ncol; i++) {
        Colonist *c = &G.col[i];
        if (!c->alive) continue;
        float dx = c->x * TS - wx, dy = c->y * TS - 4 - wy;
        float d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    G.sel = best;
    if (best < 0) G.mindview = false;
}

static void set_zoom(int s) {
    G.scale = clampi(s, 2, 5);
}

static void handle(SDL_Event *e) {
    static bool panning;
    static float px, py;
    switch (e->type) {
    case SDL_EVENT_QUIT: G.quit = true; break;
    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode k = e->key.key;
        if (k == SDLK_ESCAPE) { if (G.help) G.help = false; else if (G.tool) G.tool = TOOL_SELECT; else { G.sel = -1; G.mindview = false; } }
        else if (k == SDLK_SPACE) G.paused = !G.paused;
        else if (k == SDLK_F1 || k == SDLK_SLASH) G.help = !G.help;
        else if (k >= SDLK_1 && k <= SDLK_6 && G.tool == TOOL_BUILD) G.build = k - SDLK_1;
        else if (k >= SDLK_1 && k <= SDLK_4) { G.speed = 1 << (k - SDLK_1); G.paused = false; }
        else if (k == SDLK_Q) G.tool = TOOL_SELECT;
        else if (k == SDLK_C) G.tool = TOOL_CHOP;
        else if (k == SDLK_X) G.tool = TOOL_MINE;
        else if (k == SDLK_H) G.tool = TOOL_HARVEST;
        else if (k == SDLK_Z) G.tool = TOOL_STOCK;
        else if (k == SDLK_F) G.tool = TOOL_FARM;
        else if (k == SDLK_B) G.tool = TOOL_BUILD;
        else if (k == SDLK_V) G.tool = TOOL_DECON;
        else if (k == SDLK_R) G.tool = TOOL_CANCEL;
        else if (k == SDLK_M) { if (G.sel >= 0) G.mindview = !G.mindview; }
        else if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) set_zoom(G.scale + 1);
        else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) set_zoom(G.scale - 1);
        else if (k == SDLK_TAB) {
            for (int n = 1; n <= G.ncol; n++) {
                int i = ((G.sel < 0 ? -1 : G.sel) + n) % G.ncol;
                if (G.col[i].alive) { G.sel = i; G.camx = G.col[i].x * TS; G.camy = G.col[i].y * TS; break; }
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        set_zoom(G.scale + (e->wheel.y > 0 ? 1 : -1));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (ui_event(e)) break;
        if (e->button.button == SDL_BUTTON_MIDDLE) { panning = true; px = e->button.x; py = e->button.y; }
        else if (e->button.button == SDL_BUTTON_RIGHT) { if (G.tool) G.tool = TOOL_SELECT; else { G.sel = -1; G.mindview = false; } G.dragging = false; }
        else if (e->button.button == SDL_BUTTON_LEFT) {
            if (G.tool == TOOL_SELECT) pick_colonist(e->button.x, e->button.y);
            else {
                int tx, ty;
                screen_to_tile(e->button.x, e->button.y, &tx, &ty);
                G.dragging = true;
                G.dragx0 = G.dragx1 = tx;
                G.dragy0 = G.dragy1 = ty;
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button == SDL_BUTTON_MIDDLE) panning = false;
        if (e->button.button == SDL_BUTTON_LEFT && G.dragging) {
            G.dragging = false;
            designate(G.dragx0, G.dragy0, G.dragx1, G.dragy1, G.tool, G.build);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        screen_to_tile(e->motion.x, e->motion.y, &G.hovx, &G.hovy);
        if (G.dragging) { G.dragx1 = G.hovx; G.dragy1 = G.hovy; }
        if (panning) {
            G.camx -= (e->motion.x - px) / G.scale;
            G.camy -= (e->motion.y - py) / G.scale;
            px = e->motion.x; py = e->motion.y;
        }
        break;
    }
    }
}

int main(int argc, char **argv) {
    u32 seed = (u32)SDL_GetPerformanceCounter();
    int shot_ticks = -1;
    const char *shot_file = NULL;
    int shot_sel = -1;
    bool shot_mind = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (u32)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_ticks = atoi(argv[++i]); shot_file = argv[++i]; }
        else if (!strcmp(argv[i], "--sel") && i + 1 < argc) shot_sel = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mind")) shot_mind = true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    win = SDL_CreateWindow("Hearthmind", 1600, 900, SDL_WINDOW_RESIZABLE | (shot_file ? SDL_WINDOW_HIDDEN : 0));
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    ren = SDL_CreateRenderer(win, shot_file ? SDL_SOFTWARE_RENDERER : NULL);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(ren, 1);

    G.scale = 3;
    sprites_init();
    render_init();
    world_gen(seed);
    render_ground_build();
    ui_init(ren);
    G.help = !shot_file;
    printf("Hearthmind seed %u\n", seed);

    if (shot_file) {
        for (int i = 0; i < shot_ticks; i++) {
            sim_tick();
            if (i % 2 == 0) particles_update();
            if (getenv("AUTO") && i == 200) {
                int fx = 0, fy = 0;
                for (int k = 0; k < MAP_W * MAP_H; k++) if (G.tiles[k].feat == F_CAMPFIRE) { fx = k % MAP_W; fy = k / MAP_W; }
                designate(fx - 10, fy - 10, fx + 10, fy + 10, TOOL_CHOP, 0);
                designate(fx - 12, fy - 12, fx + 12, fy + 12, TOOL_HARVEST, 0);
                designate(fx - 5, fy + 3, fx - 2, fy + 5, TOOL_FARM, 0);
                for (int b = 0; b < 6; b++) designate(fx - 3 + b, fy - 4, fx - 3 + b, fy - 4, TOOL_BUILD, B_BED);
                designate(fx + 6, fy + 3, fx + 6, fy + 3, TOOL_BUILD, B_TORCH);
                designate(fx - 4, fy - 6, fx + 3, fy - 5, TOOL_BUILD, B_FLOOR);
            }
            if (getenv("TRACE") && i % 900 == 0) {
                printf("--- t=%d day %d %.1fh food=%d\n", i, day_number(), hour_of_day(), stock_count(-1));
                for (int c = 0; c < G.ncol; c++) {
                    Colonist *k = &G.col[c];
                    if (!k->alive) continue;
                    printf("  %-5s st=%d job=%-2d H%3.0f E%3.0f hp%3.0f carry=%d mem=%.2f stock=%d fire=%d | %s\n", k->name, k->state, k->job,
                           k->hunger, k->energy, k->health, k->carry, mem_known_frac(k), k->known_stock, k->known_fire, k->status);
                }
            }
        }
        G.sel = shot_sel;
        G.mindview = shot_mind && shot_sel >= 0;
        if (shot_sel >= 0) { G.camx = G.col[shot_sel].x * TS; G.camy = G.col[shot_sel].y * TS; }
        for (int i = 0; i < 20; i++) particles_update();
        if (getenv("BENCH")) {
            G.scale = atoi(getenv("BENCH"));
            Uint64 t0 = SDL_GetPerformanceCounter();
            for (int i = 0; i < 60; i++) { ensure_fb(); render_frame(); }
            printf("render %.2f ms/frame at %dx%d\n", (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / 60, FBW, FBH);
            t0 = SDL_GetPerformanceCounter();
            for (int i = 0; i < 3000; i++) sim_tick();
            printf("sim %.3f ms/tick\n", (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / 3000);
        }
        draw();
        SDL_Surface *s = SDL_RenderReadPixels(ren, NULL);
        if (s) { SDL_SaveBMP(s, shot_file); SDL_DestroySurface(s); }
        else fprintf(stderr, "readback: %s\n", SDL_GetError());
        if (getenv("TRACE")) {
            int cnt[F_COUNT] = {0}, fl = 0;
            for (int k = 0; k < MAP_W * MAP_H; k++) { cnt[G.tiles[k].feat]++; fl += !!(G.tiles[k].flags & FL_FLOOR); }
            int farms = 0; for (int k = 0; k < MAP_W * MAP_H; k++) farms += !!(G.tiles[k].flags & FL_FARM);
            printf("farms=%d ", farms);
            printf("beds=%d bp=%d torch=%d crops=%d floors=%d walls=%d\n", cnt[F_BED], cnt[F_BLUEPRINT], cnt[F_TORCH], cnt[F_CROP], fl, cnt[F_WALL]);
        }
        for (int i = G.nlog > 12 ? G.nlog - 12 : 0; i < G.nlog; i++) printf("  %s\n", G.log[i].text);
        SDL_Quit();
        return 0;
    }

    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0;
    while (!G.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(&e);
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        if (dt > 0.1) dt = 0.1;
        const bool *keys = SDL_GetKeyboardState(NULL);
        float pan = 520 * dt / G.scale * 2;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) G.camy -= pan;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) G.camy += pan;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) G.camx -= pan;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) G.camx += pan;
        if (!G.paused && !G.help) {
            acc += dt * TPS * G.speed;
            int n = 0;
            while (acc >= 1 && n < 400) { sim_tick(); acc -= 1; n++; }
            if (acc > 4) acc = 0;
        }
        particles_update();
        draw();
        SDL_RenderPresent(ren);
    }
    SDL_Quit();
    return 0;
}
