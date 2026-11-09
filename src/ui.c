/* ui.c - drawing for the cassette deck */
#include "ui.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

typedef struct {
    uint32_t shell, shell_hi, label, stripe, stripe2, ink, window;
    const char *brand;
} TapeStyle;

static TapeStyle styles[NUM_STYLES];
static uint32_t bg_deck[SCR_W * SCR_H];
static uint32_t bg_rack[SCR_W * SCR_H];

/* palette */
#define COL_TEXT RGB(232, 226, 210)
#define COL_DIM RGB(140, 136, 126)
#define COL_AMBER RGB(255, 170, 40)
#define COL_RED RGB(230, 60, 45)
#define COL_GREEN RGB(90, 220, 110)
#define COL_SHADOW RGB(8, 8, 10)

static unsigned int rng = 12345;
static int rnd(int n) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % (unsigned)n); }

int ui_style_for_name(const char *name)
{
    unsigned h = 5381;
    for (; *name; name++) h = h * 33 + (unsigned char)*name;
    return (int)(h % NUM_STYLES);
}

void ui_init(void)
{
    styles[0] = (TapeStyle){RGB(28, 28, 30), RGB(60, 60, 64), RGB(236, 228, 206), RGB(200, 40, 40), RGB(30, 30, 30), RGB(40, 40, 120), RGB(20, 20, 22), "CHROME 90"};
    styles[1] = (TapeStyle){RGB(150, 160, 170), RGB(195, 205, 215), RGB(245, 245, 245), RGB(30, 90, 190), RGB(130, 180, 240), RGB(20, 20, 30), RGB(70, 76, 86), "CLEAR 60"};
    styles[2] = (TapeStyle){RGB(226, 214, 188), RGB(245, 236, 214), RGB(250, 244, 228), RGB(235, 120, 30), RGB(200, 60, 30), RGB(60, 40, 30), RGB(60, 50, 40), "SUNSET 90"};
    styles[3] = (TapeStyle){RGB(28, 44, 96), RGB(60, 80, 140), RGB(205, 210, 220), RGB(250, 200, 40), RGB(30, 44, 96), RGB(20, 30, 70), RGB(16, 22, 40), "HI-FI 100"};
    styles[4] = (TapeStyle){RGB(170, 30, 36), RGB(210, 70, 70), RGB(246, 242, 236), RGB(25, 25, 25), RGB(170, 30, 36), RGB(30, 30, 30), RGB(40, 14, 16), "METAL 60"};
    styles[5] = (TapeStyle){RGB(150, 210, 190), RGB(195, 236, 220), RGB(252, 252, 248), RGB(240, 110, 150), RGB(110, 180, 230), RGB(60, 60, 90), RGB(46, 70, 66), "POP 46"};

    /* brushed metal background for the deck */
    uint32_t *old = gfx_target();
    for (int y = 0; y < SCR_H; y++) {
        int base = 44 - y * 18 / SCR_H;
        int line = rnd(7) - 3;
        for (int x = 0; x < SCR_W; x++) {
            int v = base + line + (rnd(5) - 2);
            if (v < 0) v = 0;
            bg_deck[y * SCR_W + x] = RGB(v, v, v + 3);
        }
    }
    /* wooden rack background */
    for (int y = 0; y < SCR_H; y++) {
        for (int x = 0; x < SCR_W; x++) {
            float grain = sinf(x * 0.05f + sinf(y * 0.03f) * 3.0f + y * 0.004f) * 0.5f + 0.5f;
            int n = rnd(9);
            int r = 70 + (int)(grain * 22) + n, g = 42 + (int)(grain * 14) + n / 2, b = 24 + (int)(grain * 8);
            bg_rack[y * SCR_W + x] = RGB(r, g, b);
        }
    }
    (void)old;
}

static void blit_bg(const uint32_t *bg)
{
    uint32_t *fb = gfx_target();
    for (int y = 0; y < SCR_H; y++) memcpy(fb + y * BUF_W, bg + y * SCR_W, SCR_W * 4);
}

/* ---------- small widgets ---------- */

