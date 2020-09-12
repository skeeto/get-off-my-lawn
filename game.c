#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <pc.h>
#include <conio.h>
#include <dpmi.h>
#include <unistd.h>
#include <sys/nearptr.h>

#define W 320
#define H 200
#define X 0x0f

static unsigned char grid[2][H][W];

static void
step(int i)
{
    static const int dir[] = {
        +1, +1, +1, +0, +1, -1, +0, +1, +0, -1, -1, +1, -1, +0, -1, -1
    };
    int o = !i;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int c = 0;
            for (int d = 0; d < 8; d++) {
                int nx = (W + x + dir[d*2 + 0]) % W;
                int ny = (H + y + dir[d*2 + 1]) % H;
                c += !!grid[i][ny][nx];
            }
            switch (c) {
            case 2:  grid[o][y][x] = grid[i][y][x]; break;
            case 3:  grid[o][y][x] = X; break;
            default: grid[o][y][x] = 0; break;
            }
        }
    }
}

static void 
draw(int i)
{
    if (__djgpp_nearptr_enable() == 0) abort();
    unsigned char *p = (unsigned char *)__djgpp_conventional_base + 0xa0000;
    memcpy(p, grid[i], sizeof(grid[i]));
    __djgpp_nearptr_disable();
}

enum {PAD_N=300, PAD_S, PAD_E, PAD_W, PAD_NW, PAD_NE, PAD_SE, PAD_SW};

int
xgetch(void)
{
    int result = getch();
    if (result != 0xE0 && result != 0x00) {
        return result;
    } else {
        result = getch();
        switch (result) {
            case 72: return PAD_N;
            case 80: return PAD_S;
            case 75: return PAD_W;
            case 77: return PAD_E;
            case 71: return PAD_NW;
            case 73: return PAD_NE;
            case 79: return PAD_SW;
            case 81: return PAD_SE;
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
    unsigned frequency;
    unsigned length;
};

struct sample {
    int priority;
    int length;
    struct note notes[];
};

struct sample fx_boss = {
    .priority = 500,
    .length = 13,
    {
        {110, 15},
        {0,   2},
        {110, 15},
        {0,   2},
        {110, 5},
        {175, 30},
        {0,   2},
        {175, 15},
        {0,   2},
        {175, 15},
        {0,   2},
        {175, 5},
        {110, 30},
    }
};

int
main(void)
{
#if 0
    {
        if (__djgpp_nearptr_enable() == 0) abort();
        unsigned char *p = (unsigned char *)__djgpp_conventional_base;
        p[0x00c4] = 0x06;
        p[0x00c5] = 0x07;
        p[0x00c6] = 0xff;
        __djgpp_nearptr_disable();
        __dpmi_regs regs = {.x = {.ax = 0x8300}};
        __dpmi_int(0x1a, &regs);
        for (;;) sleep(1);
    }
#endif

    for (int i = 0; i < fx_boss.length; i++) {
        if (fx_boss.notes[i].frequency) {
            tone(fx_boss.notes[i].frequency);
            tone_on();
        } else {
            tone_off();
        }
        usleep(fx_boss.notes[i].length*20000);
    }
    tone_off();
    return 0;

    __dpmi_regs regs = {.x = {.ax = 0x13}};
    __dpmi_int(0x10, &regs);

    int x = W/2;
    int y = H/2;
    for (;;) {
        grid[0][y][x] = X;
        draw(0);
        switch (xgetch()) {
        case PAD_N:  y--;      break;
        case PAD_S:  y++;      break;
        case PAD_W:  x--;      break;
        case PAD_E:  x++;      break;
        case PAD_NW: y--; x--; break;
        case PAD_NE: y--; x++; break;
        case PAD_SW: y++; x--; break;
        case PAD_SE: y++; x++; break;
        case 'q': return 0;
        }
    }
    return 0;

    srand(time(0));
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            grid[0][y][x] = rand()%2 * X;
        }
    }

    for (int i = 0; ; i = !i) {
        draw(i);
        step(i);
    }
}
