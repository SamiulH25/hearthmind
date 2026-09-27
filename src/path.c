// A* over a colonist's *beliefs*: known obstacles are avoided, unknown tiles
// are optimistically assumed walkable. Reality corrects them on the way.
#include "game.h"

#define N (MAP_W * MAP_H)
static int gcost[N], parent[N], heap[N], hpos[N];
static u32 stamp[N], closed[N], gen;
static int hn;

static int fkey(int i, int gx, int gy) {
    int dx = abs(i % MAP_W - gx), dy = abs(i / MAP_W - gy);
    int h = (dx > dy) ? 10 * (dx - dy) + 14 * dy : 10 * (dy - dx) + 14 * dx;
    return gcost[i] + h;
}
static int GX, GY;
static void hswap(int a, int b) {
    int t = heap[a]; heap[a] = heap[b]; heap[b] = t;
    hpos[heap[a]] = a; hpos[heap[b]] = b;
}
static void hup(int k) {
    while (k > 0) {
        int p = (k - 1) / 2;
        if (fkey(heap[p], GX, GY) <= fkey(heap[k], GX, GY)) break;
        hswap(p, k); k = p;
    }
}
static void hdown(int k) {
    for (;;) {
        int l = 2 * k + 1, r = l + 1, m = k;
        if (l < hn && fkey(heap[l], GX, GY) < fkey(heap[m], GX, GY)) m = l;
        if (r < hn && fkey(heap[r], GX, GY) < fkey(heap[m], GX, GY)) m = r;
        if (m == k) return;
        hswap(m, k); k = m;
    }
}

static int step_cost(Colonist *c, int i) {
    MemCell *m = &c->mem[i];
    if (!m->str) return 13;
    if (mem_blocks(m)) return -1;
    if (m->flags & FL_FLOOR) return 7;
    if (m->terr == T_SAND) return 12;
    return G.wear[i] > 96 ? 8 : 10;
}

int path_find(Colonist *c, int sx, int sy, int gx, int gy, bool adj) {
    c->plen = c->pidx = 0;
    if (!inb(gx, gy)) return -1;
    if (adj ? (abs(sx - gx) <= 1 && abs(sy - gy) <= 1) : (sx == gx && sy == gy)) return 0;
    if (!adj && mem_blocks(&c->mem[IDX(gx, gy)])) return -1;
    gen++;
    GX = gx; GY = gy;
    hn = 0;
    int s = IDX(sx, sy);
    gcost[s] = 0; parent[s] = -1; stamp[s] = gen;
    heap[hn] = s; hpos[s] = hn++;
    int found = -1, expansions = 0;
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (hn && expansions < N) {
        int cur = heap[0];
        hswap(0, --hn); hdown(0);
        closed[cur] = gen;
        expansions++;
        int cx = cur % MAP_W, cy = cur / MAP_W;
        if (adj ? (abs(cx - gx) <= 1 && abs(cy - gy) <= 1 && !(cx == gx && cy == gy)) : cur == IDX(gx, gy)) { found = cur; break; }
        for (int d = 0; d < 8; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (!inb(nx, ny)) continue;
            int ni = IDX(nx, ny);
            if (closed[ni] == gen) continue;
            if (adj && nx == gx && ny == gy) continue;
            int sc = step_cost(c, ni);
            if (sc < 0) continue;
            if (d >= 4) {
                if (step_cost(c, IDX(cx + DX[d], cy)) < 0 || step_cost(c, IDX(cx, cy + DY[d])) < 0) continue;
                sc = sc * 14 / 10;
            }
            int ng = gcost[cur] + sc;
            if (stamp[ni] != gen) {
                stamp[ni] = gen; gcost[ni] = ng; parent[ni] = cur;
                heap[hn] = ni; hpos[ni] = hn; hup(hn++);
            } else if (ng < gcost[ni]) {
                gcost[ni] = ng; parent[ni] = cur;
                hup(hpos[ni]);
            }
        }
    }
    if (found < 0) return -1;
    int len = 0;
    for (int i = found; i != s; i = parent[i]) len++;
    if (len > MAX_PATH) return -1;
    int k = len;
    for (int i = found; i != s; i = parent[i]) {
        k--;
        c->path[k][0] = i % MAP_W;
        c->path[k][1] = i / MAP_W;
    }
    c->plen = len;
    return len;
}