int ui_btn_icon(int x, int y, int type)
{
    /* 12x12 icon with top-left at x,y */
    switch (type) {
    case BTN_CROSS:
    case BTN_CIRCLE:
    case BTN_TRIANGLE:
    case BTN_SQUARE: {
        gfx_circle(x + 6, y + 6, 6, RGB(20, 20, 22));
        gfx_ring(x + 6, y + 6, 6, 5, RGB(90, 90, 96));
        if (type == BTN_CROSS) {
            uint32_t c = RGB(120, 160, 255);
            gfx_line(x + 3, y + 3, x + 9, y + 9, c);
            gfx_line(x + 9, y + 3, x + 3, y + 9, c);
            gfx_line(x + 4, y + 3, x + 9, y + 8, c);
            gfx_line(x + 8, y + 3, x + 3, y + 8, c);
        } else if (type == BTN_CIRCLE) {
            gfx_ring(x + 6, y + 6, 3, 2, RGB(255, 100, 100));
        } else if (type == BTN_TRIANGLE) {
            uint32_t c = RGB(90, 220, 170);
            gfx_line(x + 6, y + 3, x + 3, y + 8, c);
            gfx_line(x + 6, y + 3, x + 9, y + 8, c);
            gfx_line(x + 3, y + 8, x + 9, y + 8, c);
        } else {
            gfx_rect_outline(x + 3, y + 3, 7, 7, RGB(240, 140, 210));
        }
        break;
    }
    case BTN_L:
    case BTN_R:
        gfx_rrect(x, y + 1, 14, 10, 3, RGB(70, 70, 76));
        gfx_text(x + 4, y + 2, type == BTN_L ? "L" : "R", 1, COL_TEXT);
        break;
    case BTN_DPAD:
    case BTN_UPDOWN:
        gfx_rect(x + 4, y, 4, 12, RGB(70, 70, 76));
        gfx_rect(x, y + 4, 12, 4, RGB(70, 70, 76));
        if (type == BTN_DPAD) {
            gfx_rect(x + 9, y + 5, 2, 2, COL_TEXT);
            gfx_rect(x + 1, y + 5, 2, 2, COL_TEXT);
        } else {
            gfx_rect(x + 5, y + 1, 2, 2, COL_TEXT);
            gfx_rect(x + 5, y + 9, 2, 2, COL_TEXT);
        }
        break;
    case BTN_SELECT:
    case BTN_START:
        gfx_rrect(x, y + 3, 16, 6, 3, RGB(70, 70, 76));
        return 16;
    }
    return (type == BTN_L || type == BTN_R) ? 14 : 12;
}

/* draws "icon label" and returns x after it */
static int hint(int x, int y, int icon, const char *label)
{
    ui_btn_icon(x, y, icon);
    int iw = (icon == BTN_L || icon == BTN_R) ? 14 : (icon == BTN_SELECT || icon == BTN_START) ? 16 : 12;
    gfx_text(x + iw + 3, y + 2, label, 1, COL_DIM);
    return x + iw + 3 + gfx_text_w(label, 1) + 8;
}

static void battery_icon(int x, int y, int pct, int charging)
{
    gfx_rect_outline(x, y, 20, 10, COL_DIM);
    gfx_rect(x + 20, y + 3, 2, 4, COL_DIM);
    if (pct >= 0) {
        uint32_t c = pct <= 15 ? COL_RED : pct <= 35 ? COL_AMBER : COL_GREEN;
        gfx_rect(x + 2, y + 2, (16 * pct) / 100, 6, c);
    }
    if (charging) {
        gfx_line(x + 11, y + 1, x + 8, y + 5, COL_AMBER);
        gfx_line(x + 8, y + 5, x + 12, y + 5, COL_AMBER);
        gfx_line(x + 12, y + 5, x + 9, y + 9, COL_AMBER);
    }
}

static void status_right(const UiState *s, int y)
{
    char buf[16];
    int x = SCR_W - 8;
    if (s->hour >= 0) {
        snprintf(buf, sizeof buf, "%02d:%02d", s->hour, s->minute);
        x -= gfx_text_w(buf, 1);
        gfx_text(x, y + 1, buf, 1, COL_TEXT);
        x -= 10;
    }
    if (s->battery >= 0 || s->charging) {
        x -= 22;
        battery_icon(x, y, s->battery, s->charging);
        if (s->battery >= 0) {
            snprintf(buf, sizeof buf, "%d%%", s->battery);
            x -= gfx_text_w(buf, 1) + 4;
            gfx_text(x, y + 1, buf, 1, COL_DIM);
        }
    }
}

