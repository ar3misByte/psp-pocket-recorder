/*
 * Pocket Recorder - a retro dictaphone for the PSP
 *
 * Records from the PSP's microphone to WAV files in ms0:/MUSIC/Recordings,
 * so they also show up as a tape in Cassette Deck.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psprtc.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "gfx.h"
#include "ui.h"
#include "player.h"
#include "mp3info.h"
#include "wav.h"

#ifndef PREVIEW
PSP_MODULE_INFO("Pocket Recorder", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);
#endif

#define REC_DIR "ms0:/MUSIC/Recordings"
#define IN_BLOCK 512                 /* microphone samples per read */
#define RB_SIZE (1 << 18)            /* recording ring: 262144 samples (~6 s at 44 kHz) */
#define RB_MASK (RB_SIZE - 1)
#define WRITE_CHUNK 16384            /* samples per Memory Stick write */
#define WAVE_N 480
#define MAX_RECS 300
#define TWO_PI 6.2831853f

#define COL_TEXT RGB(232, 226, 210)
#define COL_DIM RGB(140, 136, 126)
#define COL_AMBER RGB(255, 170, 40)
#define COL_RED RGB(235, 50, 40)
#define COL_GREEN RGB(90, 220, 110)

/* ------------------------------------------------------------------ */

static volatile int running = 1;   /* threads keep going while this is set */
static volatile int exit_req = 0;  /* HOME -> Exit was chosen */

static const int rates[3] = {11025, 22050, 44100};
static const char *rate_names[3] = {"11 kHz", "22 kHz", "44 kHz"};
static const char *rate_desc[3] = {"Voice", "Standard", "High"};
static int rate_i = 1;

static const float gains[8] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f};
static int gain_i = 3;
static int vox = 0;

/* microphone + recording state (shared with the input/writer threads) */
static volatile int input_on = 1;     /* read the mic (for the meter) */
static volatile int rec_active = 0;   /* a file is open */
static volatile int rec_paused = 0;
static volatile int vox_open = 1;
static volatile float mic_level = 0;  /* RMS 0..1 */
static volatile float mic_peak = 0;
static volatile int mic_error = 0;
static volatile int write_error = 0;
static volatile unsigned int rb_w = 0, rb_r = 0;
static volatile long rec_samples = 0;     /* samples recorded into the file */
static short rb[RB_SIZE] __attribute__((aligned(64)));
static short in_buf[IN_BLOCK] __attribute__((aligned(64)));
static short wr_buf[WRITE_CHUNK];

static SceUID rec_fd = -1;
static long rec_bytes = 0;
static int rec_rate = 22050;
static char rec_path[256];
static char rec_name[64];

/* waveform history (one column per 512 samples) */
static volatile float wave[WAVE_N];
static volatile unsigned int wave_n = 0;

static SceUID in_th = -1, wr_th = -1, wr_sema = -1;

/* recordings list */
typedef struct {
    char file[64];
    char label[40];
    float dur;
    long bytes;
} Rec;
static Rec recs[MAX_RECS];
static int nrecs = 0, list_sel = 0;
static float list_scroll = 0;
static int confirm_delete = 0;

enum { SCR_REC, SCR_LIST, SCR_PLAY };
static int screen = SCR_REC;

/* playback */
static int play_i = -1;
static int play_paused = 0;
static int play_vol = 16;

static char toast_msg[96];
static float toast_t = 0;
static float angle_l = 0, angle_r = 0, reel_speed = 0;
static long free_bytes = -1;

static void toast(const char *m)
{
    strncpy(toast_msg, m, sizeof toast_msg - 1);
    toast_msg[sizeof toast_msg - 1] = 0;
    toast_t = 2.2f;
}

/* ------------------------------------------------------------------ */
/* threads                                                             */

static void push_wave(float v)
{
    wave[wave_n % WAVE_N] = v;
    wave_n++;
}

