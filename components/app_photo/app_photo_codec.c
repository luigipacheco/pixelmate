// SPDX-License-Identifier: Apache-2.0
/* PNG (indexed, 2 bpp, stored deflate) and GIF89a (LZW, looping) encoders for
 * photo records. Output goes to a caller buffer; nothing is allocated. */
#include <string.h>
#include "include/app_photo.h"

typedef struct {
    uint8_t *p;
    size_t n, cap;
    bool overflow;
} writer_t;

static void put(writer_t *w, uint8_t b)
{
    if (w->n < w->cap) {
        w->p[w->n++] = b;
    } else {
        w->overflow = true;
    }
}

static void put_bytes(writer_t *w, const void *data, size_t len)
{
    const uint8_t *b = data;
    for (size_t i = 0; i < len; ++i) {
        put(w, b[i]);
    }
}

static void put_be32(writer_t *w, uint32_t v)
{
    put(w, (uint8_t)(v >> 24));
    put(w, (uint8_t)(v >> 16));
    put(w, (uint8_t)(v >> 8));
    put(w, (uint8_t)v);
}

static void put_le16(writer_t *w, uint16_t v)
{
    put(w, (uint8_t)v);
    put(w, (uint8_t)(v >> 8));
}

static bool size_ok(int res, int scale)
{
    return res >= 0 && res < APP_PHOTO_RES_COUNT && scale >= 1 && app_photo_side(res) * scale <= APP_PHOTO_MAX_OUT;
}

static bool scale_ok(const app_photo_t *photo, int scale)
{
    return photo && app_photo_header_valid(&photo->header) && size_ok(photo->header.res, scale);
}

/* ---- PNG -------------------------------------------------------------- */

enum { DEFLATE_STORED_MAX = 65535 };

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static size_t png_raw_size(int res, int scale)
{
    const size_t w = (size_t)app_photo_side(res) * scale;
    return w * (1 + w / 4);
}

size_t app_photo_png_bound(int res, int scale)
{
    if (!size_ok(res, scale)) {
        return 0;
    }
    const size_t raw = png_raw_size(res, scale);
    const size_t blocks = raw / DEFLATE_STORED_MAX + 1;
    /* signature + IHDR + PLTE + IDAT(zlib header, block headers, adler) + IEND */
    return 8 + 25 + (12 + 3 * APP_PHOTO_SHADES) + 12 + 2 + raw + blocks * 5 + 4 + 12;
}

/* Begin a chunk; returns the offset of its type field for the CRC. */
static size_t chunk_begin(writer_t *w, const char type[4], uint32_t len)
{
    put_be32(w, len);
    const size_t at = w->n;
    put_bytes(w, type, 4);
    return at;
}

static void chunk_end(writer_t *w, size_t type_at)
{
    if (w->overflow) {
        return;
    }
    put_be32(w, crc32_update(0, w->p + type_at, w->n - type_at));
}

static uint8_t png_row_byte(const app_photo_t *photo, int scale, int y, int byte_index)
{
    const uint8_t *frame = app_photo_frame_const(photo, 0);
    const int side = app_photo_side(photo->header.res);
    uint8_t b = 0;
    for (int k = 0; k < 4; ++k) {
        const int x = (byte_index * 4 + k) / scale;
        b = (uint8_t)((b << 2) | app_photo_shade_at(frame, side, x, y / scale));
    }
    return b;
}

