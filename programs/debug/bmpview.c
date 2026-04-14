#include "../lib/myaos.h"
#include <stdint.h>

#define BMPVIEW_FILE_MAX (64u * 1024u * 1024u)

typedef struct {
    const uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t src_format;
    uint32_t flags;
} bmp_image_t;

static uint16_t read_le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t read_le32s(const uint8_t* p) {
    return (int32_t)read_le32(p);
}

static int load_file(const char* path, uint8_t** out_buf, uint32_t* out_size) {
    int32_t fd = -1;
    myaos_posix_stat_t st;
    uint8_t* buf = NULL;
    uint64_t offset = 0u;
    int rc = -1;

    if (!path || !out_buf || !out_size) {
        return -1;
    }
    *out_buf = NULL;
    *out_size = 0u;

    if (mya_posix_open(path, MYAOS_POSIX_O_RDONLY, &fd) != 0) {
        return -1;
    }
    if (mya_posix_fstat(fd, &st) != 0) {
        goto cleanup;
    }
    if (st.size == 0u || st.size > BMPVIEW_FILE_MAX) {
        goto cleanup;
    }

    buf = (uint8_t*)mya_mem_map(st.size, MYAOS_MEM_MAP_WRITABLE);
    if (!buf) {
        goto cleanup;
    }

    while (offset < st.size) {
        uint32_t need = (uint32_t)(st.size - offset);
        uint32_t got = 0u;

        if (mya_posix_read(fd, buf + (uint32_t)offset, need, &got) != 0) {
            goto cleanup;
        }
        if (got == 0u) {
            goto cleanup;
        }
        offset += (uint64_t)got;
    }

    *out_buf = buf;
    *out_size = (uint32_t)st.size;
    buf = NULL;
    rc = 0;

cleanup:
    if (buf) {
        (void)mya_mem_unmap(buf);
    }
    if (fd >= 0) {
        (void)mya_posix_close(fd);
    }
    return rc;
}

static int parse_bmp(const uint8_t* data, uint32_t size, bmp_image_t* out) {
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width_s;
    int32_t height_s;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_pp;
    uint64_t row_raw;
    uint32_t stride;
    uint64_t image_bytes;
    uint8_t bottom_up;

    if (!data || !out || size < 54u) {
        return -1;
    }
    if (data[0] != 'B' || data[1] != 'M') {
        return -1;
    }

    pixel_offset = read_le32(data + 10u);
    dib_size = read_le32(data + 14u);
    width_s = read_le32s(data + 18u);
    height_s = read_le32s(data + 22u);
    planes = read_le16(data + 26u);
    bpp = read_le16(data + 28u);
    compression = read_le32(data + 30u);

    if (dib_size < 40u || planes != 1u || compression != 0u) {
        return -1;
    }
    if (width_s <= 0 || height_s == 0 || height_s == (int32_t)0x80000000u) {
        return -1;
    }
    if (bpp != 24u && bpp != 32u) {
        return -1;
    }

    width = (uint32_t)width_s;
    bottom_up = (height_s > 0) ? 1u : 0u;
    height = (uint32_t)(bottom_up ? height_s : -height_s);
    bytes_pp = (uint32_t)(bpp / 8u);

    row_raw = (uint64_t)width * (uint64_t)bytes_pp;
    if (row_raw > 0xFFFFFFFFull) {
        return -1;
    }
    stride = (uint32_t)((row_raw + 3u) & ~3u);
    image_bytes = (uint64_t)stride * (uint64_t)height;
    if (pixel_offset >= size || image_bytes > (uint64_t)(size - pixel_offset)) {
        return -1;
    }

    out->pixels = data + pixel_offset;
    out->width = width;
    out->height = height;
    out->stride = stride;
    out->src_format = (bpp == 24u) ? MYAOS_GFX_SRC_BGR24 : MYAOS_GFX_SRC_BGRA32;
    out->flags = bottom_up ? MYAOS_GFX_BLIT_FLIP_Y : 0u;
    return 0;
}

static int draw_bmp(const bmp_image_t* bmp) {
    myaos_gfx_info_t info;
    myaos_gfx_blit_t blit;
    int32_t dst_x = 0;
    int32_t dst_y = 0;

    if (!bmp) {
        return -1;
    }
    if (mya_gfx_info_get(&info) != 0) {
        return -1;
    }
    if (info.width > bmp->width) {
        dst_x = (int32_t)((info.width - bmp->width) / 2u);
    }
    if (info.height > bmp->height) {
        dst_y = (int32_t)((info.height - bmp->height) / 2u);
    }

    if (mya_gfx_clear(0x00000000u) != 0) {
        return -1;
    }

    blit.dst_x = dst_x;
    blit.dst_y = dst_y;
    blit.width = bmp->width;
    blit.height = bmp->height;
    blit.src_stride = bmp->stride;
    blit.src_format = bmp->src_format;
    blit.src_ptr = (uint64_t)(uintptr_t)bmp->pixels;
    blit.flags = bmp->flags;

    if (mya_gfx_blit(&blit) != 0) {
        return -1;
    }

    (void)mya_gfx_text(8, 8, 0x00FFFFFFu, 0x00000000u, "bmpview: Esc/q to exit");
    if (mya_gfx_present() != 0) {
        return -1;
    }
    return 0;
}

int program_main(int argc, char** argv) {
    uint8_t* file_data = NULL;
    uint32_t file_size = 0u;
    bmp_image_t bmp;
    uint8_t entered_graphics = 0u;
    int rc = 1;

    if (!argv || argc < 2 || !argv[1]) {
        mya_putln("usage: bmpview <file.bmp>");
        return 1;
    }

    if (load_file(argv[1], &file_data, &file_size) != 0) {
        mya_putln("bmpview: failed to read file");
        goto cleanup;
    }
    if (parse_bmp(file_data, file_size, &bmp) != 0) {
        mya_putln("bmpview: unsupported BMP (need uncompressed 24/32-bit)");
        goto cleanup;
    }
    if (mya_gfx_mode_set(MYAOS_GFX_MODE_GRAPHICS) != 0) {
        mya_putln("bmpview: failed to enter graphics mode");
        goto cleanup;
    }
    entered_graphics = 1u;

    if (draw_bmp(&bmp) != 0) {
        mya_putln("bmpview: draw failed");
        goto cleanup;
    }

    for (;;) {
        uint32_t events = 0u;
        int wait_rc = mya_input_wait(50u, &events);

        if (wait_rc != 0 || (events & MYAOS_EVENT_INPUT_KEYBOARD) == 0u) {
            continue;
        }

        for (;;) {
            myaos_input_key_event_t ev;
            int ev_rc = mya_input_key_event_read(&ev);

            if (ev_rc <= 0) {
                break;
            }
            if (ev.action != MYAOS_INPUT_KEY_EVENT_PRESS &&
                ev.action != MYAOS_INPUT_KEY_EVENT_REPEAT) {
                continue;
            }
            if (ev.keycode == 0x01u || ev.ascii == (uint32_t)'q' || ev.ascii == (uint32_t)'Q') {
                goto done_wait;
            }
        }
    }

done_wait:
    rc = 0;

cleanup:
    if (entered_graphics) {
        (void)mya_gfx_mode_set(MYAOS_GFX_MODE_TEXT);
        (void)mya_console_clear();
    }
    if (file_data) {
        (void)mya_mem_unmap(file_data);
    }
    return rc;
}
