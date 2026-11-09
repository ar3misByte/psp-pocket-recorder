/* mp3info.h - MP3 header/ID3 parsing (platform independent, uses stdio) */
#ifndef MP3INFO_H
#define MP3INFO_H

typedef struct {
    long file_size;
    long data_start; /* first audio frame */
    long data_end;   /* end of audio (before ID3v1) */
    int sample_rate;
    int channels;
    int bitrate_kbps;     /* of first frame (CBR estimate) */
    int samples_per_frame;
    float duration;       /* seconds */
    int has_toc;
    unsigned char toc[100]; /* Xing seek table */
    char title[96];
    char artist[64];
} Mp3Info;

/* returns 0 on success, <0 if not a playable MPEG audio layer III file */
int mp3_probe(const char *path, Mp3Info *out);

/* parse a 4-byte frame header; returns frame length in bytes or 0 if invalid */
int mp3_frame_header(const unsigned char *h, int *rate, int *spf, int *channels, int *kbps);

/* find a frame start in buf (that is followed by another valid frame); -1 if none */
int mp3_find_frame(const unsigned char *buf, int len);

/* approximate byte offset for a time position */
long mp3_time_to_offset(const Mp3Info *mi, float sec);

#endif
