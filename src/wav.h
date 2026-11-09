/* wav.h - minimal 16-bit PCM WAV reading/writing (portable, stdio) */
#ifndef WAV_H
#define WAV_H

#include <stdio.h>

typedef struct {
    int rate;
    int channels;
    long data_start; /* byte offset of the first sample */
    long data_bytes; /* length of the sample data */
} WavInfo;

/* parse the header of an open file; 0 = 16-bit PCM WAV found */
int wav_parse(FILE *f, WavInfo *wi);
/* build a 44-byte header in memory */
void wav_make_header(unsigned char *h, int rate, int channels, long data_bytes);
/* write a 44-byte header at the current position */
void wav_write_header(FILE *f, int rate, int channels, long data_bytes);

#endif