static void toast(const UiState *s)
{
    if (!s->toast || s->toast_alpha <= 0.01f) return;
    int a = (int)(s->toast_alpha * 255);
    int w = gfx_text_w(s->toast, 1) + 24;
    int x = (SCR_W - w) / 2, y = 120;
    gfx_rect_alpha(x, y, w, 22, RGB(10, 10, 12), a * 220 / 255);
    gfx_rect_alpha(x, y, w, 1, COL_AMBER, a);
    gfx_rect_alpha(x, y + 21, w, 1, COL_AMBER, a);
    uint32_t c = gfx_mix(RGB(10, 10, 12), COL_TEXT, a);
    gfx_text(x + 12, y + 7, s->toast, 1, c);
}

/* ---------- the cassette ---------- */

/* trapezoid-ish lower guard; rows narrow toward the top */
static void trapezoid(float x0, float y0, float x1, float y1, float inset, uint32_t c)
{
    int h = (int)(y1 - y0);
    for (int j = 0; j < h; j++) {
        float t = 1.0f - (float)j / (h > 1 ? h - 1 : 1);
        int in = (int)(inset * t);
        gfx_rect((int)x0 + in, (int)y0 + j, (int)(x1 - x0) - in * 2, 1, c);
    }
}

static void screw(int x, int y, int r, uint32_t shell)
{
    gfx_circle(x, y, r, gfx_shade(shell, 60));
    gfx_circle(x, y, r - 1 > 0 ? r - 1 : 1, RGB(160, 160, 165));
    if (r >= 3) {
        gfx_line(x - r + 1, y, x + r - 1, y, RGB(70, 70, 74));
        gfx_line(x, y - r + 1, x, y + r - 1, RGB(70, 70, 74));
    }
}

static void hub(int cx, int cy, float r_out, float angle, uint32_t col, uint32_t hole)
{
    int ro = (int)(r_out + 0.5f), ri = (int)(r_out * 0.66f + 0.5f);
    gfx_circle(cx, cy, ro, col);
    gfx_circle(cx, cy, ri, hole);
    /* 6 teeth pointing inward, rotating */
    for (int i = 0; i < 6; i++) {
        float a = angle + i * (float)M_PI / 3.0f;
        float ca = cosf(a), sa = sinf(a);
        float w = r_out > 6 ? 2.0f : 1.0f;
        gfx_thick_line(cx + ca * ri, cy + sa * ri, cx + ca * (ri * 0.55f), cy + sa * (ri * 0.55f), w, col);
    }
    /* small marker so rotation is visible */
    float a = angle + (float)M_PI / 6.0f;
    gfx_circle((int)(cx + cosf(a) * r_out * 0.83f), (int)(cy + sinf(a) * r_out * 0.83f), r_out > 6 ? 1 : 0,
               gfx_shade(col, 70));
}

