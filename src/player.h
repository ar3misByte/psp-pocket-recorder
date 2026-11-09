/* player.h - MP3 playback engine using the PSP's hardware MP3 decoder */
#ifndef PLAYER_H
#define PLAYER_H

enum { PL_IDLE = 0, PL_PLAYING, PL_PAUSED, PL_ENDED, PL_ERROR };

typedef struct {
    volatile int state;
    volatile float pos;      /* seconds into track */
    volatile float dur;      /* seconds */
    volatile float level_l;  /* RMS 0..1 of last block */
    volatile float level_r;
    volatile int error;
    volatile int generation; /* increments on every open/seek, so the UI can tell results apart */
} PlayerStatus;

int player_init(void);   /* loads codec modules and starts the audio thread */
void player_shutdown(void);

/* open a file and start at sec; paused=1 leaves it ready but silent */
void player_open(const char *path, float sec, int paused);
void player_seek(float sec, int paused);
void player_pause(int paused);
void player_close(void);
void player_set_volume(int vol); /* 0..0x8000 */
const PlayerStatus *player_status(void);

/* short mechanical sound effects on a separate channel */
enum { SFX_CLUNK = 0, SFX_CLICK, SFX_EJECT };
void sfx_play(int which);
void sfx_wind(int on); /* looping motor/tape sound while winding */

#endif
