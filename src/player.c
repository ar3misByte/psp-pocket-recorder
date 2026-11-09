/* player.c - MP3 playback
 *
 * Decoding is done in software by libmpg123 (checked on a PC to match
 * ffmpeg's output bit for bit). Two threads:
 *   decoder thread - reads the file, decodes and keeps a ring of PCM blocks
 *                    filled about half a second ahead
 *   output thread  - highest priority; only hands ready blocks to the audio
 *                    hardware, so drawing or Memory Stick delays can't starve it
 */
#include "player.h"
#include "dec.h"

#include <pspkernel.h>
#include <psputils.h>
#include <pspaudio.h>
#include <math.h>
#include <string.h>

#define OUT_SAMPLES 1152 /* frames per output block (multiple of 64) */
#define RING 24          /* ~0.6 s of audio at 44.1 kHz */
/* The audio hardware keeps reading a buffer for a while after the output call
   returns. So blocks are copied into a small rotation of dedicated output
   buffers that nothing else ever writes to while they may still be playing. */
#define OUT_BUFS 4

enum { CMD_NONE = 0, CMD_OPEN, CMD_SEEK, CMD_PAUSE, CMD_CLOSE };

typedef struct {
    int type;
    char path[512];
    float sec;
    int paused;
} Cmd;

typedef struct {
    short pcm[OUT_SAMPLES * 2]; /* 4608 bytes = 72 cache lines */
    int epoch;
    int rate;
    float start;
    char pad[64 - 12];          /* keep every block 64-byte aligned */
} __attribute__((aligned(64))) Block;

static PlayerStatus st;
static SceUID lock = -1, dec_th = -1, out_th = -1;
static volatile int quit = 0;
static Cmd pending;
static volatile int has_pending = 0;
static volatile int volume = 0x5000;

/* ring buffer (single producer: decoder, single consumer: output) */
static Block ring[RING] __attribute__((aligned(64)));
static volatile unsigned int wr = 0, rd = 0;
static volatile int epoch = 0;
static volatile int eof_epoch = -1;
static volatile int drained_epoch = -1;

/* decoder state (decoder thread only) */
static char cur_path[512];
static Dec dec;
static int dec_ok = 0;
static int at_eof = 0;
static float base_sec = 0;
static long dec_frames = 0;
static int err_count = 0;

/* output state (output thread only) */
static int src_rate = 0;
static short out_bufs[OUT_BUFS][OUT_SAMPLES * 2] __attribute__((aligned(64)));
static int out_i = 0;

static void lock_take(void) { sceKernelWaitSema(lock, 1, 0); }
static void lock_give(void) { sceKernelSignalSema(lock, 1); }

static void post(const Cmd *c)
{
    lock_take();
    if (c->type == CMD_PAUSE && has_pending && (pending.type == CMD_OPEN || pending.type == CMD_SEEK)) {
        pending.paused = c->paused; /* fold into the pending open/seek */
    } else if (c->type == CMD_SEEK && has_pending && pending.type == CMD_OPEN) {
        pending.sec = c->sec; /* keep the pending file change, move its start */
        pending.paused = c->paused;
    } else {
        pending = *c;
        has_pending = 1;
    }
    lock_give();
}

static void close_decoder(void)
{
    if (dec_ok) dec_close(&dec);
    dec_ok = 0;
}

static void new_epoch(void)
{
    epoch++; /* every queued block becomes stale and is dropped */
    st.level_l = st.level_r = 0;
}

static void fail(int code)
{
    close_decoder();
    new_epoch();
    st.error = code;
    st.state = PL_ERROR;
}

static void start_at(float sec, int paused)
{
    new_epoch();
    err_count = 0;
    at_eof = 0;
    st.generation++;
    if (!dec_ok) {
        if (dec_open(&dec, cur_path) != 0) { fail(-1); return; }
        dec_ok = 1;
    }
    if (sec < 0) sec = 0;
    st.dur = dec.duration;
    if (dec.duration > 0 && sec >= dec.duration - 0.05f) {
        st.pos = dec.duration;
        st.state = PL_ENDED;
        return;
    }
    float actual = dec_seek(&dec, sec);
    if (actual < 0) {
        /* seek failed: reopen from the start as a fallback */
        close_decoder();
        if (dec_open(&dec, cur_path) != 0) { fail(-2); return; }
        dec_ok = 1;
        actual = 0;
    }
    base_sec = actual;
    dec_frames = 0;
    st.pos = actual;
    st.error = 0;
    st.state = paused ? PL_PAUSED : PL_PLAYING;
}

