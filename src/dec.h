/* dec.h - MP3 (libmpg123) and WAV decoding (portable: same code runs on PSP and PC) */
#ifndef DEC_H
#define DEC_H

#include <stdio.h>

typedef struct {
    void *mh;      /* mpg123_handle */
    FILE *f;
    char *vbuf;    /* stdio read buffer */
    long rate;
    float duration;
    /* WAV files are read directly */
    int is_wav;
    int wav_ch;
    long wav_start, wav_bytes, wav_pos; /* wav_pos: bytes read so far */
} Dec;

int dec_init(void);                      /* once at startup; 0 = ok */
int dec_open(Dec *d, const char *path);  /* 0 = ok */
float dec_seek(Dec *d, float sec);       /* returns actual position, <0 on error */
/* decode up to `frames` stereo 16-bit frames into out.
   returns frames written (>0), 0 at end of track, <0 on error */
int dec_read(Dec *d, short *out, int frames);
void dec_close(Dec *d);

#endif
