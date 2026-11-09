/* gfx.c - tiny software renderer */
#include "gfx.h"
#include "font8x8.h"
#include <math.h>
#include <string.h>

static uint32_t *fb;
static int cx0 = 0, cy0 = 0, cx1 = SCR_W, cy1 = SCR_H;

void gfx_set_target(uint32_t *buf) { fb = buf; }
uint32_t *gfx_target(void) { return fb; }

void gfx_clip(int x0, int y0, int x1, int y1)
{
    cx0 = x0 < 0 ? 0 : x0;
    cy0 = y0 < 0 ? 0 : y0;
    cx1 = x1 > SCR_W ? SCR_W : x1;
    cy1 = y1 > SCR_H ? SCR_H : y1;
}

void gfx_noclip(void) { cx0 = 0; cy0 = 0; cx1 = SCR_W; cy1 = SCR_H; }

static int cstack[8][4];
static int csp = 0;

void gfx_clip_push(int x0, int y0, int x1, int y1)
{
    if (csp < 8) {
        cstack[csp][0] = cx0; cstack[csp][1] = cy0; cstack[csp][2] = cx1; cstack[csp][3] = cy1;
        csp++;
    }
    if (x0 > cx0) cx0 = x0;
    if (y0 > cy0) cy0 = y0;
    if (x1 < cx1) cx1 = x1;
    if (y1 < cy1) cy1 = y1;
    if (cx1 < cx0) cx1 = cx0;
    if (cy1 < cy0) cy1 = cy0;
}

void gfx_clip_pop(void)
{
    if (csp > 0) {
        csp--;
        cx0 = cstack[csp][0]; cy0 = cstack[csp][1]; cx1 = cstack[csp][2]; cy1 = cstack[csp][3];
    }
}

uint32_t gfx_mix(uint32_t a, uint32_t b, int t)
{
    if (t <= 0) return a;
    if (t >= 255) return b;
    int r = C_R(a) + ((C_R(b) - C_R(a)) * t) / 255;
    int g = C_G(a) + ((C_G(b) - C_G(a)) * t) / 255;
    int bl = C_B(a) + ((C_B(b) - C_B(a)) * t) / 255;
    return RGB(r, g, bl);
}

uint32_t gfx_shade(uint32_t c, int pct)
{
    int r = C_R(c) * pct / 100, g = C_G(c) * pct / 100, b = C_B(c) * pct / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

void gfx_clear(uint32_t c)
{
    for (int y = 0; y < SCR_H; y++) {
        uint32_t *p = fb + y * BUF_W;
        for (int x = 0; x < SCR_W; x++) p[x] = c;
    }
}

void gfx_pixel(int x, int y, uint32_t c)
{
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    fb[y * BUF_W + x] = c;
}

void gfx_blend_pixel(int x, int y, uint32_t c, int alpha)
{
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    uint32_t *p = &fb[y * BUF_W + x];
    *p = gfx_mix(*p, c, alpha);
}

void gfx_rect(int x, int y, int w, int h, uint32_t c)
{
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = fb + j * BUF_W;
        for (int i = x0; i < x1; i++) p[i] = c;
    }
}

void gfx_rect_alpha(int x, int y, int w, int h, uint32_t c, int alpha)
{
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = fb + j * BUF_W;
        for (int i = x0; i < x1; i++) p[i] = gfx_mix(p[i], c, alpha);
    }
}

void gfx_rect_outline(int x, int y, int w, int h, uint32_t c)
{
    gfx_rect(x, y, w, 1, c);
    gfx_rect(x, y + h - 1, w, 1, c);
    gfx_rect(x, y, 1, h, c);
    gfx_rect(x + w - 1, y, 1, h, c);
}

void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int j = 0; j < h; j++) {
        int inset = 0;
        int dy = -1;
        if (j < r) dy = r - j;
        else if (j >= h - r) dy = j - (h - r - 1);
        if (dy > 0) {
            float d = (float)r - sqrtf((float)(r * r - (dy - 0.5f) * (dy - 0.5f)));
            inset = (int)(d + 0.5f);
        }
        gfx_rect(x + inset, y + j, w - inset * 2, 1, c);
    }
}

