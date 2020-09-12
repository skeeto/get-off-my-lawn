#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <pc.h>
#include <conio.h>
#include <dpmi.h>
#include <unistd.h>
#include <sys/nearptr.h>

#include "gun.h"
#include "back.h"
#include "monster.h"
#include "gameover.h"
#include "goml.h"

#define countof(a) ((int)(sizeof(a)/sizeof(a[0])))

#define W 320
#define H 200
#define FPS 30
#define GRIDW 16
#define GRIDH 10
#define GRIDS 20
#define TARGET_X 9
#define TARGET_Y 9
#define START_X 5
#define START_Y 0
#define GUNRANGE 2.5f

static unsigned char vga[H][W];

static void
vga_init(void)
{
    __dpmi_regs regs = {.x = {.ax = 0x13}};
    __dpmi_int(0x10, &regs);
}

static void
vga_free(void)
{
    __dpmi_regs regs = {.x = {.ax = 0x03}};
    __dpmi_int(0x10, &regs);
}

static void 
vga_flush(void)
{
    if (__djgpp_nearptr_enable() == 0) abort();
    unsigned char *p = (unsigned char *)__djgpp_conventional_base + 0xa0000;
    memcpy(p, vga, sizeof(vga));
    __djgpp_nearptr_disable();
}

static void
vga_fill(int x, int y, const unsigned char *buf, int w, int h)
{
    for (int py = 0; py < h; py++) {
        if (y+py < 0 || y+py >= H) continue;
        for (int px = 0; px < w; px++) {
            int v = buf[py*w+px];
            if (v != 255 && x+px >= 0 && x+px < W) {
                vga[y+py][x+px] = v;
            }
        }
    }
}

#if 0
static void
vsync(void)
{
    while (inportb(0x3DA) & 8);
    while (!(inportb(0x3DA) & 8));
}
#endif

static double
now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec/1e6;
}

enum {KEY_N=300, KEY_S, KEY_E, KEY_W, KEY_NW, KEY_NE, KEY_SE, KEY_SW};

int
xgetch(void)
{
    int result = getch();
    if (result != 0xE0 && result != 0x00) {
        return result;
    } else {
        result = getch();
        switch (result) {
            case 72: return KEY_N;
            case 80: return KEY_S;
            case 75: return KEY_W;
            case 77: return KEY_E;
            case 71: return KEY_NW;
            case 73: return KEY_NE;
            case 79: return KEY_SW;
            case 81: return KEY_SE;
            default: return result + 256;
        }
    }
}

static void 
tone_on(void)
{
    outportb(0x61, inportb(0x61) | 0x03);
}

static void 
tone_off(void)
{
    outportb(0x61, inportb(0x61) & ~0x03);
}

static void 
tone(unsigned frequency)
{
    unsigned short period = 1193180 / frequency;
    outportb(0x42, period & 0xff);
    outportb(0x42, period >> 8);
}

struct note {
    short freq;
    short duration;
};

struct {
    const struct note *notes;
    size_t len;
    size_t i;
    double deadline;
    short priority;
} snd_current;

static void
snd_tick(void)
{
    double t = now();
    if (t >= snd_current.deadline) {
        if (snd_current.len == 0) {
            tone_off();
            snd_current.priority = 0;
        } else {
            snd_current.deadline = t + snd_current.notes->duration/1e3;
            int freq = snd_current.notes->freq;
            if (freq) {
                tone(freq);
                tone_on();
            } else {
                tone_off();
            }
            snd_current.notes++;
            snd_current.len--;
        }
    }
}

static void
snd_play(const struct note *notes, size_t len, short priority)
{
    if (priority > snd_current.priority) {
        snd_current.notes = notes;
        snd_current.len = len;
        snd_current.i = 0;
        snd_current.deadline = 0;
        snd_current.priority = priority;
        snd_tick();
    }
}

static void
box(int x, int y, int c)
{
    for (int i = 0; i < GRIDS - 1; i++) {
        vga[y*GRIDS][x*GRIDS+i] = c;
        vga[y*GRIDS+GRIDS-1][x*GRIDS+i] = c;
        vga[y*GRIDS+i][x*GRIDS] = c;
        vga[y*GRIDS+i][x*GRIDS+GRIDS-1] = c;
    }
    vga[y*GRIDS+GRIDS-1][x*GRIDS+GRIDS-1] = c;
}

static inline int
clamp(int v, int min, int max)
{
    if (v < min)
        return min;
    if (v > max)
        return max;
    return v;
}

static const struct note fx_drop[] = {
    {400, 30},
    {450, 30}
};

static const struct note fx_fire[] = {
    {20, 1},
};

