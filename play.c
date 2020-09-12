#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static const struct {
    char name[3];
    float freq;
} NOTES[] = {
    {"c0", 16.35},
    {"d0", 18.35},
    {"e0", 20.60},
    {"f0", 21.83},
    {"g0", 24.50},
    {"a0", 27.50},
    {"b0", 30.87},
    {"c1", 32.70},
    {"d1", 36.71},
    {"e1", 41.20},
    {"f1", 43.65},
    {"g1", 49.00},
    {"a1", 55.00},
    {"b1", 61.74},
    {"c2", 65.41},
    {"d2", 73.42},
    {"e2", 82.41},
    {"f2", 87.31},
    {"g2", 98.00},
    {"a2", 110.00},
    {"b2", 123.47},
    {"c3", 130.81},
    {"d3", 146.83},
    {"e3", 164.81},
    {"f3", 174.61},
    {"g3", 196.00},
    {"a3", 220.00},
    {"b3", 246.94},
    {"c4", 261.63},
    {"d4", 293.66},
    {"e4", 329.63},
    {"f4", 349.23},
    {"g4", 392.00},
    {"a4", 440.00},
    {"b4", 493.88},
    {"c5", 523.25},
    {"d5", 587.33},
    {"e5", 659.25},
    {"f5", 698.46},
    {"g5", 783.99},
    {"a5", 880.00},
    {"b5", 987.77},
    {"c6", 1046.50},
    {"d6", 1174.66},
    {"e6", 1318.51},
    {"f6", 1396.91},
    {"g6", 1567.98},
    {"a6", 1760.00},
    {"b6", 1975.53},
    {"c7", 2093.00},
    {"d7", 2349.32},
    {"e7", 2637.02},
    {"f7", 2793.83},
    {"g7", 3135.96},
    {"a7", 3520.00},
    {"b7", 3951.07},
    {"c8", 4186.01},
    {"d8", 4698.63},
    {"e8", 5274.04},
    {"f8", 5587.65},
    {"g8", 6271.93},
    {"a8", 7040.00},
    {"b8", 7902.13},
};

int
main(void)
{
    char line[16];
    while (fgets(line, sizeof(line), stdin)) {
        fputs(line, stdout);
        tone_off();
        char *name = strtok(line, " \t");
        int time = atoi(strtok(0, "\n"));
        for (size_t i = 0; i < sizeof(NOTES)/sizeof(*NOTES); i++) {
            if (!strcmp(NOTES[i].name, name)) {
                tone(NOTES[i].freq + 0.5);
                tone_on();
                break;
            }
        }
        usleep(time * 1000);
    }
}