void ui_draw_cassette(float x, float y, float sx, float sy, int style, const char *label, char side,
                      float progress, float angle_l, float angle_r, int detail)
{
    const TapeStyle *st = &styles[style % NUM_STYLES];
#define X(u) ((int)(x + (u) * sx + 0.5f))
#define Y(u) ((int)(y + (u) * sy + 0.5f))
#define W(u) ((int)((u) * sx + 0.5f))
#define H(u) ((int)((u) * sy + 0.5f))
    float smin = sx < sy ? sx : sy;
    if (sx < 0.2f) return;

    /* drop shadow + body */
    gfx_rrect(X(1.2f), Y(1.6f), W(100), H(63), (int)(3 * smin), COL_SHADOW);
    gfx_rrect(X(0), Y(0), W(100), H(63), (int)(3 * smin), st->shell);
    gfx_rect(X(2), Y(0.6f), W(96), H(0.8f) > 0 ? H(0.8f) : 1, st->shell_hi); /* top bevel */

    /* label */
    gfx_rrect(X(6), Y(4), W(88), H(38), (int)(2 * smin), st->label);
    gfx_rect(X(6), Y(15.5f), W(88), H(2.2f), st->stripe);
    gfx_rect(X(6), Y(18.2f), W(88), H(1.0f) > 0 ? H(1.0f) : 1, st->stripe2);
    if (detail) {
        /* ruled line for handwriting */
        gfx_rect(X(9), Y(13.2f), W(82), 1, gfx_mix(st->label, st->ink, 60));
        if (sx >= 1.5f) {
            char sd[2] = {side, 0};
            gfx_text(X(8), Y(23), sd, sx >= 2.2f ? 3 : 2, st->ink);
            /* brand on the right of the writing line */
            int bw = gfx_text_w(st->brand, 1);
            gfx_text(X(91) - bw, Y(13.2f) - 9, st->brand, 1, gfx_mix(st->label, st->ink, 110));
            if (label) gfx_text_fit(X(10), Y(13.2f) - 9, label, 1, st->ink, X(91) - bw - 10 - X(10));
        }
    }

    /* window */
    int wx = X(18), wy = Y(20.5f), ww = W(64), wh = H(17.5f);
    gfx_rrect(wx - 1, wy - 1, ww + 2, wh + 2, (int)(2.5f * smin), gfx_shade(st->label, 70));
    gfx_rrect(wx, wy, ww, wh, (int)(2.5f * smin), st->window);

    gfx_clip_push(wx + 1, wy + 1, wx + ww - 1, wy + wh - 1);
    float hub_r = 4.4f * smin;
    float pmin = 5.4f * smin, pmax = 14.5f * smin;
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    float rl = sqrtf(pmin * pmin + (pmax * pmax - pmin * pmin) * (1.0f - progress));
    float rr = sqrtf(pmin * pmin + (pmax * pmax - pmin * pmin) * progress);
    int hlx = X(32), hrx = X(68), hy = Y(29.25f);
    uint32_t tape = RGB(74, 44, 28), tape_edge = RGB(110, 70, 44);
    gfx_circle(hlx, hy, (int)rl, tape_edge);
    gfx_circle(hlx, hy, (int)rl - 1, tape);
    gfx_circle(hrx, hy, (int)rr, tape_edge);
    gfx_circle(hrx, hy, (int)rr - 1, tape);
    /* tape path along the bottom of the window */
    gfx_rect(hlx, Y(37.2f), hrx - hlx, H(0.8f) > 0 ? H(0.8f) : 1, tape);
    if (detail) {
        /* window glare */
        for (int i = 0; i < W(10); i++)
            gfx_line(wx + W(8) + i, wy, wx + W(2) + i, wy + wh, gfx_mix(st->window, RGB(255, 255, 255), 30));
    }
    gfx_clip_pop();

    /* hubs (sit in round holes through the label) */
    gfx_ring(hlx, hy, (int)(hub_r + 2 * smin), (int)(hub_r), gfx_shade(st->window, 70));
    gfx_ring(hrx, hy, (int)(hub_r + 2 * smin), (int)(hub_r), gfx_shade(st->window, 70));
    hub(hlx, hy, hub_r, angle_l, RGB(238, 236, 228), st->window);
    hub(hrx, hy, hub_r, angle_r, RGB(238, 236, 228), st->window);

    /* lower guard with holes */
    uint32_t guard = gfx_shade(st->shell, 85);
    trapezoid(X(17), Y(47), X(83), Y(63), 4 * sx, guard);
    gfx_rect(X(17), Y(47), W(66), 1, gfx_shade(st->shell, 120));
    if (detail) {
        gfx_circle(X(31), Y(56), (int)(2.2f * smin), COL_SHADOW);
        gfx_circle(X(69), Y(56), (int)(2.2f * smin), COL_SHADOW);
        gfx_rect(X(42), Y(54), W(4), H(4), COL_SHADOW);
        gfx_rect(X(54), Y(54), W(4), H(4), COL_SHADOW);
        gfx_rect(X(48.5f), Y(58.5f), W(3), H(3), COL_SHADOW);
        int sr = (int)(1.7f * smin + 0.5f);
        screw(X(3.5f), Y(3.5f), sr, st->shell);
        screw(X(96.5f), Y(3.5f), sr, st->shell);
        screw(X(3.5f), Y(59.5f), sr, st->shell);
        screw(X(96.5f), Y(59.5f), sr, st->shell);
        screw(X(50), Y(51), sr, st->shell);
    }
#undef X
#undef Y
#undef W
#undef H
}