size_t app_photo_encode_png(const app_photo_t *photo, int scale, uint8_t *out, size_t cap)
{
    if (!scale_ok(photo, scale) || !out) {
        return 0;
    }
    writer_t w = {out, 0, cap, false};
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    put_bytes(&w, signature, sizeof(signature));

    const uint32_t width = (uint32_t)app_photo_side(photo->header.res) * scale, height = width;
    size_t at = chunk_begin(&w, "IHDR", 13);
    put_be32(&w, width);
    put_be32(&w, height);
    put(&w, 2);   /* bit depth */
    put(&w, 3);   /* indexed color */
    put(&w, 0);
    put(&w, 0);
    put(&w, 0);
    chunk_end(&w, at);

    app_photo_rgb_t palette[APP_PHOTO_SHADES];
    app_photo_palette((app_photo_filter_t)photo->header.filter, photo->header.base, palette);
    at = chunk_begin(&w, "PLTE", 3 * APP_PHOTO_SHADES);
    for (int i = 0; i < APP_PHOTO_SHADES; ++i) {
        put(&w, palette[i].r);
        put(&w, palette[i].g);
        put(&w, palette[i].b);
    }
    chunk_end(&w, at);

    /* zlib stream of stored (uncompressed) deflate blocks: the image is tiny. */
    const size_t raw = png_raw_size(photo->header.res, scale);
    const size_t blocks = (raw + DEFLATE_STORED_MAX - 1) / DEFLATE_STORED_MAX;
    at = chunk_begin(&w, "IDAT", (uint32_t)(2 + raw + blocks * 5 + 4));
    put(&w, 0x78);
    put(&w, 0x01);
    const size_t row_bytes = width / 4;
    uint32_t a = 1, b = 0;   /* Adler-32 */
    size_t done = 0, in_block = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (size_t i = 0; i <= row_bytes; ++i) {
            if (in_block == 0) {
                const size_t len = raw - done < DEFLATE_STORED_MAX ? raw - done : DEFLATE_STORED_MAX;
                put(&w, raw - done <= DEFLATE_STORED_MAX ? 1 : 0);   /* BFINAL, BTYPE=00 */
                put_le16(&w, (uint16_t)len);
                put_le16(&w, (uint16_t)~len);
                in_block = len;
            }
            const uint8_t byte = i == 0 ? 0 : png_row_byte(photo, scale, (int)y, (int)i - 1);
            put(&w, byte);
            a = (a + byte) % 65521U;
            b = (b + a) % 65521U;
            ++done;
            --in_block;
        }
    }
    put_be32(&w, (b << 16) | a);
    chunk_end(&w, at);
    at = chunk_begin(&w, "IEND", 0);
    chunk_end(&w, at);
    return w.overflow ? 0 : w.n;
}

/* ---- GIF -------------------------------------------------------------- */

enum {
    LZW_MIN_BITS = 2,               /* 4 colors. */
    LZW_CLEAR = 1 << LZW_MIN_BITS,
    LZW_EOI = LZW_CLEAR + 1,
    LZW_FIRST = LZW_CLEAR + 2,
    LZW_MAX_BITS = 12,
    LZW_LIMIT = (1 << LZW_MAX_BITS) - 1,
    LZW_HASH = 5003,                /* Prime > 4096, the classic table size. */
    GIF_SUBBLOCK = 255,
};

typedef struct {
    writer_t *w;
    int32_t *keys;                  /* (prefix << 8 | pixel), -1 = empty */
    uint16_t *codes;
    int next, bits;
    uint32_t acc;
    int acc_bits;
    uint8_t block[GIF_SUBBLOCK];
    int block_n;
} lzw_t;

static void lzw_flush_block(lzw_t *z)
{
    if (z->block_n) {
        put(z->w, (uint8_t)z->block_n);
        put_bytes(z->w, z->block, (size_t)z->block_n);
        z->block_n = 0;
    }
}

static void lzw_byte(lzw_t *z, uint8_t b)
{
    z->block[z->block_n++] = b;
    if (z->block_n == GIF_SUBBLOCK) {
        lzw_flush_block(z);
    }
}

static void lzw_reset(lzw_t *z)
{
    for (int i = 0; i < LZW_HASH; ++i) {
        z->keys[i] = -1;
    }
    z->next = LZW_FIRST;
    z->bits = LZW_MIN_BITS + 1;
}

/* Emit with the current width; the decoder lags one table entry behind, so
 * the width grows once the entries added before this code fill it. */
static void lzw_emit(lzw_t *z, int code)
{
    z->acc |= (uint32_t)code << z->acc_bits;
    z->acc_bits += z->bits;
    while (z->acc_bits >= 8) {
        lzw_byte(z, (uint8_t)z->acc);
        z->acc >>= 8;
        z->acc_bits -= 8;
    }
    if (z->next >= (1 << z->bits) && z->bits < LZW_MAX_BITS) {
        ++z->bits;
    }
}

static uint8_t gif_pixel(const uint8_t *frame, int side, int scale, int i)
{
    const int width = side * scale;
    return app_photo_shade_at(frame, side, (i % width) / scale, (i / width) / scale);
}

