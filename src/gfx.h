/* gfx.h - tiny software renderer (platform independent) */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512 /* line stride in pixels (PSP framebuffer width) */

/* PSP 8888 pixel format is 0xAABBGGRR */
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))
#define C_R(c) ((c) & 0xFF)
#define C_G(c) (((c) >> 8) & 0xFF)
#define C_B(c) (((c) >> 16) & 0xFF)

void gfx_set_target(uint32_t *buf);
uint32_t *gfx_target(void);
void gfx_clip(int x0, int y0, int x1, int y1); /* inclusive-exclusive */
void gfx_noclip(void);
void gfx_clip_push(int x0, int y0, int x1, int y1); /* intersects with current */
void gfx_clip_pop(void);

uint32_t gfx_mix(uint32_t a, uint32_t b, int t); /* t: 0..255 (0 = a) */
uint32_t gfx_shade(uint32_t c, int pct);         /* pct 100 = same */

void gfx_clear(uint32_t c);
void gfx_pixel(int x, int y, uint32_t c);
void gfx_blend_pixel(int x, int y, uint32_t c, int alpha);
void gfx_rect(int x, int y, int w, int h, uint32_t c);
void gfx_rect_alpha(int x, int y, int w, int h, uint32_t c, int alpha);
void gfx_rect_outline(int x, int y, int w, int h, uint32_t c);
void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c);
void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bot);
void gfx_circle(int cx, int cy, int r, uint32_t c);
void gfx_circle_alpha(int cx, int cy, int r, uint32_t c, int alpha);
void gfx_ring(int cx, int cy, int r_out, int r_in, uint32_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint32_t c);
void gfx_thick_line(float x0, float y0, float x1, float y1, float w, uint32_t c);

/* text: 8x8 bitmap font, integer scale */
int gfx_text_w(const char *s, int scale);
void gfx_text(int x, int y, const char *s, int scale, uint32_t c);
void gfx_text_shadow(int x, int y, const char *s, int scale, uint32_t c, uint32_t sh);
/* draw text clipped to max width; adds ".." if truncated */
void gfx_text_fit(int x, int y, const char *s, int scale, uint32_t c, int maxw);
void gfx_char(int x, int y, unsigned char ch, int scale, uint32_t c);

#endif
