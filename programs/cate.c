#include "./lib/myaos.h"

#define COLOR_BG         0x00101820u
#define COLOR_PANEL      0x00203040u
#define COLOR_BORDER     0x006090B0u
#define COLOR_BALL       0x00FF5A5Au
#define COLOR_BALL_GLOW  0x00FFB0B0u
#define COLOR_TEXT       0x00FFFFFFu
#define COLOR_TEXT_BG    0x00203040u
#define COLOR_DEBUG_BG   0x00000000u
#define CATE_KEY_ESC     0x01u

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    int r;
} ball_t;

static int abs_i(int v) {
    return (v < 0) ? -v : v;
}

static void u32_to_text(uint32_t value, char* out, uint32_t out_size) {
    char rev[16];
    uint32_t n = 0;
    uint32_t pos = 0;

    if (!out || out_size == 0) {
        return;
    }

    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }

    out[pos] = '\0';
}

static void append_text(char* dst, uint32_t dst_size, const char* src) {
    uint32_t i = 0;
    uint32_t j = 0;

    if (!dst || !src || dst_size == 0) {
        return;
    }

    while (i + 1 < dst_size && dst[i]) {
        i++;
    }

    while (i + 1 < dst_size && src[j]) {
        dst[i++] = src[j++];
    }

    dst[i] = '\0';
}

static void draw_filled_circle(int cx, int cy, int r, uint32_t color) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;

        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) {
            dx++;
        }

        dx--;

        if (dx >= 0) {
            mya_gfx_line(cx - dx, cy + dy, cx + dx, cy + dy, color);
        }
    }
}

static void draw_ball(const ball_t* ball) {
    draw_filled_circle(ball->x, ball->y, ball->r + 3, COLOR_BALL_GLOW);
    draw_filled_circle(ball->x, ball->y, ball->r, COLOR_BALL);
}

int program_main(int argc, char** argv) {
    myaos_gfx_info_t info;
    ball_t ball;
    uint32_t tick_freq;
    uint64_t fps_last_ticks;
    uint32_t frame_counter = 0;
    uint32_t fps = 0;
    int running = 1;
    int in_graphics = 0;
    int rc = 0;
    (void)argc;
    (void)argv;

    uint32_t last_ascii = 0;
    uint32_t last_action = 0;
    uint32_t last_keycode = 0;

    if (mya_gfx_mode_set(MYAOS_GFX_MODE_GRAPHICS) != 0) {
        mya_putln("failed to switch to graphics mode");
        return 1;
    }
    in_graphics = 1;

    if (mya_gfx_info_get(&info) != 0) {
        mya_putln("gfx_info_get failed");
        rc = 1;
        goto cleanup;
    }

    tick_freq = mya_time_freq();
    if (tick_freq == 0) {
        tick_freq = 100;
    }

    ball.x = (int)(info.width / 2u);
    ball.y = (int)(info.height / 2u);
    ball.vx = 3;
    ball.vy = 2;
    ball.r = 16;

    fps_last_ticks = mya_time_ticks();

    while (running) {
        uint64_t now = mya_time_ticks();
        uint32_t events = 0;
        char line1[128];
        char line2[128];
        char numbuf[32];

        while (mya_event_poll(MYAOS_EVENT_INPUT_KEYBOARD, 1, &events) == 0 &&
               (events & MYAOS_EVENT_INPUT_KEYBOARD)) {
            myaos_input_key_event_t ev;

            while (mya_input_key_event_read(&ev) == 0) {
                last_ascii = ev.ascii;
                last_action = ev.action;
                last_keycode = ev.keycode;

                if (ev.action != MYAOS_INPUT_KEY_EVENT_PRESS &&
                    ev.action != MYAOS_INPUT_KEY_EVENT_REPEAT) {
                    continue;
                }

                if (ev.ascii == 'q' || ev.ascii == 'Q' ||
                    ev.ascii == 27u || ev.keycode == CATE_KEY_ESC) {
                    running = 0;
                }
            }
        }

        ball.x += ball.vx;
        ball.y += ball.vy;

        if (ball.x - ball.r < 8) {
            ball.x = 8 + ball.r;
            ball.vx = abs_i(ball.vx);
        }
        if (ball.y - ball.r < 40) {
            ball.y = 40 + ball.r;
            ball.vy = abs_i(ball.vy);
        }
        if (ball.x + ball.r >= (int)info.width - 8) {
            ball.x = (int)info.width - 8 - ball.r - 1;
            ball.vx = -abs_i(ball.vx);
        }
        if (ball.y + ball.r >= (int)info.height - 8) {
            ball.y = (int)info.height - 8 - ball.r - 1;
            ball.vy = -abs_i(ball.vy);
        }

        frame_counter++;
        if (now - fps_last_ticks >= tick_freq) {
            fps = frame_counter;
            frame_counter = 0;
            fps_last_ticks = now;
        }

        mya_gfx_rect(0, 0, (int32_t)info.width, (int32_t)info.height, COLOR_BG);

        mya_gfx_rect(0, 0, (int32_t)info.width, 32, COLOR_PANEL);
        mya_gfx_frame(0, 0, (int32_t)info.width, 32, COLOR_BORDER);
        mya_gfx_frame(4, 36, (int32_t)info.width - 8, (int32_t)info.height - 40, COLOR_BORDER);

        draw_ball(&ball);

        line1[0] = '\0';
        append_text(line1, sizeof(line1), "MyaOS Graphics Demo | FPS: ");
        u32_to_text(fps, numbuf, sizeof(numbuf));
        append_text(line1, sizeof(line1), numbuf);
        append_text(line1, sizeof(line1), " | Press Q/Esc to quit");

        mya_gfx_text(10, 10, COLOR_TEXT, COLOR_TEXT_BG, line1);

        line2[0] = '\0';
        append_text(line2, sizeof(line2), "last ascii=");
        u32_to_text(last_ascii, numbuf, sizeof(numbuf));
        append_text(line2, sizeof(line2), numbuf);
        append_text(line2, sizeof(line2), " action=");
        u32_to_text(last_action, numbuf, sizeof(numbuf));
        append_text(line2, sizeof(line2), numbuf);
        append_text(line2, sizeof(line2), " keycode=");
        u32_to_text(last_keycode, numbuf, sizeof(numbuf));
        append_text(line2, sizeof(line2), numbuf);

        mya_gfx_text(10, 46, COLOR_TEXT, COLOR_DEBUG_BG, line2);

        mya_gfx_present();
        mya_proc_sleep(1);
    }

cleanup:
    if (in_graphics) {
        (void)mya_gfx_mode_set(MYAOS_GFX_MODE_TEXT);
    }
    mya_console_clear();
    if (rc == 0) {
        mya_putln("graphics demo closed");
    }
    return rc;
}