/* ---------- VU meter ---------- */

static void vu_meter(int x, int y, int w, int h, float v, int peak, const char *ch)
{
    /* housing */
    gfx_rrect(x - 2, y - 2, w + 4, h + 4, 4, RGB(14, 14, 16));
    gfx_vgrad(x, y, w, h, RGB(250, 226, 160), RGB(226, 186, 110));
    int px = x + w / 2, py = y + h + 26; /* pivot below the face */
    float R = (float)(py - y) - 10;
    /* scale arc with ticks: -20 .. +3 dB */
    static const float marks[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    for (int i = 0; i < 11; i++) {
        float t = (marks[i] + 20) / 23.0f;
        float t2 = powf(t, 1.6f);
        float a = (-50 + 100 * t2) * (float)M_PI / 180.0f;
        uint32_t c = marks[i] > 0 ? RGB(200, 30, 20) : RGB(40, 30, 20);
        float r0 = R - 2, r1 = R - (i % 2 == 0 || marks[i] == 0 ? 8 : 5);
        gfx_thick_line(px + sinf(a) * r0, py - cosf(a) * r0, px + sinf(a) * r1, py - cosf(a) * r1, 1, c);
    }
    /* arc line */
    for (int i = 0; i <= 100; i++) {
        float a = (-50 + i) * (float)M_PI / 180.0f;
        float t = (float)i / 100.0f;
        uint32_t c = t > powf(20.0f / 23.0f, 1.6f) ? RGB(200, 30, 20) : RGB(40, 30, 20);
        gfx_pixel((int)(px + sinf(a) * (R - 2)), (int)(py - cosf(a) * (R - 2)), c);
        gfx_pixel((int)(px + sinf(a) * (R - 1)), (int)(py - cosf(a) * (R - 1)), c);
    }
    gfx_text(x + 6, y + h - 12, "VU", 1, RGB(60, 40, 20));
    gfx_text(x + w - 14, y + h - 12, ch, 1, RGB(60, 40, 20));
    /* peak led */
    gfx_circle(x + w - 9, y + 8, 3, peak ? RGB(255, 50, 40) : RGB(90, 30, 25));
    /* needle */
    if (v < 0) v = 0;
    if (v > 1.08f) v = 1.08f;
    float a = (-50 + 100 * v) * (float)M_PI / 180.0f;
    gfx_clip_push(x, y, x + w, y + h);
    gfx_thick_line(px, py, px + sinf(a) * (R + 2), py - cosf(a) * (R + 2), 1, RGB(20, 16, 14));
    gfx_clip_pop();
    /* pivot cover */
    gfx_rect(x, y + h - 4, w, 4, RGB(40, 36, 30));
    /* glass glare */
    gfx_rect_alpha(x, y, w, h / 3, RGB(255, 255, 255), 28);
}

/* ---------- tape counter ---------- */

static void counter(int x, int y, float value)
{
    int w = 22, h = 30;
    if (value < 0) value = 0;
    int v = (int)value;
    float frac = value - v;
    gfx_rrect(x - 4, y - 4, w * 3 + 4 * 2 + 8, h + 8, 3, RGB(12, 12, 14));
    for (int d = 0; d < 3; d++) {
        int dx = x + d * (w + 4);
        gfx_vgrad(dx, y, w, h, RGB(30, 30, 32), RGB(10, 10, 12));
        int div = d == 0 ? 100 : d == 1 ? 10 : 1;
        int digit = (v / div) % 10;
        int next = (digit + 1) % 10;
        /* the ones digit rolls continuously; higher digits roll when lower ones pass 9 */
        float roll = 0;
        if (d == 2) roll = frac;
        else if ((v % div) == div - 1) roll = frac;
        int off = (int)(roll * (h - 2));
        gfx_clip_push(dx, y, dx + w, y + h);
        char c1[2] = {(char)('0' + digit), 0}, c2[2] = {(char)('0' + next), 0};
        gfx_text(dx + 4, y + 7 - off, c1, 2, RGB(240, 236, 224));
        gfx_text(dx + 4, y + 7 - off + (h - 2), c2, 2, RGB(240, 236, 224));
        gfx_clip_pop();
        gfx_rect_alpha(dx, y, w, 6, RGB(0, 0, 0), 120);
        gfx_rect_alpha(dx, y + h - 6, w, 6, RGB(0, 0, 0), 120);
        gfx_rect(dx, y + h / 2, w, 1, RGB(0, 0, 0));
    }
}

static void led(int x, int y, int on, uint32_t c, const char *label)
{
    gfx_circle(x + 3, y + 3, 3, on ? c : gfx_shade(c, 25));
    if (on) gfx_circle_alpha(x + 3, y + 3, 6, c, 50);
    gfx_text(x + 10, y, label, 1, on ? COL_TEXT : COL_DIM);
}

static void fmt_time(char *buf, int n, float sec)
{
    if (sec < 0) sec = 0;
    int t = (int)sec;
    snprintf(buf, n, "%d:%02d", t / 60, t % 60);
}

/* ---------- screens ---------- */

void ui_draw_deck(const UiState *s)
{
    blit_bg(bg_deck);
    char buf[96];

    /* status bar */
    gfx_rect(0, 0, SCR_W, 18, RGB(14, 14, 16));
    gfx_rect(0, 18, SCR_W, 1, RGB(60, 60, 64));
    snprintf(buf, sizeof buf, "SIDE %c", s->side);
    gfx_text(8, 5, buf, 1, COL_AMBER);
    if (s->tracks_on_side > 0) {
        snprintf(buf, sizeof buf, "TRACK %02d/%02d", s->track_no, s->tracks_on_side);
        gfx_text(64, 5, buf, 1, COL_TEXT);
    }
    int vx = 170;
    gfx_text(vx, 5, "VOL", 1, COL_DIM);
    for (int i = 0; i < 10; i++) {
        int on = s->volume > i * 2;
        gfx_rect(vx + 26 + i * 4, 12 - i / 2, 3, 2 + i / 2, on ? COL_TEXT : RGB(60, 60, 64));
    }
    if (s->sleep_min >= 0) {
        snprintf(buf, sizeof buf, "SLEEP %dm", s->sleep_min);
        gfx_text(242, 5, buf, 1, COL_AMBER);
    }
    status_right(s, 4);

    /* cassette well */
    int wx = 10, wy = 24, ww = 284, wh = 182;
    gfx_rrect(wx, wy, ww, wh, 6, RGB(10, 10, 12));
    gfx_rrect(wx + 2, wy + 2, ww - 4, wh - 4, 5, RGB(22, 22, 25));
    gfx_rect(wx + 6, wy + 3, ww - 12, 1, RGB(4, 4, 4));

    /* cassette, with flip/insert animations */
    float sx = 2.64f, sy = 2.64f;
    float cw = 100 * sx, ch = 63 * sy;
    float cx = wx + (ww - cw) / 2, cy = wy + (wh - ch) / 2 - 2;
    char side = s->side;
    if (s->flip > 0 && s->flip < 1) {
        float f = cosf(s->flip * (float)M_PI);
        if (s->flip < 0.5f) side = (s->side == 'A') ? 'B' : 'A';
        float k = fabsf(f);
        if (k < 0.04f) k = 0.04f;
        cx += cw * (1 - k) / 2;
        sx *= k;
    }
    if (s->insert < 1) cy -= (1 - s->insert) * (1 - s->insert) * 200;
    gfx_clip_push(wx + 2, wy + 2, wx + ww - 2, wy + wh - 2);
    if (s->tape_name) {
        /* clip region is respected by primitives; draw cassette */
        ui_draw_cassette(cx, cy, sx, sy, s->tape_style, s->tape_name, side, s->side_progress, s->angle_l,
                         s->angle_r, 1);
    }
    gfx_clip_pop();
    /* door glass */
    gfx_rect_alpha(wx + 4, wy + 4, ww - 8, 22, RGB(255, 255, 255), 10);

    /* right panel: VU meters + counter */
    int px = 304;
    vu_meter(px, 26, 166, 54, s->vu_l, s->peak_l, "L");
    vu_meter(px, 92, 166, 54, s->vu_r, s->peak_r, "R");
    gfx_text(px, 160, "COUNTER", 1, COL_DIM);
    counter(px + 4, 174, s->counter);
    int lx = px + 98;
    int playing = s->mode == MODE_PLAY || s->mode == MODE_FF || s->mode == MODE_REW;
    led(lx, 162, playing, COL_GREEN, "PLAY");
    led(lx, 176, s->mode == MODE_PAUSE && ((int)(s->time_sec * 2) % 2 == 0), COL_AMBER, "PAUSE");
    led(lx, 190, s->auto_reverse, COL_AMBER, "AUTO REV");

    /* track info */
    int ty = 212;
    const char *title = s->title ? s->title : "No tape";
    gfx_text_fit(10, ty, title, 2, COL_TEXT, 330);
    if (s->artist && s->artist[0]) gfx_text_fit(10, ty + 19, s->artist, 1, COL_DIM, 330);

    /* transport mode label */
    const char *ml = s->mode == MODE_PLAY ? "PLAY" : s->mode == MODE_PAUSE ? "PAUSE" : s->mode == MODE_FF ? "FF >>" : s->mode == MODE_REW ? "<< REW" : "STOP";
    gfx_text(SCR_W - 10 - gfx_text_w(ml, 2), ty, ml, 2, s->mode == MODE_STOP ? COL_DIM : COL_AMBER);

    /* progress bar */
    int by = 244, bx = 10, bw = 400;
    fmt_time(buf, sizeof buf, s->pos);
    gfx_rect(bx, by, bw, 3, RGB(50, 50, 54));
    float p = s->dur > 0 ? s->pos / s->dur : 0;
    if (p > 1) p = 1;
    gfx_rect(bx, by, (int)(bw * p), 3, COL_AMBER);
    gfx_rect(bx + (int)(bw * p) - 1, by - 2, 3, 7, COL_TEXT);
    char t2[16];
    fmt_time(t2, sizeof t2, s->dur);
    snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " / %s", t2);
    gfx_text(SCR_W - 10 - gfx_text_w(buf, 1), by - 2, buf, 1, COL_DIM);

    /* hints */
    int hx = 8, hy = 256;
    hx = hint(hx, hy, BTN_CROSS, "Play");
    hx = hint(hx, hy, BTN_CIRCLE, "Stop");
    hx = hint(hx, hy, BTN_L, "Rew");
    hx = hint(hx, hy, BTN_R, "FF");
    hx = hint(hx, hy, BTN_DPAD, "Track");
    hx = hint(hx, hy, BTN_TRIANGLE, "Flip");
    hx = hint(hx, hy, BTN_SQUARE, "Sleep");
    hx = hint(hx, hy, BTN_SELECT, "Screen");
    (void)hx;

    toast(s);
}