static int ring_space(void) { return RING - (int)(wr - rd); }

/* decode exactly one block into the ring */
static void decode_step(void)
{
    Block *b = &ring[wr % RING];
    int got = 0, n = 1;
    while (got < OUT_SAMPLES) {
        n = dec_read(&dec, b->pcm + got * 2, OUT_SAMPLES - got);
        if (n <= 0) break;
        got += n;
    }
    if (n < 0 && got == 0) {
        if (++err_count > 8) { /* a broken tail counts as the end of the song */
            at_eof = 1;
            eof_epoch = epoch;
        }
        return;
    }
    if (got > 0) {
        if (got < OUT_SAMPLES) memset(b->pcm + got * 2, 0, (OUT_SAMPLES - got) * 4);
        b->epoch = epoch;
        b->rate = (int)dec.rate;
        b->start = base_sec + (float)dec_frames / dec.rate;
        dec_frames += got;
        wr++;
    }
    if (n <= 0) {
        at_eof = 1;
        eof_epoch = epoch;
    }
}

static int decoder_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    while (!quit) {
        Cmd c;
        int have = 0;
        lock_take();
        if (has_pending) {
            c = pending;
            has_pending = 0;
            have = 1;
        }
        lock_give();
        if (have) {
            switch (c.type) {
            case CMD_OPEN:
                if (!dec_ok || strcmp(c.path, cur_path) != 0) {
                    close_decoder();
                    strncpy(cur_path, c.path, sizeof cur_path - 1);
                    cur_path[sizeof cur_path - 1] = 0;
                }
                start_at(c.sec, c.paused);
                break;
            case CMD_SEEK:
                start_at(c.sec, c.paused);
                break;
            case CMD_PAUSE:
                if (st.state == PL_PLAYING && c.paused) st.state = PL_PAUSED;
                else if (st.state == PL_PAUSED && !c.paused) st.state = PL_PLAYING;
                if (c.paused) st.level_l = st.level_r = 0;
                break;
            case CMD_CLOSE:
                close_decoder();
                new_epoch();
                cur_path[0] = 0;
                st.state = PL_IDLE;
                break;
            }
        }
        if (st.state == PL_PLAYING && drained_epoch == epoch && eof_epoch == epoch) {
            st.state = PL_ENDED;
            st.level_l = st.level_r = 0;
        }
        /* decode ahead while playing or paused (so resuming is instant) */
        if (dec_ok && !at_eof && (st.state == PL_PLAYING || st.state == PL_PAUSED) && ring_space() > 0)
            decode_step();
        else
            sceKernelDelayThread(4000);
    }
    close_decoder();
    return 0;
}

static int output_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    while (!quit) {
        if (rd != wr) {
            Block *b = &ring[rd % RING];
            if (b->epoch != epoch) { /* stale block from before a seek */
                rd++;
                continue;
            }
            if (st.state != PL_PLAYING) {
                sceKernelDelayThread(3000);
                continue;
            }
            if (b->rate != src_rate) {
                if (src_rate) sceAudioSRCChRelease();
                src_rate = (sceAudioSRCChReserve(OUT_SAMPLES, b->rate, 2) < 0) ? 0 : b->rate;
                if (!src_rate) {
                    rd++;
                    sceKernelDelayThread(10000);
                    continue;
                }
            }
            /* levels for the VU meters */
            float sl = 0, sr = 0;
            for (int i = 0; i < OUT_SAMPLES; i += 4) {
                float l = b->pcm[i * 2], r = b->pcm[i * 2 + 1];
                sl += l * l;
                sr += r * r;
            }
            st.level_l = sqrtf(sl / (OUT_SAMPLES / 4)) / 32768.0f;
            st.level_r = sqrtf(sr / (OUT_SAMPLES / 4)) / 32768.0f;
            float end = b->start + (float)OUT_SAMPLES / b->rate;
            int ep = b->epoch;
            short *ob = out_bufs[out_i];
            out_i = (out_i + 1) % OUT_BUFS;
            memcpy(ob, b->pcm, sizeof b->pcm);
            rd++; /* the ring slot is free again; the hardware only sees ob */
            sceKernelDcacheWritebackRange(ob, sizeof b->pcm);
            sceAudioSRCOutputBlocking(volume, ob);
            if (ep == epoch) st.pos = end;
        } else {
            if (eof_epoch == epoch) drained_epoch = epoch; /* the decoder turns this into PL_ENDED */
            sceKernelDelayThread(3000);
        }
    }
    if (src_rate) sceAudioSRCChRelease();
    return 0;
}

