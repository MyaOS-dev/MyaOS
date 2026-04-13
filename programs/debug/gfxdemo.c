#include "../lib/myaos.h"
#include <stdint.h>

static void draw_scene(const myaos_gfx_info_t* info) {
    myaos_gfx_object_t objs[8];
    uint32_t cx = info->width / 2u;
    uint32_t cy = info->height / 2u;
    uint32_t panel_w = (info->width > 200u) ? info->width - 120u : info->width;
    uint32_t panel_h = (info->height > 160u) ? info->height - 100u : info->height;

    for (uint32_t i = 0u; i < 8u; i++) {
        objs[i].type = 0u;
        objs[i].x0 = 0;
        objs[i].y0 = 0;
        objs[i].x1 = 0;
        objs[i].y1 = 0;
        objs[i].color = 0u;
        objs[i].color2 = 0u;
        objs[i].text_ptr = 0u;
        objs[i].text_len = 0u;
        objs[i].flags = 0u;
    }

    objs[0].type = MYAOS_GFX_OBJ_CLEAR;
    objs[0].color = 0x00131A2Cu;

    objs[1].type = MYAOS_GFX_OBJ_RECT;
    objs[1].x0 = (int32_t)((info->width - panel_w) / 2u);
    objs[1].y0 = (int32_t)((info->height - panel_h) / 2u);
    objs[1].x1 = (int32_t)panel_w;
    objs[1].y1 = (int32_t)panel_h;
    objs[1].color = 0x001F2A44u;

    objs[2].type = MYAOS_GFX_OBJ_FRAME;
    objs[2].x0 = objs[1].x0;
    objs[2].y0 = objs[1].y0;
    objs[2].x1 = objs[1].x1;
    objs[2].y1 = objs[1].y1;
    objs[2].color = 0x00BFD8FFu;

    objs[3].type = MYAOS_GFX_OBJ_LINE;
    objs[3].x0 = 0;
    objs[3].y0 = 0;
    objs[3].x1 = (int32_t)(info->width - 1u);
    objs[3].y1 = (int32_t)(info->height - 1u);
    objs[3].color = 0x006FA5FFu;

    objs[4].type = MYAOS_GFX_OBJ_LINE;
    objs[4].x0 = (int32_t)(info->width - 1u);
    objs[4].y0 = 0;
    objs[4].x1 = 0;
    objs[4].y1 = (int32_t)(info->height - 1u);
    objs[4].color = 0x006FA5FFu;

    objs[5].type = MYAOS_GFX_OBJ_RECT;
    objs[5].x0 = (int32_t)cx - 80;
    objs[5].y0 = (int32_t)cy - 30;
    objs[5].x1 = 160;
    objs[5].y1 = 60;
    objs[5].color = 0x00344A7Fu;

    objs[6].type = MYAOS_GFX_OBJ_FRAME;
    objs[6].x0 = objs[5].x0;
    objs[6].y0 = objs[5].y0;
    objs[6].x1 = objs[5].x1;
    objs[6].y1 = objs[5].y1;
    objs[6].color = 0x00FFFFFFu;

    (void)mya_gfx_draw_batch(objs, 7u);
    (void)mya_gfx_text((int32_t)cx - 116, (int32_t)cy - 8, 0x00FFFFFFu, 0x00344A7Fu, "MyaOS Graphics ABI ready");
}

int program_main(int argc, char** argv) {
    myaos_gfx_info_t info;
    uint32_t mode = MYAOS_GFX_MODE_TEXT;
    uint8_t restore_text = 1u;
    uint8_t keep_mode = 0u;

    (void)argc;
    if (argv && argc >= 2 && argv[1] && mya_streq(argv[1], "--keep")) {
        keep_mode = 1u;
    }

    if (mya_gfx_mode_get(&mode) != 0) {
        mya_putln("gfxdemo: failed to query mode");
        return 1;
    }
    if (mode == MYAOS_GFX_MODE_GRAPHICS) {
        restore_text = 0u;
    }

    if (mya_gfx_mode_set(MYAOS_GFX_MODE_GRAPHICS) != 0) {
        mya_putln("gfxdemo: failed to enter graphics mode");
        return 1;
    }
    if (mya_gfx_info_get(&info) != 0) {
        mya_putln("gfxdemo: failed to read gfx info");
        if (restore_text && !keep_mode) {
            (void)mya_gfx_mode_set(MYAOS_GFX_MODE_TEXT);
        }
        return 1;
    }

    draw_scene(&info);
    (void)mya_gfx_present();
    mya_proc_sleep(500u);

    if (!keep_mode && restore_text) {
        (void)mya_gfx_mode_set(MYAOS_GFX_MODE_TEXT);
    }

    mya_putln("gfxdemo: done");
    return 0;
}