static int input_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    float quiet_t = 0;
    while (running) {
        if (!input_on) {
            mic_level = 0;
            sceKernelDelayThread(10000);
            continue;
        }
        int rate = rates[rate_i];
        if (rec_active) rate = rec_rate;
        int r = sceAudioInputBlocking(IN_BLOCK, rate, in_buf);
        if (r < 0) {
            mic_error = r;
            mic_level = 0;
            sceKernelDelayThread(100000);
            continue;
        }
        mic_error = 0;
        /* the mic writes with DMA: read through the uncached address */
#ifdef PREVIEW
        const short *src = in_buf; /* PC test build */
#else
        const short *src = (const short *)((unsigned int)in_buf | 0x40000000);
#endif
        float g = gains[gain_i];
        float sum = 0, peak = 0;
        short tmp[IN_BLOCK];
        for (int i = 0; i < IN_BLOCK; i++) {
            float v = src[i] * g;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            tmp[i] = (short)v;
            float a = fabsf(v);
            sum += v * v;
            if (a > peak) peak = a;
        }
        float rms = sqrtf(sum / IN_BLOCK) / 32768.0f;
        mic_level = rms;
        mic_peak = peak / 32768.0f;

        /* voice activation: open on sound, close after 1.5 s of quiet */
        float block_sec = (float)IN_BLOCK / rate;
        if (rms > 0.02f) {
            vox_open = 1;
            quiet_t = 0;
        } else {
            quiet_t += block_sec;
            if (quiet_t > 1.5f) vox_open = 0;
        }

        int keep = rec_active && !rec_paused && (!vox || vox_open);
        push_wave((keep || !rec_active) ? peak / 32768.0f : 0); /* flat line while paused */
        if (keep) {
            if ((int)(rb_w - rb_r) + IN_BLOCK <= RB_SIZE) {
                for (int i = 0; i < IN_BLOCK; i++) rb[(rb_w + i) & RB_MASK] = tmp[i];
                rb_w += IN_BLOCK;
                rec_samples += IN_BLOCK;
            }
            if (rb_w - rb_r >= WRITE_CHUNK) sceKernelSignalSema(wr_sema, 1);
        }
    }
    return 0;
}

static void write_header(void)
{
    unsigned char h[44];
    wav_make_header(h, rec_rate, 1, rec_bytes);
    sceIoLseek32(rec_fd, 0, PSP_SEEK_SET);
    sceIoWrite(rec_fd, h, 44);
    sceIoLseek32(rec_fd, 0, PSP_SEEK_END);
}

/* write whatever is in the ring; all = also the last partial chunk */
static void drain(int all)
{
    while (rec_fd >= 0) {
        unsigned int avail = rb_w - rb_r;
        if (avail == 0 || (!all && avail < WRITE_CHUNK)) break;
        int n = avail > WRITE_CHUNK ? WRITE_CHUNK : (int)avail;
        for (int i = 0; i < n; i++) wr_buf[i] = rb[(rb_r + i) & RB_MASK];
        int w = sceIoWrite(rec_fd, wr_buf, n * 2);
        if (w != n * 2) {
            write_error = 1;
            rb_r = rb_w;
            break;
        }
        rb_r += n;
        rec_bytes += n * 2;
    }
}

static volatile int wr_flush_req = 0, wr_flush_done = 0;

