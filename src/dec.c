/* dec.c - MP3 decoding with libmpg123 */
#include "dec.h"
#include "wav.h"
#include <mpg123.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define READ_BUF (64 * 1024) /* big reads are much faster on the Memory Stick */

static int r_read(void *h, void *buf, size_t n, size_t *got)
{
    *got = fread(buf, 1, n, (FILE *)h);
    return ferror((FILE *)h) ? -1 : 0;
}

static int64_t r_lseek(void *h, int64_t off, int whence)
{
    FILE *f = (FILE *)h;
    if (fseek(f, (long)off, whence) != 0) return -1;
    return (int64_t)ftell(f);
}

int dec_init(void) { return mpg123_init() == MPG123_OK ? 0 : -1; }

void dec_close(Dec *d)
{
    if (d->mh) {
        mpg123_close((mpg123_handle *)d->mh);
        mpg123_delete((mpg123_handle *)d->mh);
        d->mh = NULL;
    }
    if (d->f) {
        fclose(d->f);
        d->f = NULL;
    }
    if (d->vbuf) {
        free(d->vbuf);
        d->vbuf = NULL;
    }
}

static int wav_open(Dec *d, const char *path)
{
    d->f = fopen(path, "rb");
    if (!d->f) return -2;
    WavInfo wi;
    if (wav_parse(d->f, &wi) != 0) { dec_close(d); return -6; }
    d->vbuf = malloc(READ_BUF);
    if (d->vbuf) setvbuf(d->f, d->vbuf, _IOFBF, READ_BUF);
    d->is_wav = 1;
    d->wav_ch = wi.channels;
    d->wav_start = wi.data_start;
    d->wav_bytes = wi.data_bytes;
    d->wav_pos = 0;
    d->rate = wi.rate;
    d->duration = (float)wi.data_bytes / (2 * wi.channels) / wi.rate;
    fseek(d->f, d->wav_start, SEEK_SET);
    return 0;
}

static int is_wav_name(const char *path)
{
    size_t n = strlen(path);
    return n > 4 && (!strcmp(path + n - 4, ".wav") || !strcmp(path + n - 4, ".WAV") || !strcmp(path + n - 4, ".Wav"));
}

int dec_open(Dec *d, const char *path)
{
    memset(d, 0, sizeof *d);
    if (is_wav_name(path)) return wav_open(d, path);
    int err = 0;
    mpg123_handle *mh = mpg123_new(NULL, &err);
    if (!mh) return -1;
    d->mh = mh;

    /* always give us 16-bit stereo (mono files are duplicated to both sides);
       fuzzy seeking uses the Xing table instead of scanning the whole file */
    mpg123_param(mh, MPG123_FLAGS, MPG123_FORCE_STEREO | MPG123_QUIET | MPG123_GAPLESS | MPG123_FUZZY | MPG123_SKIP_ID3V2, 0);
    mpg123_format_none(mh);
    const long *rates;
    size_t nrates;
    mpg123_rates(&rates, &nrates);
    for (size_t i = 0; i < nrates; i++) mpg123_format(mh, rates[i], MPG123_STEREO, MPG123_ENC_SIGNED_16);

    d->f = fopen(path, "rb");
    if (!d->f) { dec_close(d); return -2; }
    d->vbuf = malloc(READ_BUF);
    if (d->vbuf) setvbuf(d->f, d->vbuf, _IOFBF, READ_BUF);

    if (mpg123_reader64(mh, r_read, r_lseek, NULL) != MPG123_OK) { dec_close(d); return -3; }
    if (mpg123_open_handle(mh, d->f) != MPG123_OK) { dec_close(d); return -4; }

    long rate;
    int ch, enc;
    if (mpg123_getformat(mh, &rate, &ch, &enc) != MPG123_OK || rate <= 0) { dec_close(d); return -5; }
    d->rate = rate;
    int64_t len = mpg123_length64(mh);
    d->duration = len > 0 ? (float)len / rate : 0;
    return 0;
}

float dec_seek(Dec *d, float sec)
{
    if (sec < 0) sec = 0;
    if (d->is_wav) {
        long frame = (long)(sec * d->rate);
        long pos = frame * 2 * d->wav_ch;
        if (pos > d->wav_bytes) pos = d->wav_bytes;
        if (fseek(d->f, d->wav_start + pos, SEEK_SET) != 0) return -1;
        d->wav_pos = pos;
        return (float)(pos / (2 * d->wav_ch)) / d->rate;
    }
    if (!d->mh) return -1;
    int64_t r = mpg123_seek64((mpg123_handle *)d->mh, (int64_t)(sec * d->rate), SEEK_SET);
    if (r < 0) return -1;
    return (float)r / d->rate; /* where we actually landed */
}

int dec_read(Dec *d, short *out, int frames)
{
    if (d->is_wav) {
        long left = (d->wav_bytes - d->wav_pos) / (2 * d->wav_ch);
        if (left <= 0) return 0;
        if (frames > left) frames = (int)left;
        if (d->wav_ch == 2) {
            size_t got = fread(out, 4, frames, d->f);
            d->wav_pos += (long)got * 4;
            return got > 0 ? (int)got : (ferror(d->f) ? -1 : 0);
        }
        /* mono: read into the second half of out, then spread to both channels */
        short *tmp = out + frames;
        size_t got = fread(tmp, 2, frames, d->f);
        for (size_t i = 0; i < got; i++) out[i * 2] = out[i * 2 + 1] = tmp[i];
        d->wav_pos += (long)got * 2;
        return got > 0 ? (int)got : (ferror(d->f) ? -1 : 0);
    }
    if (!d->mh) return -1;
    for (int tries = 0; tries < 8; tries++) {
        size_t done = 0;
        int r = mpg123_read((mpg123_handle *)d->mh, (unsigned char *)out, (size_t)frames * 4, &done);
        if (done > 0) return (int)(done / 4);
        if (r == MPG123_NEW_FORMAT) {
            long rate;
            int ch, enc;
            if (mpg123_getformat((mpg123_handle *)d->mh, &rate, &ch, &enc) == MPG123_OK && rate > 0) d->rate = rate;
            continue;
        }
        if (r == MPG123_DONE) return 0;
        if (r != MPG123_OK) return -1;
    }
    return 0;
}
