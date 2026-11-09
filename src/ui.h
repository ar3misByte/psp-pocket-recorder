/* ui.h - screens for the cassette deck (platform independent) */
#ifndef UI_H
#define UI_H

#define NUM_STYLES 6

typedef struct {
    const char *name;
    int ntracks;
    int style;
    float total_sec;
} UiTape;

enum { MODE_STOP = 0, MODE_PLAY, MODE_PAUSE, MODE_FF, MODE_REW };
enum { BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE, BTN_SQUARE, BTN_L, BTN_R, BTN_DPAD, BTN_SELECT, BTN_START, BTN_UPDOWN };

typedef struct {
    /* shared / status */
    int battery;      /* 0..100, <0 unknown */
    int charging;
    int hour, minute; /* <0 unknown */
    const char *toast;
    float toast_alpha; /* 0..1 */

    /* rack screen */
    const UiTape *tapes;
    int ntapes;
    int sel;
    float scroll;       /* smooth scroll position (in rows) */
    float sel_slide;    /* 0..1 animation for selected spine */
    const char *resume_hint; /* e.g. "Resume: Tape name, side A track 2" or NULL */
    const char *music_dir;

    /* deck screen */
    const char *tape_name;
    int tape_style;
    char side;          /* 'A' or 'B' */
    int track_no;       /* 1-based within side */
    int tracks_on_side;
    const char *title;
    const char *artist;
    float pos, dur;     /* seconds within track */
    float side_progress;/* 0..1 */
    int mode;
    float angle_l, angle_r;
    float counter;
    float vu_l, vu_r;   /* needle positions 0..1 */
    int peak_l, peak_r;
    int volume;         /* 0..20 */
    int sleep_min;      /* minutes left, <0 = off */
    int auto_reverse;
    float flip;         /* 0 = none, 0..1 flip animation progress */
    float insert;       /* 0..1, 1 = fully inserted */
    int screen_off;
    float time_sec;     /* global time for blinking etc. */
} UiState;

void ui_init(void);
void ui_draw_rack(const UiState *s);
void ui_draw_deck(const UiState *s);
void ui_draw_screen_off(const UiState *s);
void ui_draw_cassette(float x, float y, float sx, float sy, int style, const char *label, char side,
                      float progress, float angle_l, float angle_r, int detail);
void ui_draw_loading(const char *msg);
void ui_draw_message(const char *title, const char *l1, const char *l2, const char *l3);
int ui_style_for_name(const char *name);
int ui_btn_icon(int x, int y, int type); /* returns icon width */

#endif