void ui_draw_rack(const UiState *s)
{
    blit_bg(bg_rack);
    char buf[96];

    /* header */
    gfx_rect(0, 0, SCR_W, 22, RGB(20, 14, 10));
    gfx_rect(0, 22, SCR_W, 2, RGB(120, 84, 50));
    gfx_text(10, 4, "TAPE RACK", 2, COL_AMBER);
    status_right(s, 6);

    if (s->ntapes == 0) {
        gfx_rrect(40, 60, 400, 150, 8, RGB(20, 14, 10));
        gfx_text(60, 76, "No tapes found", 2, COL_TEXT);
        gfx_text(60, 104, "Put your MP3s in folders like:", 1, COL_DIM);
        gfx_text(60, 120, "ms0:/MUSIC/Road Trip/01 song.mp3", 1, COL_AMBER);
        gfx_text(60, 134, "ms0:/MUSIC/Chill/track.mp3", 1, COL_AMBER);
        gfx_text(60, 156, "Each folder becomes one cassette.", 1, COL_DIM);
        gfx_text(60, 170, "Tracks are split into Side A / Side B.", 1, COL_DIM);
        gfx_text(60, 190, "Press START to rescan, HOME to quit.", 1, COL_DIM);
        toast(s);
        return;
    }

    /* the rack: spines */
    int rx = 12, ry = 32, row_h = 24, rows = 9, rw = 218;
    /* rack frame */
    gfx_rect(rx - 6, ry - 4, rw + 12, rows * row_h + 8, RGB(40, 26, 16));
    gfx_clip_push(rx - 6, ry - 2, rx + rw + 40, ry + rows * row_h + 2);
    for (int i = 0; i < s->ntapes; i++) {
        float rowf = i - s->scroll;
        int y = ry + (int)(rowf * row_h);
        if (y < ry - row_h || y > ry + rows * row_h) continue;
        const TapeStyle *st = &styles[s->tapes[i].style % NUM_STYLES];
        int out = (i == s->sel) ? (int)(18 * s->sel_slide) : 0;
        int x = rx + out;
        gfx_rect(x + 2, y + 3, rw, row_h - 3, COL_SHADOW);
        gfx_rect(x, y + 1, rw, row_h - 3, st->shell);
        gfx_rect(x + 8, y + 4, rw - 16, row_h - 9, st->label);
        gfx_rect(x + 8, y + row_h - 8, rw - 16, 2, st->stripe);
        gfx_text_fit(x + 12, y + 6, s->tapes[i].name, 1, st->ink, rw - 60);
        snprintf(buf, sizeof buf, "%2d", s->tapes[i].ntracks);
        gfx_text(x + rw - 30, y + 6, buf, 1, gfx_mix(st->label, st->ink, 150));
        if (i == s->sel) gfx_rect_outline(x - 1, y, rw + 2, row_h - 1, COL_AMBER);
    }
    gfx_clip_pop();
    /* shelf lips */
    gfx_rect(rx - 6, ry + rows * row_h + 2, rw + 12, 4, RGB(120, 84, 50));

    /* selected tape preview */
    const UiTape *t = &s->tapes[s->sel];
    int px = 252, py = 34;
    gfx_rrect(px - 6, py - 4, 228, 228, 8, RGB(20, 14, 10));
    ui_draw_cassette(px + 4, py + 4, 2.06f, 2.06f, t->style, t->name, 'A', 0.0f, 0.3f, 0.3f, 1);
    gfx_text_fit(px + 4, py + 144, t->name, 2, COL_TEXT, 212);
    int na = (t->ntracks + 1) / 2, nb = t->ntracks - na;
    snprintf(buf, sizeof buf, "%d tracks  (A:%d  B:%d)", t->ntracks, na, nb);
    gfx_text(px + 4, py + 166, buf, 1, COL_DIM);
    if (t->total_sec > 0) {
        int m = (int)(t->total_sec / 60);
        snprintf(buf, sizeof buf, "about %d min", m);
        gfx_text(px + 4, py + 180, buf, 1, COL_DIM);
    }
    if (s->resume_hint) gfx_text_fit(px + 4, py + 200, s->resume_hint, 1, COL_AMBER, 212);

    /* hints */
    int hx = 8, hy = 256;
    gfx_rect(0, 250, SCR_W, 22, RGB(20, 14, 10));
    hx = hint(hx, hy, BTN_UPDOWN, "Choose");
    hx = hint(hx, hy, BTN_CROSS, "Insert tape");
    if (s->resume_hint) hx = hint(hx, hy, BTN_TRIANGLE, "Resume");
    hx = hint(hx, hy, BTN_START, "Rescan");
    (void)hx;

    toast(s);
}