static const struct note fx_die[] = {
    {50,  7},
    {75,  7},
    {100, 7},
    {150, 7},
    {200, 1},
    {250, 1},
    {300, 1},
    {370, 1},
    {360, 1},
    {350, 1},
    {340, 1},
    {330, 1},
    {320, 1},
    {310, 1}
};

#include "intro.h"
#include "outro.h"

static const int dirs[] = {
    +0, -1,
    //+1, -1,
    +1, +0,
    //+1, +1,
    +0, +1,
    //-1, +1,
    -1, +0,
    //-1, -1,
};

static struct {
    signed char state;
    signed char gradient;
} grid[GRIDH][GRIDW];
#define GRID_EMPTY  0
#define GRID_TARGET 1
#define GRID_GUN    2
#define GRID_FENCE  3

static int
compute_gradient(void)
{
    static int queue[GRIDW*GRIDH];
    int head = 1;
    int tail = 0;
    queue[0] = TARGET_Y*GRIDW + TARGET_X;
    for (int y = 0; y < GRIDH; y++) {
        for (int x = 0; x < GRIDW; x++) {
            grid[y][x].gradient = -1;
        }
    }
    grid[TARGET_Y][TARGET_X].gradient = 9;

    while (head > tail) {
        int n = queue[tail++];
        int x = n%GRIDW;
        int y = n/GRIDW;
        for (int i = 0; i < countof(dirs)/2; i++) {
            int tx = x + dirs[i*2 + 0];
            int ty = y + dirs[i*2 + 1];
            if (tx >= 0 && tx < GRIDW && ty >= 0 && ty < GRIDH) {
                if (grid[ty][tx].state == GRID_EMPTY) {
                    if (grid[ty][tx].gradient == -1) {
                        int g = (i + countof(dirs)/4) % (countof(dirs)/2);
                        grid[ty][tx].gradient = g;
                        queue[head++] = ty*GRIDW + tx;
                    }
                }
            }
        }
    }

    grid[TARGET_Y][TARGET_X].gradient = -1;
    return grid[START_Y][START_X].gradient != -1;
}

static struct {
    int x, y;
    int cooldown;
} guns[GRIDW*GRIDH/2];
static int nguns;
static int guns_available = 2;

static void
gun_place(int x, int y)
{
    if (!guns_available) return;
    if (grid[y][x].state == GRID_EMPTY) {
        grid[y][x].state = GRID_GUN;
        if (compute_gradient()) {
            guns[nguns].x = x;
            guns[nguns].y = y;
            nguns++;
            snd_play(fx_drop, countof(fx_drop), 100);
            guns_available--;
        } else {
            grid[y][x].state = GRID_EMPTY;
            compute_gradient();
        }
    }
}

static struct {
    float x, y;
    short hp;
} monsters[256];
static int nmonsters;

static void
monsters_draw(void)
{
    for (int i = 0; i < nmonsters; i++) {
        int x = monsters[i].x * GRIDS;
        int y = monsters[i].y * GRIDS;
        vga_fill(x-MONSTER_W/2, y-MONSTER_H/2, MONSTER, MONSTER_W, MONSTER_H);
    }
}

static int
monsters_step(void)
{
    for (int i = 0; i < nmonsters; i++) {
        int x = (int)(monsters[i].x * GRIDS) / GRIDS;
        int y = (int)(monsters[i].y * GRIDS) / GRIDS;
        x = clamp(x, 0, GRIDW-1);
        y = clamp(y, 0, GRIDH-1);
        if (x == TARGET_X && y == TARGET_Y) {
            return 1;
        }
        int g = grid[y][x].gradient;
        if (g == -1) continue;
        int dx = dirs[g*2 + 0];
        int dy = dirs[g*2 + 1];
        float div = 50.0f;
        monsters[i].x += dx/div;
        monsters[i].y += dy/div;
    }
    return 0;
}

struct {
    float x, y;
    float dx, dy;
    int ttl;
    short target;
    short next;
} bullets[1024];
static int bullet0 = -1;
static int bulletf;

static void
bullet_step(void)
{
    int prev = -1;
    for (int i = bullet0; i != -1;) {
        if (--bullets[i].ttl >= 0) {
            bullets[i].x += bullets[i].dx;
            bullets[i].y += bullets[i].dy;
            int x = bullets[i].x*GRIDS;
            int y = bullets[i].y*GRIDS;
            vga[y][x] = vga[y-1][x] = vga[y+1][x] = 
                vga[y][x-1] = vga[y][x+1] = 90;
            prev = i;
            i = bullets[i].next;
        } else {
            int dead = i;
            if (prev != -1) {
                i = bullets[prev].next = bullets[i].next;
            } else {
                i = bullet0 = bullets[i].next;
            }
            bullets[dead].next = bulletf;
            bulletf = dead;

            int t = bullets[dead].target;
            if (!--monsters[t].hp) {
                size_t s = (--nmonsters - t) * sizeof(monsters[0]);
                memmove(monsters+t, monsters+t+1, s);
                guns_available++;
                snd_play(fx_die, countof(fx_die), 25);
            }
            snd_play(fx_fire, countof(fx_fire), 10);
        }
    }
}

