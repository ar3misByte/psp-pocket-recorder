/* mp3info.c - MP3 header/ID3 parsing */
#include "mp3info.h"
#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int br_v1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const int br_v2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
static const int sr_v1[3] = {44100, 48000, 32000};

int mp3_frame_header(const unsigned char *h, int *rate, int *spf, int *channels, int *kbps)
{
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    int ver = (h[1] >> 3) & 3;   /* 0 = 2.5, 2 = 2, 3 = 1 */
    int layer = (h[1] >> 1) & 3; /* 1 = layer III */
    int bri = h[2] >> 4, sri = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) return 0;
    int sr = sr_v1[sri];
    if (ver == 2) sr /= 2;
    else if (ver == 0) sr /= 4;
    int br = (ver == 3) ? br_v1[bri] : br_v2[bri];
    int len = (ver == 3) ? (144000 * br / sr + pad) : (72000 * br / sr + pad);
    if (rate) *rate = sr;
    if (spf) *spf = (ver == 3) ? 1152 : 576;
    if (channels) *channels = ((h[3] >> 6) == 3) ? 1 : 2;
    if (kbps) *kbps = br;
    return len;
}

int mp3_find_frame(const unsigned char *buf, int len)
{
    for (int i = 0; i + 4 <= len; i++) {
        if (buf[i] != 0xFF) continue;
        int rate, n = mp3_frame_header(buf + i, &rate, 0, 0, 0);
        if (n < 24) continue;
        if (i + n + 4 > len) {
            /* can't verify the next header; accept only if near end of buffer */
            if (i + n >= len) return i;
            continue;
        }
        int rate2;
        if (mp3_frame_header(buf + i + n, &rate2, 0, 0, 0) && rate2 == rate) return i;
    }
    return -1;
}

static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static unsigned syncsafe(const unsigned char *p) { return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F); }

/* convert an ID3 text frame payload to printable ASCII */
static void id3_text(const unsigned char *p, int n, char *out, int outn)
{
    int o = 0;
    if (n <= 0) { out[0] = 0; return; }
    int enc = p[0];
    p++; n--;
    if (enc == 1 || enc == 2) {
        int be = (enc == 2);
        if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { be = 1; p += 2; n -= 2; }
        else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { be = 0; p += 2; n -= 2; }
        for (int i = 0; i + 1 < n && o < outn - 1; i += 2) {
            unsigned c = be ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
            if (c == 0) break;
            out[o++] = (c >= 32 && c < 127) ? (char)c : '?';
        }
    } else {
        for (int i = 0; i < n && o < outn - 1; i++) {
            unsigned char c = p[i];
            if (c == 0) break;
            if (enc == 3 && c >= 0x80) {
                /* skip UTF-8 continuation bytes, emit one '?' per code point */
                if ((c & 0xC0) == 0x80) continue;
                out[o++] = '?';
                continue;
            }
            out[o++] = (c >= 32 && c < 127) ? (char)c : '?';
        }
    }
    /* trim */
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

/* a title made mostly of '?' (non-Latin script) is not useful with our font */
static int mostly_unknown(const char *s)
{
    int q = 0, n = 0;
    for (; *s; s++, n++)
        if (*s == '?') q++;
    return n == 0 || q * 2 > n;
}

static void parse_id3v2(const unsigned char *tag, int size, int ver, Mp3Info *mi)
{
    int pos = 0;
    while (pos + 10 <= size) {
        char id[5] = {0};
        unsigned fsz;
        int hdr;
        if (ver == 2) {
            if (pos + 6 > size) break;
            memcpy(id, tag + pos, 3);
            fsz = (tag[pos + 3] << 16) | (tag[pos + 4] << 8) | tag[pos + 5];
            hdr = 6;
        } else {
            memcpy(id, tag + pos, 4);
            fsz = (ver == 4) ? syncsafe(tag + pos + 4) : be32(tag + pos + 4);
            hdr = 10;
        }
        if (id[0] == 0 || fsz == 0 || pos + hdr + (int)fsz > size) break;
        const unsigned char *d = tag + pos + hdr;
        if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) id3_text(d, (int)fsz, mi->title, sizeof mi->title);
        else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) id3_text(d, (int)fsz, mi->artist, sizeof mi->artist);
        pos += hdr + (int)fsz;
    }
    if (mostly_unknown(mi->title)) mi->title[0] = 0;
    if (mostly_unknown(mi->artist)) mi->artist[0] = 0;
}