/* ---------------- sound effects ---------------- */

#define SFX_BLOCK 1024
#define WIND_BLOCKS 4
static short sfx_data[3][SFX_BLOCK * 2 * 6] __attribute__((aligned(64)));
static int sfx_blocks[3];
static short wind_data[SFX_BLOCK * 2 * WIND_BLOCKS] __attribute__((aligned(64)));
static short sfx_silence[SFX_BLOCK * 2] __attribute__((aligned(64)));
static SceUID sfx_sema = -1, sfx_th = -1;
static volatile int sfx_req = -1;
static volatile int wind_on = 0;
static int sfx_ch = -1;

static unsigned int nseed = 1;
static float noise(void)
{
    nseed = nseed * 1664525u + 1013904223u;
    return ((int)(nseed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
}

static void make_sfx(void)
{
    const float sr = 44100.0f, tau = 6.2831853f;
    /* clunk: thump + mechanical noise */
    sfx_blocks[SFX_CLUNK] = 3;
    for (int i = 0; i < SFX_BLOCK * 3; i++) {
        float t = (float)i;
        float v = noise() * expf(-t / 220.0f) * 0.5f + sinf(tau * 95.0f * t / sr) * expf(-t / 1400.0f) * 0.7f +
                  noise() * expf(-fabsf(t - 900.0f) / 60.0f) * 0.25f;
        short s = (short)(v * 9000);
        sfx_data[SFX_CLUNK][i * 2] = sfx_data[SFX_CLUNK][i * 2 + 1] = s;
    }
    /* click: short and light */
    sfx_blocks[SFX_CLICK] = 1;
    for (int i = 0; i < SFX_BLOCK; i++) {
        float t = (float)i;
        float v = noise() * expf(-t / 70.0f) * 0.6f + sinf(tau * 1700.0f * t / sr) * expf(-t / 160.0f) * 0.3f;
        short s = (short)(v * 8000);
        sfx_data[SFX_CLICK][i * 2] = sfx_data[SFX_CLICK][i * 2 + 1] = s;
    }
    /* eject: spring rattle then a clack */
    sfx_blocks[SFX_EJECT] = 6;
    for (int i = 0; i < SFX_BLOCK * 6; i++) {
        float t = (float)i;
        float v = noise() * expf(-t / 300.0f) * 0.4f + sinf(tau * 70.0f * t / sr) * expf(-t / 900.0f) * 0.5f;
        float t2 = t - 3800.0f;
        if (t2 > 0) v += noise() * expf(-t2 / 120.0f) * 0.6f + sinf(tau * 140.0f * t2 / sr) * expf(-t2 / 700.0f) * 0.5f;
        short s = (short)(v * 9000);
        sfx_data[SFX_EJECT][i * 2] = sfx_data[SFX_EJECT][i * 2 + 1] = s;
    }
    /* wind: motor whine + soft tape hiss, loops seamlessly (whole cycles in 4096 samples) */
    float lp = 0;
    for (int i = 0; i < SFX_BLOCK * WIND_BLOCKS; i++) {
        float t = (float)i;
        float f = 17.0f * sr / (SFX_BLOCK * WIND_BLOCKS); /* ~183 Hz */
        lp += (noise() - lp) * 0.15f;
        float v = sinf(tau * f * t / sr) * 0.35f + sinf(tau * 2 * f * t / sr) * 0.18f + sinf(tau * 5 * f * t / sr) * 0.06f + lp * 0.5f;
        short s = (short)(v * 3200);
        wind_data[i * 2] = wind_data[i * 2 + 1] = s;
    }
}

static int sfx_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    int wpos = 0;
    while (!quit) {
        int fire = 0;
        if (wind_on) {
            fire = sceKernelPollSema(sfx_sema, 1) >= 0;
        } else {
            SceUInt timeout = 30000;
            fire = sceKernelWaitSema(sfx_sema, 1, &timeout) >= 0;
        }
        if (fire) {
            int which = sfx_req;
            if (which >= 0 && which <= 2)
                for (int b = 0; b < sfx_blocks[which]; b++)
                    sceAudioOutputBlocking(sfx_ch, PSP_AUDIO_VOLUME_MAX * 3 / 5, &sfx_data[which][b * SFX_BLOCK * 2]);
        }
        if (wind_on) {
            sceAudioOutputBlocking(sfx_ch, PSP_AUDIO_VOLUME_MAX / 2, &wind_data[wpos * SFX_BLOCK * 2]);
            wpos = (wpos + 1) % WIND_BLOCKS;
        } else if (fire) {
            sceAudioOutputBlocking(sfx_ch, 0, sfx_silence);
        }
    }
    return 0;
}

void sfx_play(int which)
{
    if (sfx_sema < 0) return;
    sfx_req = which;
    sceKernelSignalSema(sfx_sema, 1);
}

void sfx_wind(int on) { wind_on = on; }

/* ---------------- public API ---------------- */

int player_init(void)
{
    if (dec_init() != 0) return -1;
    lock = sceKernelCreateSema("plock", 0, 1, 1, 0);
    memset(&st, 0, sizeof st);
    out_th = sceKernelCreateThread("audio_out", output_thread, 0x10, 0x4000, PSP_THREAD_ATTR_USER, 0);
    dec_th = sceKernelCreateThread("audio_dec", decoder_thread, 0x16, 0x20000, PSP_THREAD_ATTR_USER, 0);
    if (out_th < 0 || dec_th < 0) return -1;
    sceKernelStartThread(out_th, 0, 0);
    sceKernelStartThread(dec_th, 0, 0);

    make_sfx();
    sfx_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SFX_BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (sfx_ch >= 0) {
        sfx_sema = sceKernelCreateSema("sfx", 0, 0, 1, 0);
        sfx_th = sceKernelCreateThread("sfx", sfx_thread, 0x12, 0x4000, PSP_THREAD_ATTR_USER, 0);
        if (sfx_th >= 0) sceKernelStartThread(sfx_th, 0, 0);
    }
    return 0;
}

void player_shutdown(void)
{
    quit = 1;
    if (dec_th >= 0) {
        sceKernelWaitThreadEnd(dec_th, 0);
        sceKernelDeleteThread(dec_th);
    }
    if (out_th >= 0) {
        sceKernelWaitThreadEnd(out_th, 0);
        sceKernelDeleteThread(out_th);
    }
    if (sfx_th >= 0) {
        sceKernelSignalSema(sfx_sema, 1);
        sceKernelWaitThreadEnd(sfx_th, 0);
        sceKernelDeleteThread(sfx_th);
    }
    if (sfx_ch >= 0) sceAudioChRelease(sfx_ch);
}

void player_open(const char *path, float sec, int paused)
{
    Cmd c;
    c.type = CMD_OPEN;
    strncpy(c.path, path, sizeof c.path - 1);
    c.path[sizeof c.path - 1] = 0;
    c.sec = sec;
    c.paused = paused;
    post(&c);
}

void player_seek(float sec, int paused)
{
    Cmd c;
    c.type = CMD_SEEK;
    c.path[0] = 0;
    c.sec = sec;
    c.paused = paused;
    post(&c);
}

void player_pause(int paused)
{
    Cmd c;
    c.type = CMD_PAUSE;
    c.path[0] = 0;
    c.sec = 0;
    c.paused = paused;
    post(&c);
}

void player_close(void)
{
    Cmd c;
    c.type = CMD_CLOSE;
    c.path[0] = 0;
    c.sec = 0;
    c.paused = 0;
    post(&c);
}

void player_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > PSP_AUDIO_VOLUME_MAX) vol = PSP_AUDIO_VOLUME_MAX;
    volume = vol;
}

const PlayerStatus *player_status(void) { return &st; }