static void
gun_step(void)
{
    for (int i = 0; i < nguns; i++) {
        if (bulletf == -1) {
            abort();
            return;
        }
        if (guns[i].cooldown > 0) {
            guns[i].cooldown--;
            continue;
        }
        for (int j = 0; j < nmonsters; j++) {
            float dx = guns[i].x + 0.5f - monsters[j].x;
            float dy = guns[i].y + 0.5f - monsters[j].y;
            float r = dx*dx + dy*dy;
            if (r <= GUNRANGE*GUNRANGE) {
                int b = bulletf;
                bulletf = bullets[b].next;
                bullets[b].x = guns[i].x + 0.5f;
                bullets[b].y = guns[i].y + 0.5f;
                bullets[b].ttl = 8;
                bullets[b].dx = dx / -bullets[b].ttl;
                bullets[b].dy = dy / -bullets[b].ttl;
                bullets[b].target = j;
                bullets[b].next = bullet0;
                bullet0 = b;
                guns[i].cooldown = 10 + rand()%4;
                //snd_play(fx_fire, countof(fx_fire), 10);
                break;
            }
        }
    }
}

int
main(void)
{
    int rate = 100;
    int init_hp = 10;
    int cursor_x = 5;
    int cursor_y = 2;
    grid[TARGET_Y][TARGET_X].state = GRID_TARGET;
    for (int x = 0; x < GRIDW; x++) {
        if (x != START_X) grid[0][x].state = 1;
    }
    compute_gradient();
    srand(time(0));
    for (int i = 0; i < countof(bullets) - 1; i++) {
        bullets[i].next = i + 1;
    }
    bullets[countof(bullets)-1].next = -1;

    vga_init();

    vga_fill(0, 0, GOML, GOML_W, GOML_H);
    vga_flush();
    for (;;) {
        snd_play(fx_intro, countof(fx_intro), 1);
        snd_tick();
        if (kbhit()) {
            getch();
            break;
        }
        usleep(1000);
    }
    snd_play(fx_intro, 0, 2);
    snd_tick();

    int done = 0;
    for (unsigned tick = 0; !done; tick++) {
        double start = now();
        snd_tick();
        if (rand() % 80 == 0) rate = clamp(rate - 1, 10, rate);
        if (rand() % 200 == 0) rate++;
        if (rand() % 10 == 0) init_hp++;

        if (tick % rate == 0 && nmonsters < countof(monsters)) {
            monsters[nmonsters].x = START_X + 0.5f;
            monsters[nmonsters].y = START_Y + 0.5f;
            monsters[nmonsters].hp = init_hp;
            nmonsters++;
        }

        static const int cursor_colors[] = {12, 40, 14};
        memcpy(vga, BACK, sizeof(vga));
        for (int y = 0; y < GRIDH; y++) {
            for (int x = 0; x < GRIDW; x++) {
                if (grid[y][x].state == GRID_GUN) {
                    vga_fill(x*GRIDS, y*GRIDS, GUN, GUN_W, GUN_H);
                }
            }
        }
        box(cursor_x, cursor_y, cursor_colors[tick%3]);
        monsters_draw();
        gun_step();
        bullet_step();

        if (monsters_step()) {
            vga_fill(0, 0, GAMEOVER, GAMEOVER_W, GAMEOVER_H);
            vga_flush();
            snd_play(fx_outro, countof(fx_outro), 1000);
            while (!kbhit()) {
                snd_tick();
                usleep(1000);
            }
            break;
        }

        vga_flush();

        if (kbhit()) {
            switch (xgetch()) {
            case KEY_N:  cursor_y--; break;
            case KEY_S:  cursor_y++; break;
            case KEY_W:  cursor_x--; break;
            case KEY_E:  cursor_x++; break;
            case KEY_NW: cursor_y--; cursor_x--; break;
            case KEY_NE: cursor_y--; cursor_x++; break;
            case KEY_SW: cursor_y++; cursor_x--; break;
            case KEY_SE: cursor_y++; cursor_x++; break;
            case ' ': gun_place(cursor_x, cursor_y); break;
            case 'q': done = 1;    break;
            }
            cursor_x = clamp(cursor_x, 0, GRIDW - 1);
            cursor_y = clamp(cursor_y, 0, GRIDH - 1);
        }

        double rem = 1.0/FPS - (now() - start);
        if (rem > 0.0) usleep(rem * 1e6);
    }

    tone_off();
    vga_free();
}