int mp3_probe(const char *path, Mp3Info *mi)
{
    memset(mi, 0, sizeof *mi);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    mi->file_size = ftell(f);
    mi->data_end = mi->file_size;
    fseek(f, 0, SEEK_SET);

    {
        WavInfo wi;
        if (wav_parse(f, &wi) == 0) {
            mi->sample_rate = wi.rate;
            mi->channels = wi.channels;
            mi->data_start = wi.data_start;
            mi->data_end = wi.data_start + wi.data_bytes;
            mi->duration = (float)wi.data_bytes / (2 * wi.channels) / wi.rate;
            fclose(f);
            return 0;
        }
        fseek(f, 0, SEEK_SET);
    }
    unsigned char h[10];
    long start = 0;
    if (fread(h, 1, 10, f) == 10 && h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
        int ver = h[3];
        unsigned sz = syncsafe(h + 6);
        start = 10 + (long)sz + ((h[5] & 0x10) ? 10 : 0);
        if (sz > 0 && sz < 1024 * 1024 && !(h[5] & 0x80)) {
            /* read only the first part; text frames come before pictures in nearly all files */
            int n = sz > 65536 ? 65536 : (int)sz;
            unsigned char *tag = malloc(n);
            if (tag) {
                if ((int)fread(tag, 1, n, f) == n) parse_id3v2(tag, n, ver, mi);
                free(tag);
            }
        }
    }
    /* ID3v1 at end */
    if (mi->file_size > 128) {
        unsigned char t[128];
        fseek(f, mi->file_size - 128, SEEK_SET);
        if (fread(t, 1, 128, f) == 128 && t[0] == 'T' && t[1] == 'A' && t[2] == 'G') {
            mi->data_end = mi->file_size - 128;
            if (!mi->title[0]) {
                char tmp[31];
                memcpy(tmp, t + 3, 30); tmp[30] = 0;
                int o = 0;
                for (int i = 0; i < 30 && tmp[i]; i++) mi->title[o++] = (tmp[i] >= 32 && tmp[i] < 127) ? tmp[i] : '?';
                while (o > 0 && mi->title[o - 1] == ' ') o--;
                mi->title[o] = 0;
                if (mostly_unknown(mi->title)) mi->title[0] = 0;
            }
            if (!mi->artist[0]) {
                int o = 0;
                for (int i = 0; i < 30 && t[33 + i]; i++) mi->artist[o++] = (t[33 + i] >= 32 && t[33 + i] < 127) ? t[33 + i] : '?';
                while (o > 0 && mi->artist[o - 1] == ' ') o--;
                mi->artist[o] = 0;
                if (mostly_unknown(mi->artist)) mi->artist[0] = 0;
            }
        }
    }

    /* locate first frame */
    enum { SCAN = 32768 };
    unsigned char *buf = malloc(SCAN);
    if (!buf) { fclose(f); return -2; }
    long scan_at = start;
    int idx = -1, got = 0;
    for (int tries = 0; tries < 4 && idx < 0; tries++) {
        fseek(f, scan_at, SEEK_SET);
        got = (int)fread(buf, 1, SCAN, f);
        if (got < 4) break;
        idx = mp3_find_frame(buf, got);
        if (idx < 0) scan_at += got - 4;
    }
    if (idx < 0) { free(buf); fclose(f); return -3; }
    mi->data_start = scan_at + idx;
    const unsigned char *fh = buf + idx;
    int flen = mp3_frame_header(fh, &mi->sample_rate, &mi->samples_per_frame, &mi->channels, &mi->bitrate_kbps);

    /* Xing / Info / VBRI header in the first frame */
    int ver = (fh[1] >> 3) & 3;
    int xoff = (ver == 3) ? (mi->channels == 1 ? 17 : 32) : (mi->channels == 1 ? 9 : 17);
    long frames = 0;
    if (idx + 4 + xoff + 120 <= got) {
        const unsigned char *x = fh + 4 + xoff;
        if (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4)) {
            unsigned flags = be32(x + 4);
            const unsigned char *p = x + 8;
            if (flags & 1) { frames = be32(p); p += 4; }
            if (flags & 2) p += 4;
            if (flags & 4) { memcpy(mi->toc, p, 100); mi->has_toc = 1; }
            /* the Xing frame itself is silent; skip it */
            mi->data_start += flen;
        } else if (idx + 4 + 32 + 26 <= got && !memcmp(fh + 36, "VBRI", 4)) {
            frames = be32(fh + 36 + 14);
            mi->data_start += flen;
        }
    }
    if (frames > 0) mi->duration = (float)frames * mi->samples_per_frame / mi->sample_rate;
    else if (mi->bitrate_kbps > 0) mi->duration = (float)(mi->data_end - mi->data_start) * 8.0f / (mi->bitrate_kbps * 1000.0f);
    free(buf);
    fclose(f);
    return 0;
}

long mp3_time_to_offset(const Mp3Info *mi, float sec)
{
    long len = mi->data_end - mi->data_start;
    if (sec <= 0 || mi->duration <= 0) return mi->data_start;
    float pct = sec / mi->duration * 100.0f;
    if (pct >= 100) return mi->data_end;
    float frac;
    if (mi->has_toc) {
        int i = (int)pct;
        float a = mi->toc[i], b = (i < 99) ? mi->toc[i + 1] : 256.0f;
        frac = (a + (b - a) * (pct - i)) / 256.0f;
    } else {
        frac = pct / 100.0f;
    }
    return mi->data_start + (long)(len * frac);
}
