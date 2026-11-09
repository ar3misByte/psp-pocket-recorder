#define PREVIEW
#include "../src/main.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
/* --- mocks mapping PSP calls to the PC --- */
static void mappath(const char *p, char *o) { if (!strncmp(p, "ms0:/", 5)) sprintf(o, "ms/%s", p + 5); else strcpy(o, p); }
int sceIoOpen(const char *p, int fl, int m) { char q[512]; mappath(p, q); return open(q, O_WRONLY | O_CREAT | O_TRUNC, 0644); }
int sceIoClose(int fd) { return close(fd); }
int sceIoWrite(int fd, const void *b, int n) { usleep(2000); return (int)write(fd, b, n); }
int sceIoLseek32(int fd, int off, int wh) { return (int)lseek(fd, off, wh == PSP_SEEK_SET ? SEEK_SET : SEEK_END); }
int sceIoMkdir(const char *p, int m) { char q[512]; mappath(p, q); return mkdir(q, 0755); }
int sceIoRemove(const char *p) { char q[512]; mappath(p, q); return unlink(q); }
int sceRtcGetCurrentClockLocalTime(ScePspDateTime *t) { t->year = 2026; t->month = 11; t->day = 9; t->hour = 18; t->minute = 30; t->second = 0; return 0; }
int sceAudioInputInit(int a, int b, int c) { return 0; }
static int counter = 0;
int sceAudioInputBlocking(int n, int rate, void *buf) {
    /* "mic" produces a numbered sequence; buf is read through the 0x40000000 alias,
       so the test maps that alias back by writing to the aliased address below */
    short *dst = (short *)((unsigned long)buf);
    for (int i = 0; i < n; i++) dst[i] = (short)((counter++ % 20000) - 10000);
    usleep((useconds_t)(1e6 * n / rate / SPEED));
    return 0;
}
void sfx_play(int w) {}
int sceIoDevctl(const char *d, unsigned int c, void *i, int il, void *o, int ol) { return -1; }
static PlayerStatus fake; const PlayerStatus *player_status(void) { return &fake; }
int main(void) {
    system("rm -rf ms && mkdir -p ms");
    gain_i = 0; rate_i = 2; /* x1 gain, 44 kHz */
    wr_sema = sceKernelCreateSema("wr", 0, 0, 1, 0);
    in_th = sceKernelCreateThread("mic", input_thread, 0x10, 0x8000, 0, 0);
    wr_th = sceKernelCreateThread("writer", writer_thread, 0x18, 0x8000, 0, 0);
    sceKernelStartThread(in_th, 0, 0); sceKernelStartThread(wr_th, 0, 0);
    ensure_dirs();
    usleep(100000);
    start_recording();
    int c0 = counter;
    usleep(3000000 / SPEED * 4);   /* ~12 s of audio */
    rec_paused = 1; int cp = counter;
    usleep(1000000 / SPEED * 4);
    rec_paused = 0; int cr = counter;
    usleep(2000000 / SPEED * 4);
    stop_recording(0);
    int ce = counter;
    running = 0; usleep(300000);
    printf("toast: %s\nfile: %s\n", toast_msg, rec_path);
    char q[512]; mappath(rec_path, q);
    FILE *f = fopen(q, "rb"); WavInfo wi; int r = wav_parse(f, &wi);
    printf("wav_parse=%d rate=%d ch=%d bytes=%ld (%.2f s) samples_recorded=%ld\n", r, wi.rate, wi.channels, wi.data_bytes, wi.data_bytes / 2.0 / wi.rate, (long)rec_samples);
    long n = wi.data_bytes / 2; short *d = malloc(n * 2); fseek(f, wi.data_start, SEEK_SET); fread(d, 2, n, f); fclose(f);
    int jumps = 0;
    for (long i = 1; i < n; i++) { int e = ((d[i - 1] + 10000 + 1) % 20000) - 10000; if (d[i] != e) { jumps++; printf("  discontinuity at sample %ld (%.2fs): %d -> %d\n", i, i / 44100.0, d[i-1], d[i]); } }
    printf("discontinuities: %d (expected 1, at the pause)\n", jumps);
    printf("mic samples while recording (excl. pause): ~%d, saved: %ld\n", (cp - c0) + (ce - cr), n);
    return 0;
}