static int writer_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int since_header = 0;
    while (running) {
        SceUInt timeout = 200000;
        sceKernelWaitSema(wr_sema, 1, &timeout);
        if (rec_fd >= 0) {
            long before = rec_bytes;
            drain(0);
            since_header += (int)(rec_bytes - before);
            /* keep the header up to date every ~10 s, so a recording survives
               a flat battery */
            if (since_header > rec_rate * 2 * 10) {
                write_header();
                since_header = 0;
            }
        }
        if (wr_flush_req) {
            drain(1);
            wr_flush_req = 0;
            wr_flush_done = 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* recordings                                                          */

static void ensure_dirs(void)
{
    sceIoMkdir("ms0:/MUSIC", 0777);
    sceIoMkdir(REC_DIR, 0777);
}

static void update_free_space(void)
{
    unsigned int buf[5];
    unsigned int *pbuf = buf;
    memset(buf, 0, sizeof buf);
    if (sceIoDevctl("ms0:", 0x02425818, &pbuf, sizeof(pbuf), 0, 0) >= 0) {
        double fb = (double)buf[1] * buf[3] * buf[4]; /* free clusters * sectors * bytes */
        free_bytes = fb > 2e9 ? 2000000000L : (long)fb;
    }
}

static const char *month_name(int m)
{
    static const char *n[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return (m >= 1 && m <= 12) ? n[m - 1] : "???";
}

/* "REC_20261109_181205.wav" -> "09 Nov 18:12:05" */
static void make_label(const char *file, char *out, int n)
{
    int y, mo, d, h, mi, s;
    if (sscanf(file, "REC_%4d%2d%2d_%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) == 6)
        snprintf(out, n, "%02d %s %02d:%02d:%02d", d, month_name(mo), h, mi, s);
    else {
        strncpy(out, file, n - 1);
        out[n - 1] = 0;
        char *dot = strrchr(out, '.');
        if (dot) *dot = 0;
    }
}

static int rec_cmp(const void *a, const void *b) { return strcmp(((const Rec *)b)->file, ((const Rec *)a)->file); }

static void scan_recordings(void)
{
    nrecs = 0;
    SceUID d = sceIoDopen(REC_DIR);
    if (d >= 0) {
        SceIoDirent e;
        memset(&e, 0, sizeof e);
        while (sceIoDread(d, &e) > 0 && nrecs < MAX_RECS) {
            size_t l = strlen(e.d_name);
            if (e.d_name[0] != '.' && l > 4 && strcasecmp(e.d_name + l - 4, ".wav") == 0 && strlen(e.d_name) < 64) {
                Rec *r = &recs[nrecs++];
                memset(r, 0, sizeof *r);
                strcpy(r->file, e.d_name);
                make_label(r->file, r->label, sizeof r->label);
                r->bytes = (long)e.d_stat.st_size;
            }
            memset(&e, 0, sizeof e);
        }
        sceIoDclose(d);
    }
    qsort(recs, nrecs, sizeof(Rec), rec_cmp); /* newest first */
    for (int i = 0; i < nrecs; i++) {
        char p[256];
        snprintf(p, sizeof p, "%s/%s", REC_DIR, recs[i].file);
        Mp3Info mi;
        if (mp3_probe(p, &mi) == 0) recs[i].dur = mi.duration;
    }
    if (list_sel >= nrecs) list_sel = nrecs > 0 ? nrecs - 1 : 0;
}

static void start_recording(void)
{
    ensure_dirs();
    ScePspDateTime t;
    memset(&t, 0, sizeof t);
    sceRtcGetCurrentClockLocalTime(&t);
    snprintf(rec_name, sizeof rec_name, "REC_%04d%02d%02d_%02d%02d%02d.wav", t.year, t.month, t.day, t.hour, t.minute,
             t.second);
    snprintf(rec_path, sizeof rec_path, "%s/%s", REC_DIR, rec_name);
    rec_fd = sceIoOpen(rec_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (rec_fd < 0) {
        toast("Can't create file on the Memory Stick");
        return;
    }
    rec_rate = rates[rate_i];
    rec_bytes = 0;
    write_header();
    rb_r = rb_w;
    rec_samples = 0;
    write_error = 0;
    rec_paused = 0;
    vox_open = 1;
    rec_active = 1;
    sfx_play(SFX_CLUNK);
}

static void stop_recording(int quiet)
{
    if (!rec_active) return;
    rec_active = 0; /* the input thread stops adding samples */
    sceKernelDelayThread(30000);
    wr_flush_done = 0;
    wr_flush_req = 1;
    sceKernelSignalSema(wr_sema, 1);
    for (int i = 0; i < 300 && !wr_flush_done; i++) sceKernelDelayThread(10000);
    if (!wr_flush_done) drain(1);
    write_header();
    sceIoClose(rec_fd);
    rec_fd = -1;
    float secs = (float)rec_bytes / 2 / rec_rate;
    if (rec_bytes == 0) {
        sceIoRemove(rec_path);
        if (!quiet) toast("Nothing recorded");
    } else if (!quiet) {
        char m[96];
        snprintf(m, sizeof m, "Saved %d:%02d  -  also on the Recordings tape", (int)secs / 60, (int)secs % 60);
        toast(m);
    }
    sfx_play(SFX_CLUNK);
    update_free_space();
}

/* ------------------------------------------------------------------ */
/* display                                                             */

static uint32_t drawbuf[BUF_W * SCR_H] __attribute__((aligned(64)));
static int backbuf = 1;

static void present(int vblanks)
{
    uint32_t *vram = (uint32_t *)(0x44000000 + backbuf * BUF_W * SCR_H * 4);
    memcpy(vram, drawbuf, sizeof drawbuf);
    for (int i = 0; i < vblanks; i++) sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(vram, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    backbuf ^= 1;
}

static uint32_t bg[SCR_W * SCR_H];

static void make_background(void)
{
    /* graphite body with fine brushed texture */
    unsigned int seed = 7;
    for (int y = 0; y < SCR_H; y++) {
        int line = (int)((seed = seed * 1103515245u + 12345u) >> 16) % 5 - 2;
        for (int x = 0; x < SCR_W; x++) {
            seed = seed * 1103515245u + 12345u;
            int v = 48 - y * 20 / SCR_H + line + (int)((seed >> 16) % 3) - 1;
            bg[y * SCR_W + x] = RGB(v, v + 1, v + 4);
        }
    }
}

static void blit_bg(void)
{
    for (int y = 0; y < SCR_H; y++) memcpy(drawbuf + y * BUF_W, bg + y * SCR_W, SCR_W * 4);
}

static void fmt_time(char *b, int n, float sec, int big)
{
    if (sec < 0) sec = 0;
    int t = (int)sec;
    if (t >= 3600) snprintf(b, n, "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    else if (big) snprintf(b, n, "%02d:%02d", t / 60, t % 60);
    else snprintf(b, n, "%d:%02d", t / 60, t % 60);
}

static void hint(int *x, int y, int icon, const char *label)
{
    int w = ui_btn_icon(*x, y, icon);
    gfx_text(*x + w + 3, y + 2, label, 1, COL_DIM);
    *x += w + 3 + gfx_text_w(label, 1) + 9;
}

static void status_bar(const char *title, int hh, int mm, int bat)
{
    char b[32];
    gfx_rect(0, 0, SCR_W, 18, RGB(14, 14, 16));
    gfx_rect(0, 18, SCR_W, 1, RGB(60, 60, 64));
    gfx_text(8, 5, title, 1, COL_AMBER);
    int x = SCR_W - 8;
    if (hh >= 0) {
        snprintf(b, sizeof b, "%02d:%02d", hh, mm);
        x -= gfx_text_w(b, 1);
        gfx_text(x, 5, b, 1, COL_TEXT);
        x -= 12;
    }
    if (bat >= 0) {
        x -= 22;
        gfx_rect_outline(x, 4, 20, 10, COL_DIM);
        gfx_rect(x + 20, 7, 2, 4, COL_DIM);
        gfx_rect(x + 2, 6, 16 * bat / 100, 6, bat <= 15 ? COL_RED : bat <= 35 ? COL_AMBER : COL_GREEN);
    }
}

static void draw_toast(void)
{
    if (toast_t <= 0) return;
    float a = toast_t > 0.4f ? 1 : toast_t / 0.4f;
    int w = gfx_text_w(toast_msg, 1) + 24;
    int x = (SCR_W - w) / 2, y = 124;
    gfx_rect_alpha(x, y, w, 22, RGB(10, 10, 12), (int)(220 * a));
    gfx_rect_alpha(x, y, w, 1, COL_AMBER, (int)(255 * a));
    gfx_rect_alpha(x, y + 21, w, 1, COL_AMBER, (int)(255 * a));
    gfx_text(x + 12, y + 7, toast_msg, 1, gfx_mix(RGB(10, 10, 12), COL_TEXT, (int)(255 * a)));
}

/* horizontal LED level meter */
static void led_meter(int x, int y, int w, float level, float peak)
{
    int segs = 24, sw = (w - (segs - 1) * 2) / segs;
    float db = level > 1e-5f ? 20 * log10f(level) : -100;
    float t = (db + 50) / 50; /* -50 dB .. 0 dB */
    float pdb = peak > 1e-5f ? 20 * log10f(peak) : -100;
    float pt = (pdb + 50) / 50;
    for (int i = 0; i < segs; i++) {
        float st = (float)(i + 1) / segs;
        uint32_t c = i >= segs - 3 ? COL_RED : i >= segs - 8 ? COL_AMBER : COL_GREEN;
        int on = st <= t;
        int pk = (int)(pt * segs) == i + 1;
        gfx_rect(x + i * (sw + 2), y, sw, 10, on || pk ? c : gfx_shade(c, 18));
    }
}

/* scrolling waveform "oscilloscope" */
static void draw_wave(int x, int y, int w, int h, uint32_t col)
{
    gfx_rrect(x - 3, y - 3, w + 6, h + 6, 5, RGB(10, 12, 10));
    gfx_rect(x, y, w, h, RGB(12, 26, 18));
    for (int gx = x; gx < x + w; gx += 24) gfx_rect(gx, y, 1, h, RGB(20, 40, 28));
    for (int gy = y; gy < y + h; gy += 12) gfx_rect(x, gy, w, 1, RGB(20, 40, 28));
    int mid = y + h / 2;
    gfx_rect(x, mid, w, 1, RGB(30, 70, 45));
    unsigned int n = wave_n;
    int cols = w / 2; /* one thin bar every 2 pixels */
    for (int i = 0; i < cols; i++) {
        long k = (long)n - cols + i;
        if (k < 0) continue;
        float v = wave[k % WAVE_N];
        /* a log-ish curve so quiet speech is still visible */
        float d = v > 0.0003f ? (20 * log10f(v) + 54) / 54 : 0;
        if (d < 0) d = 0;
        if (d > 1) d = 1;
        int a = (int)(d * (h / 2 - 2));
        if (a > 0) gfx_rect(x + i * 2, mid - a, 1, a * 2 + 1, col);
    }
    /* scanline sheen */
    gfx_rect_alpha(x, y, w, h / 3, RGB(255, 255, 255), 8);
}

static void draw_rec_screen(int hh, int mm, int bat, float now)
{
    char b[64];
    blit_bg();
    status_bar("POCKET RECORDER", hh, mm, bat);

    /* cassette window */
    int cx = 10, cy = 26, cw = 236, chh = 152;
    gfx_rrect(cx, cy, cw, chh, 8, RGB(10, 10, 12));
    gfx_rrect(cx + 2, cy + 2, cw - 4, chh - 4, 7, RGB(24, 24, 28));
    float prog = rec_active ? fminf(1.0f, (float)rec_samples / rec_rate / 1800.0f) : 0;
    char lbl[40];
    if (rec_active) make_label(rec_name, lbl, sizeof lbl);
    const char *label = rec_active ? lbl : "New recording";
    ui_draw_cassette(cx + 10, cy + 10, 2.16f, 2.08f, 4, label, 'A', prog, angle_l, angle_r, 1);
    gfx_rect_alpha(cx + 4, cy + 4, cw - 8, 20, RGB(255, 255, 255), 10);

    /* right panel */
    int px = 258;
    int recording = rec_active && !rec_paused;
    int blink = ((int)(now * 2)) % 2 == 0;
    int lamp = rec_active && (recording ? (!vox || vox_open) : blink);
    gfx_circle(px + 16, 44, 13, RGB(30, 10, 10));
    gfx_circle(px + 16, 44, 11, lamp ? COL_RED : RGB(80, 20, 18));
    if (lamp) gfx_circle_alpha(px + 16, 44, 20, COL_RED, 50);
    gfx_text(px + 36, 37, rec_active ? (rec_paused ? "PAUSED" : (vox && !vox_open ? "WAITING" : "REC")) : "READY", 2,
             rec_active ? (rec_paused ? COL_AMBER : COL_RED) : COL_TEXT);

    float secs = rec_active ? (float)rec_samples / rec_rate : 0;
    fmt_time(b, sizeof b, secs, 1);
    gfx_rrect(px, 64, 212, 44, 5, RGB(12, 12, 14));
    gfx_text(px + 12, 72, b, 4, rec_active ? COL_TEXT : COL_DIM);

    gfx_text(px, 116, "MIC", 1, COL_DIM);
    led_meter(px + 28, 115, 184, mic_level, mic_peak);

    snprintf(b, sizeof b, "LEVEL x%g", gains[gain_i]);
    gfx_text(px, 134, b, 1, COL_TEXT);
    snprintf(b, sizeof b, "%s %s", rate_names[rec_active ? (rec_rate == 11025 ? 0 : rec_rate == 22050 ? 1 : 2) : rate_i],
             rate_desc[rec_active ? (rec_rate == 11025 ? 0 : rec_rate == 22050 ? 1 : 2) : rate_i]);
    gfx_text(px + 96, 134, b, 1, COL_TEXT);
    gfx_circle(px + 4, 153, 3, vox ? COL_AMBER : RGB(60, 40, 20));
    gfx_text(px + 12, 150, vox ? "VOICE ACTIVATED ON" : "VOICE ACTIVATED OFF", 1, vox ? COL_TEXT : COL_DIM);
    if (free_bytes >= 0) {
        int r = rec_active ? rec_rate : rates[rate_i];
        long mins = free_bytes / (r * 2) / 60;
        snprintf(b, sizeof b, "Space for about %ld min", mins);
        gfx_text(px, 166, b, 1, COL_DIM);
    }

    if (mic_error) {
        gfx_rect_alpha(px, 112, 212, 16, RGB(60, 0, 0), 200);
        gfx_text(px + 6, 116, "No microphone signal", 1, COL_TEXT);
    }

    draw_wave(14, 190, 452, 52, recording ? COL_RED : RGB(90, 220, 140));

    int hx = 8, hy = 256;
    hint(&hx, hy, BTN_CROSS, rec_active ? (rec_paused ? "Resume" : "Pause") : "Record");
    hint(&hx, hy, BTN_CIRCLE, "Save");
    hint(&hx, hy, BTN_SQUARE, "List");
    hint(&hx, hy, BTN_UPDOWN, "Level");
    if (!rec_active) hint(&hx, hy, BTN_START, "Quality");
    hint(&hx, hy, BTN_SELECT, "Voice activation");
    draw_toast();
}

static void draw_list_screen(int hh, int mm, int bat)
{
    char b[64];
    blit_bg();
    status_bar("RECORDINGS", hh, mm, bat);
    if (nrecs == 0) {
        gfx_text(40, 110, "No recordings yet", 2, COL_TEXT);
        gfx_text(40, 136, "Press O to go back and record something.", 1, COL_DIM);
    } else {
        int rows = 10, y0 = 28, rh = 21;
        gfx_clip_push(0, y0, SCR_W, y0 + rows * rh);
        for (int i = 0; i < nrecs; i++) {
            int y = y0 + (int)((i - list_scroll) * rh);
            if (y < y0 - rh || y > y0 + rows * rh) continue;
            int sel = i == list_sel;
            gfx_rect(10, y, 460, rh - 3, sel ? RGB(60, 46, 24) : RGB(24, 24, 28));
            if (sel) gfx_rect(10, y, 3, rh - 3, COL_AMBER);
            /* tiny tape icon */
            gfx_rrect(20, y + 4, 18, 11, 2, RGB(170, 30, 36));
            gfx_rect(24, y + 7, 10, 4, RGB(30, 30, 30));
            gfx_text(48, y + 5, recs[i].label, 1, sel ? COL_TEXT : RGB(200, 196, 186));
            fmt_time(b, sizeof b, recs[i].dur, 0);
            gfx_text(330, y + 5, b, 1, COL_TEXT);
            snprintf(b, sizeof b, "%.1f MB", recs[i].bytes / 1048576.0f);
            gfx_text(400, y + 5, b, 1, COL_DIM);
        }
        gfx_clip_pop();
    }
    if (confirm_delete && nrecs > 0) {
        gfx_rect_alpha(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
        gfx_rrect(90, 90, 300, 84, 8, RGB(20, 20, 24));
        gfx_rrect(90, 90, 300, 2, 1, COL_RED);
        gfx_text(110, 104, "Delete this recording?", 1, COL_TEXT);
        gfx_text(110, 120, recs[list_sel].label, 1, COL_AMBER);
        int hx = 110;
        hint(&hx, 148, BTN_CROSS, "Delete");
        hint(&hx, 148, BTN_CIRCLE, "Keep");
    }
    int hx = 8, hy = 256;
    hint(&hx, hy, BTN_CROSS, "Play");
    hint(&hx, hy, BTN_TRIANGLE, "Delete");
    hint(&hx, hy, BTN_CIRCLE, "Back");
    draw_toast();
}

static void draw_play_screen(int hh, int mm, int bat)
{
    char b[64];
    const PlayerStatus *st = player_status();
    blit_bg();
    status_bar("PLAYBACK", hh, mm, bat);
    Rec *r = &recs[play_i];
    float dur = st->dur > 0 ? st->dur : r->dur;
    float pos = st->pos;
    int cx = 10, cy = 26, cw = 236, chh = 152;
    gfx_rrect(cx, cy, cw, chh, 8, RGB(10, 10, 12));
    gfx_rrect(cx + 2, cy + 2, cw - 4, chh - 4, 7, RGB(24, 24, 28));
    ui_draw_cassette(cx + 10, cy + 10, 2.16f, 2.08f, 4, r->label, 'A', dur > 0 ? pos / dur : 0, angle_l, angle_r, 1);

    int px = 258;
    gfx_text(px, 34, play_paused ? "PAUSED" : (st->state == PL_ENDED ? "END" : "PLAY"), 2,
             play_paused ? COL_AMBER : COL_GREEN);
    gfx_text_fit(px, 56, r->label, 1, COL_TEXT, 212);
    fmt_time(b, sizeof b, pos, 1);
    gfx_rrect(px, 70, 212, 44, 5, RGB(12, 12, 14));
    gfx_text(px + 12, 78, b, 4, COL_TEXT);
    fmt_time(b, sizeof b, dur, 0);
    char b2[32];
    snprintf(b2, sizeof b2, "of %s", b);
    gfx_text(px, 120, b2, 1, COL_DIM);
    gfx_text(px, 140, "VOL", 1, COL_DIM);
    for (int i = 0; i < 10; i++) gfx_rect(px + 28 + i * 6, 146 - i, 4, 4 + i, play_vol > i * 2 ? COL_TEXT : RGB(60, 60, 64));
    gfx_text(px, 156, "OUT", 1, COL_DIM);
    led_meter(px + 28, 156, 184, (st->level_l + st->level_r) / 2, 0);

    draw_wave(14, 190, 452, 44, RGB(90, 220, 140));
    /* progress */
    float p = dur > 0 ? pos / dur : 0;
    if (p > 1) p = 1;
    gfx_rect(14, 241, 452, 3, RGB(50, 50, 54));
    gfx_rect(14, 241, (int)(452 * p), 3, COL_AMBER);

    int hx = 8, hy = 256;
    hint(&hx, hy, BTN_CROSS, play_paused ? "Play" : "Pause");
    hint(&hx, hy, BTN_CIRCLE, "Stop");
    hint(&hx, hy, BTN_DPAD, "Skip 10s");
    hint(&hx, hy, BTN_UPDOWN, "Volume");
    draw_toast();
}

/* ------------------------------------------------------------------ */
/* input                                                               */

static unsigned int pad_now = 0, pad_old = 0;
static float rep_t = 0;
static unsigned int rep_b = 0;
static int pressed(unsigned int b) { return (pad_now & b) && !(pad_old & b); }
static int repeat(unsigned int b, float dt)
{
    if (pressed(b)) { rep_b = b; rep_t = 0.35f; return 1; }
    if ((pad_now & b) && rep_b == b) {
        rep_t -= dt;
        if (rep_t <= 0) { rep_t = 0.08f; return 1; }
    }
    return 0;
}

static void set_play_volume(void)
{
    float v = play_vol / 20.0f;
    player_set_volume((int)(0x8000 * v * v));
}

static void start_playback(int i)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", REC_DIR, recs[i].file);
    input_on = 0; /* the mic is not needed while listening */
    play_i = i;
    play_paused = 0;
    set_play_volume();
    player_open(p, 0, 0);
    screen = SCR_PLAY;
    sfx_play(SFX_CLUNK);
}

static void stop_playback(void)
{
    player_close();
    input_on = 1;
    screen = SCR_LIST;
    sfx_play(SFX_CLUNK);
}

static void handle_input(float dt)
{
    if (screen == SCR_REC) {
        if (pressed(PSP_CTRL_CROSS)) {
            if (!rec_active) start_recording();
            else {
                rec_paused = !rec_paused;
                sfx_play(SFX_CLICK);
            }
        }
        if (pressed(PSP_CTRL_CIRCLE)) stop_recording(0);
        if (repeat(PSP_CTRL_UP, dt) && gain_i < 7) gain_i++;
        if (repeat(PSP_CTRL_DOWN, dt) && gain_i > 0) gain_i--;
        if (pressed(PSP_CTRL_START) && !rec_active) {
            rate_i = (rate_i + 1) % 3;
            char m[64];
            snprintf(m, sizeof m, "Quality: %s (%s)", rate_names[rate_i], rate_desc[rate_i]);
            toast(m);
        }
        if (pressed(PSP_CTRL_SELECT)) {
            vox = !vox;
            toast(vox ? "Voice activation ON: pauses when it's quiet" : "Voice activation OFF");
        }
        if (pressed(PSP_CTRL_SQUARE)) {
            if (rec_active) stop_recording(0);
            scan_recordings();
            list_sel = 0;
            confirm_delete = 0;
            screen = SCR_LIST;
            sfx_play(SFX_CLICK);
        }
    } else if (screen == SCR_LIST) {
        if (confirm_delete) {
            if (pressed(PSP_CTRL_CROSS)) {
                char p[256];
                snprintf(p, sizeof p, "%s/%s", REC_DIR, recs[list_sel].file);
                sceIoRemove(p);
                confirm_delete = 0;
                scan_recordings();
                update_free_space();
                toast("Deleted");
                sfx_play(SFX_EJECT);
            } else if (pressed(PSP_CTRL_CIRCLE)) {
                confirm_delete = 0;
            }
            return;
        }
        if (nrecs > 0) {
            if (repeat(PSP_CTRL_DOWN, dt)) list_sel = (list_sel + 1) % nrecs;
            if (repeat(PSP_CTRL_UP, dt)) list_sel = (list_sel + nrecs - 1) % nrecs;
            if (pressed(PSP_CTRL_CROSS)) start_playback(list_sel);
            else if (pressed(PSP_CTRL_TRIANGLE)) confirm_delete = 1;
        }
        if (pressed(PSP_CTRL_CIRCLE)) {
            screen = SCR_REC;
            sfx_play(SFX_CLICK);
        }
    } else if (screen == SCR_PLAY) {
        const PlayerStatus *st = player_status();
        if (pressed(PSP_CTRL_CROSS)) {
            if (st->state == PL_ENDED) {
                player_seek(0, 0);
                play_paused = 0;
            } else {
                play_paused = !play_paused;
                player_pause(play_paused);
            }
            sfx_play(SFX_CLICK);
        }
        if (pressed(PSP_CTRL_RIGHT)) player_seek(st->pos + 10, play_paused);
        if (pressed(PSP_CTRL_LEFT)) player_seek(st->pos > 10 ? st->pos - 10 : 0, play_paused);
        if (repeat(PSP_CTRL_UP, dt) && play_vol < 20) { play_vol++; set_play_volume(); }
        if (repeat(PSP_CTRL_DOWN, dt) && play_vol > 0) { play_vol--; set_play_volume(); }
        if (pressed(PSP_CTRL_CIRCLE)) stop_playback();
    }
}

/* ------------------------------------------------------------------ */

static int exit_callback(int a, int b, void *c)
{
    (void)a; (void)b; (void)c;
    exit_req = 1;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

#ifndef PREVIEW
int main(void)
{
    int th = sceKernelCreateThread("cb", callback_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
    scePowerSetClockFrequency(333, 333, 166);

    sceDisplaySetMode(0, SCR_W, SCR_H);
    gfx_set_target(drawbuf);
    ui_init();
    make_background();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    player_init();
    /* microphone: gain value as used by other homebrew; fall back to 0 */
    if (sceAudioInputInit(0, 2048, 0) < 0) sceAudioInputInit(0, 0, 0);
    wr_sema = sceKernelCreateSema("wr", 0, 0, 1, 0);
    in_th = sceKernelCreateThread("mic", input_thread, 0x10, 0x8000, PSP_THREAD_ATTR_USER, 0);
    wr_th = sceKernelCreateThread("writer", writer_thread, 0x18, 0x8000, PSP_THREAD_ATTR_USER, 0);
    sceKernelStartThread(in_th, 0, 0);
    sceKernelStartThread(wr_th, 0, 0);

    ensure_dirs();
    update_free_space();

    SceInt64 last = sceKernelGetSystemTimeWide();
    float now = 0, info_t = 0, tick_t = 0;
    int hh = -1, mm = 0, bat = -1;
    float play_wave_t = 0;

    while (!exit_req) {
        SceInt64 t = sceKernelGetSystemTimeWide();
        float dt = (float)(t - last) / 1e6f;
        last = t;
        if (dt > 0.1f) dt = 0.1f;
        now += dt;

        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        pad_old = pad_now;
        pad_now = pad.Buttons;

        info_t -= dt;
        if (info_t <= 0) {
            info_t = 1;
            ScePspDateTime d;
            if (sceRtcGetCurrentClockLocalTime(&d) >= 0) { hh = d.hour; mm = d.minute; }
            bat = scePowerIsBatteryExist() ? scePowerGetBatteryLifePercent() : -1;
        }
        tick_t -= dt;
        if (tick_t <= 0) {
            tick_t = 5;
            if (rec_active || screen == SCR_PLAY) scePowerTick(PSP_POWER_TICK_SUSPEND);
        }
        if (toast_t > 0) toast_t -= dt;

        if (write_error && rec_active) {
            stop_recording(1);
            toast("Recording stopped: Memory Stick write failed");
        }

        handle_input(dt);

        /* reels */
        float target = 0;
        if (screen == SCR_REC && rec_active && !rec_paused && (!vox || vox_open)) target = 1;
        if (screen == SCR_PLAY && !play_paused && player_status()->state == PL_PLAYING) target = 1;
        reel_speed += (target - reel_speed) * (1 - expf(-6 * dt));
        angle_l += reel_speed * 1.1f * dt;
        angle_r += reel_speed * 1.6f * dt;
        if (angle_l > TWO_PI) angle_l -= TWO_PI;
        if (angle_r > TWO_PI) angle_r -= TWO_PI;

        if (screen == SCR_PLAY) {
            /* feed the waveform from the playback level */
            play_wave_t += dt;
            const PlayerStatus *st = player_status();
            while (play_wave_t > 1.0f / 40) {
                play_wave_t -= 1.0f / 40;
                push_wave(st->state == PL_PLAYING ? fmaxf(st->level_l, st->level_r) * 2.2f : 0);
            }
            if (st->state == PL_ENDED) play_paused = 0;
        }

        if (screen == SCR_LIST) {
            if (list_sel < list_scroll) list_scroll = list_sel;
            if (list_sel > list_scroll + 9) list_scroll = list_sel - 9;
        }

        if (screen == SCR_REC) draw_rec_screen(hh, mm, bat, now);
        else if (screen == SCR_LIST) draw_list_screen(hh, mm, bat);
        else draw_play_screen(hh, mm, bat);
        present(2);
    }

    /* HOME -> exit: never lose a recording */
    if (rec_active) stop_recording(1);
    running = 0;
    player_shutdown();
    sceKernelExitGame();
    return 0;
}
#endif