void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bot)
{
    for (int j = 0; j < h; j++) gfx_rect(x, y + j, w, 1, gfx_mix(top, bot, h > 1 ? j * 255 / (h - 1) : 0));
}

void gfx_circle(int cx, int cy, int r, uint32_t c)
{
    if (r <= 0) return;
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r) - (float)(dy * dy) + 0.25f * r);
        if (dx > r) dx = r;
        gfx_rect(cx - dx, cy + dy, dx * 2 + 1, 1, c);
    }
}

void gfx_circle_alpha(int cx, int cy, int r, uint32_t c, int alpha)
{
    if (r <= 0) return;
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r) - (float)(dy * dy) + 0.25f * r);
        if (dx > r) dx = r;
        gfx_rect_alpha(cx - dx, cy + dy, dx * 2 + 1, 1, c, alpha);
    }
}

void gfx_ring(int cx, int cy, int ro, int ri, uint32_t c)
{
    for (int dy = -ro; dy <= ro; dy++) {
        int xo = (int)sqrtf((float)(ro * ro - dy * dy) + 0.25f * ro);
        if (xo > ro) xo = ro;
        if (dy * dy < ri * ri) {
            int xi = (int)sqrtf((float)(ri * ri - dy * dy));
            gfx_rect(cx - xo, cy + dy, xo - xi, 1, c);
            gfx_rect(cx + xi + 1, cy + dy, xo - xi, 1, c);
        } else {
            gfx_rect(cx - xo, cy + dy, xo * 2 + 1, 1, c);
        }
    }
}

void gfx_line(int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_thick_line(float x0, float y0, float x1, float y1, float w, uint32_t c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    int steps = (int)(len * 2) + 1;
    int r = (int)(w / 2);
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        int px = (int)(x0 + dx * t + 0.5f), py = (int)(y0 + dy * t + 0.5f);
        if (r <= 0) gfx_pixel(px, py, c);
        else gfx_rect(px - r, py - r, r * 2 + 1, r * 2 + 1, c);
    }
}

void gfx_char(int x, int y, unsigned char ch, int scale, uint32_t c)
{
    const unsigned char *g = &font8x8[ch * 8];
    for (int row = 0; row < 8; row++) {
        unsigned char bits = g[row];
        if (!bits) continue;
        for (int col = 0; col < 8; col++)
            if (bits & (0x80 >> col)) {
                if (scale == 1) gfx_pixel(x + col, y + row, c);
                else gfx_rect(x + col * scale, y + row * scale, scale, scale, c);
            }
    }
}

/* font glyphs are ~6px wide; use 7px advance at scale 1 for a tighter look */
#define ADV(scale) (7 * (scale))

int gfx_text_w(const char *s, int scale) { return (int)strlen(s) * ADV(scale); }

void gfx_text(int x, int y, const char *s, int scale, uint32_t c)
{
    for (; *s; s++, x += ADV(scale)) gfx_char(x, y, (unsigned char)*s, scale, c);
}

void gfx_text_shadow(int x, int y, const char *s, int scale, uint32_t c, uint32_t sh)
{
    gfx_text(x + 1, y + 1, s, scale, sh);
    gfx_text(x, y, s, scale, c);
}

void gfx_text_fit(int x, int y, const char *s, int scale, uint32_t c, int maxw)
{
    int maxch = maxw / ADV(scale);
    int n = (int)strlen(s);
    if (n <= maxch) { gfx_text(x, y, s, scale, c); return; }
    if (maxch < 3) return;
    for (int i = 0; i < maxch - 2; i++) gfx_char(x + i * ADV(scale), y, (unsigned char)s[i], scale, c);
    gfx_text(x + (maxch - 2) * ADV(scale), y, "..", scale, c);
}
