/* preview.c - renders the recorder screens on a PC (uses main.c's drawing code) */
#define PREVIEW
#include "../src/main.c"
static PlayerStatus fake;
const PlayerStatus *player_status(void) { return &fake; }
void sfx_play(int w) { (void)w; }
static void save(const char *path)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n480 272\n255\n");
    for (int y = 0; y < 272; y++)
        for (int x = 0; x < 480; x++) {
            uint32_t c = drawbuf[y * BUF_W + x];
            unsigned char p[3] = {C_R(c), C_G(c), C_B(c)};
            fwrite(p, 1, 3, f);
        }
    fclose(f);
}
int main(void)
{
    gfx_set_target(drawbuf);
    ui_init();
    make_background();
    free_bytes = 1500L * 1024 * 1024;
    /* fake speech-like waveform */
    for (int i = 0; i < 700; i++) {
        float env = fabsf(sinf(i * 0.07f)) * (0.5f + 0.5f * sinf(i * 0.013f));
        push_wave(env * env * 0.6f + 0.002f);
    }
    /* recording */
    rec_active = 1; rec_rate = 22050; rec_samples = 22050L * 83;
    strcpy(rec_name, "REC_20261109_181205.wav");
    mic_level = 0.12f; mic_peak = 0.45f; angle_l = 0.5f; angle_r = 1.3f;
    draw_rec_screen(18, 12, 64, 0.1f);
    save("rec.ppm");
    /* idle with voice activation, toast */
    rec_active = 0; vox = 1; mic_level = 0.01f; mic_peak = 0.03f;
    toast("Quality: 22 kHz (Standard)"); toast_t = 1;
    draw_rec_screen(18, 12, 64, 0.1f);
    save("idle.ppm");
    toast_t = 0;
    /* list */
    const char *files[] = {"REC_20261109_181205.wav", "REC_20261109_090012.wav", "REC_20261108_221530.wav", "REC_20261105_140001.wav", "Lecture notes.wav"};
    float durs[] = {83, 412, 15, 1934, 3720};
    nrecs = 5;
    for (int i = 0; i < 5; i++) { strcpy(recs[i].file, files[i]); make_label(files[i], recs[i].label, 40); recs[i].dur = durs[i]; recs[i].bytes = (long)(durs[i] * 44100); }
    list_sel = 1;
    draw_list_screen(18, 12, 64);
    save("list.ppm");
    confirm_delete = 1;
    draw_list_screen(18, 12, 64);
    save("delete.ppm");
    confirm_delete = 0;
    /* playback */
    play_i = 1; fake.state = PL_PLAYING; fake.pos = 140; fake.dur = 412; fake.level_l = fake.level_r = 0.2f;
    draw_play_screen(18, 12, 64);
    save("play.ppm");
    /* icon + background */
    gfx_vgrad(0, 0, 144, 80, RGB(40, 40, 46), RGB(16, 16, 18));
    ui_draw_cassette(4, 4, 1.36f, 1.15f, 4, "", 'A', 0.4f, 0.2f, 0.9f, 1);
    gfx_circle(128, 14, 7, RGB(30, 10, 10)); gfx_circle(128, 14, 5, COL_RED); gfx_circle_alpha(128, 14, 10, COL_RED, 60);
    { FILE *f = fopen("icon0.ppm", "wb"); fprintf(f, "P6\n144 80\n255\n"); for (int y = 0; y < 80; y++) for (int x = 0; x < 144; x++) { uint32_t c = drawbuf[y * BUF_W + x]; unsigned char p[3] = {C_R(c), C_G(c), C_B(c)}; fwrite(p, 1, 3, f); } fclose(f); }
    blit_bg();
    ui_draw_cassette(150, 60, 3.0f, 3.0f, 4, "Pocket Recorder", 'A', 0.3f, 0.5f, 1.2f, 1);
    gfx_rect_alpha(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 60);
    save("pic1.ppm");
    return 0;
}