void ui_draw_screen_off(const UiState *s)
{
    gfx_clear(RGB(0, 0, 0));
    /* faint indicator for the first few seconds only (caller decides) */
    if (s->toast_alpha > 0.01f) {
        int a = (int)(s->toast_alpha * 160);
        uint32_t c = gfx_mix(RGB(0, 0, 0), COL_DIM, a);
        const char *m = "Walkman mode - press SELECT to wake";
        gfx_text((SCR_W - gfx_text_w(m, 1)) / 2, 130, m, 1, c);
    }
}

void ui_draw_loading(const char *msg)
{
    blit_bg(bg_deck);
    gfx_text((SCR_W - gfx_text_w(msg, 1)) / 2, 130, msg, 1, COL_TEXT);
}

void ui_draw_message(const char *title, const char *l1, const char *l2, const char *l3)
{
    blit_bg(bg_deck);
    gfx_rrect(40, 70, 400, 130, 8, RGB(14, 14, 16));
    gfx_text(60, 86, title, 2, COL_AMBER);
    if (l1) gfx_text_fit(60, 120, l1, 1, COL_TEXT, 360);
    if (l2) gfx_text_fit(60, 136, l2, 1, COL_TEXT, 360);
    if (l3) gfx_text_fit(60, 160, l3, 1, COL_DIM, 360);
}
