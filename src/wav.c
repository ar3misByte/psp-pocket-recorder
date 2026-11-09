/* wav.c - minimal 16-bit PCM WAV reading/writing */
#include "wav.h"
#include <string.h>

static unsigned le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static unsigned le16(const unsigned char *p) { return p[0] | (p[1] << 8); }

int wav_parse(FILE *f, WavInfo *wi)
{
    unsigned char h[12];
    memset(wi, 0, sizeof *wi);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return -1;
    int have_fmt = 0, bits = 0, fmt = 0;
    long pos = 12;
    while (pos + 8 <= size) {
        unsigned char c[8];
        fseek(f, pos, SEEK_SET);
        if (fread(c, 1, 8, f) != 8) break;
        unsigned len = le32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char b[16];
            if (len < 16 || fread(b, 1, 16, f) != 16) return -2;
            fmt = le16(b);
            wi->channels = le16(b + 2);
            wi->rate = (int)le32(b + 4);
            bits = le16(b + 14);
            have_fmt = 1;
        } else if (!memcmp(c, "data", 4)) {
            wi->data_start = pos + 8;
            /* a recording that was cut off (battery died) has a size of 0 or
               a wrong one: then just use the rest of the file */
            long avail = size - wi->data_start;
            wi->data_bytes = (len == 0 || (long)len > avail) ? avail : (long)len;
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!have_fmt || !wi->data_start) return -3;
    if ((fmt != 1 && fmt != 0xFFFE) || bits != 16 || wi->channels < 1 || wi->channels > 2 || wi->rate <= 0) return -4;
    wi->data_bytes -= wi->data_bytes % (2 * wi->channels);
    return 0;
}

static void put32(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

void wav_make_header(unsigned char *h, int rate, int channels, long data_bytes)
{
    memcpy(h, "RIFF", 4);
    put32(h + 4, (unsigned)(36 + data_bytes));
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);
    put16(h + 22, channels);
    put32(h + 24, rate);
    put32(h + 28, rate * channels * 2);
    put16(h + 32, channels * 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, (unsigned)data_bytes);
}

void wav_write_header(FILE *f, int rate, int channels, long data_bytes)
{
    unsigned char h[44];
    wav_make_header(h, rate, channels, data_bytes);
    fwrite(h, 1, 44, f);
}