static void lzw_frame(lzw_t *z, const uint8_t *frame, int side, int scale)
{
    const int count = side * scale * side * scale;
    put(z->w, LZW_MIN_BITS);
    z->acc = 0;
    z->acc_bits = 0;
    z->block_n = 0;
    lzw_reset(z);
    lzw_emit(z, LZW_CLEAR);
    int prefix = gif_pixel(frame, side, scale, 0);
    for (int i = 1; i < count; ++i) {
        const int c = gif_pixel(frame, side, scale, i);
        const int32_t key = (int32_t)((prefix << 8) | c);
        int h = (int)(((uint32_t)c << 4 ^ (uint32_t)prefix) % LZW_HASH);
        while (z->keys[h] != -1 && z->keys[h] != key) {
            h = h + 1 < LZW_HASH ? h + 1 : 0;
        }
        if (z->keys[h] == key) {
            prefix = z->codes[h];
            continue;
        }
        lzw_emit(z, prefix);
        if (z->next < LZW_LIMIT) {
            z->keys[h] = key;
            z->codes[h] = (uint16_t)z->next++;
        } else {
            lzw_emit(z, LZW_CLEAR);
            lzw_reset(z);
        }
        prefix = c;
    }
    lzw_emit(z, prefix);
    lzw_emit(z, LZW_EOI);
    if (z->acc_bits > 0) {
        lzw_byte(z, (uint8_t)z->acc);
    }
    lzw_flush_block(z);
    put(z->w, 0);   /* block terminator */
}

size_t app_photo_gif_bound(int res, int frames, int scale)
{
    if (frames < 1 || frames > APP_PHOTO_GIF_FRAMES || !size_ok(res, scale)) {
        return 0;
    }
    /* Worst case one 12-bit code per pixel, plus sub-block lengths and headers. */
    const size_t side = (size_t)app_photo_side(res) * scale;
    const size_t px = side * side;
    const size_t lzw = px * 3 / 2 + 8;
    const size_t frame = 8 + 10 + 1 + lzw + lzw / GIF_SUBBLOCK + 2;
    return 13 + 3 * APP_PHOTO_SHADES + 19 + (size_t)frames * frame + 1;
}

size_t app_photo_encode_gif(const app_photo_t *photo, int scale, int frame_ms, void *work,
                            uint8_t *out, size_t cap)
{
    if (!scale_ok(photo, scale) || !work || !out) {
        return 0;
    }
    writer_t w = {out, 0, cap, false};
    const int side = app_photo_side(photo->header.res);
    const uint16_t width = (uint16_t)(side * scale), height = width;
    put_bytes(&w, "GIF89a", 6);
    put_le16(&w, width);
    put_le16(&w, height);
    put(&w, 0x91);   /* global table, 2-bit color resolution, 4 entries */
    put(&w, 0);
    put(&w, 0);
    app_photo_rgb_t palette[APP_PHOTO_SHADES];
    app_photo_palette((app_photo_filter_t)photo->header.filter, photo->header.base, palette);
    for (int i = 0; i < APP_PHOTO_SHADES; ++i) {
        put(&w, palette[i].r);
        put(&w, palette[i].g);
        put(&w, palette[i].b);
    }
    const int frames = photo->header.frames;
    if (frames > 1) {
        static const uint8_t loop[19] = {
            0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0, 0, 0
        };
        put_bytes(&w, loop, sizeof(loop));
    }
    lzw_t z = {.w = &w, .keys = work, .codes = (uint16_t *)((int32_t *)work + LZW_HASH)};
    const uint16_t delay = (uint16_t)(frame_ms > 0 ? (frame_ms + 5) / 10 : 0);
    for (int f = 0; f < frames; ++f) {
        static const uint8_t gce_head[3] = {0x21, 0xF9, 0x04};
        put_bytes(&w, gce_head, sizeof(gce_head));
        put(&w, 0x04);   /* disposal: leave in place */
        put_le16(&w, delay);
        put(&w, 0);
        put(&w, 0);
        put(&w, 0x2C);
        put_le16(&w, 0);
        put_le16(&w, 0);
        put_le16(&w, width);
        put_le16(&w, height);
        put(&w, 0);
        lzw_frame(&z, app_photo_frame_const(photo, f), side, scale);
    }
    put(&w, 0x3B);
    return w.overflow ? 0 : w.n;
}
