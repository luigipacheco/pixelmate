// SPDX-License-Identifier: Apache-2.0
/* PNG (indexed, 2 bpp, stored deflate) and GIF89a (LZW, looping) encoders for
 * photo records. Output goes to a caller buffer, whole or streamed through a
 * sink; nothing is allocated. */
#include <string.h>
#include "include/app_photo.h"

typedef struct {
    uint8_t *p;
    size_t n, cap;              /* Bytes waiting in p; its size. */
    size_t total;               /* Bytes produced so far. */
    bool overflow;              /* No room left (no sink), or the sink stopped. */
    app_photo_sink_t sink;      /* NULL: p must hold the whole file. */
    void *ctx;
    uint32_t crc;               /* Running CRC of the PNG chunk being written. */
    bool crc_on;
} writer_t;

static uint32_t crc_byte(uint32_t crc, uint8_t b)
{
    crc ^= b;
    for (int k = 0; k < 8; ++k) {
        crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

static void put(writer_t *w, uint8_t b)
{
    if (w->overflow) {
        return;
    }
    if (w->n == w->cap) {
        if (!w->sink || !w->sink(w->ctx, w->p, w->n)) {
            w->overflow = true;
            return;
        }
        w->n = 0;
    }
    w->p[w->n++] = b;
    ++w->total;
    if (w->crc_on) {
        w->crc = crc_byte(w->crc, b);
    }
}

/* Hand the rest to the sink; the byte count, or 0 when anything failed. */
static size_t finish(writer_t *w)
{
    if (!w->overflow && w->sink && w->n) {
        w->overflow = !w->sink(w->ctx, w->p, w->n);
        w->n = 0;
    }
    return w->overflow ? 0 : w->total;
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
    return photo && app_photo_header_valid(&photo->header) && size_ok(app_photo_lens(&photo->header), scale);
}

/* ---- PNG -------------------------------------------------------------- */

enum { DEFLATE_STORED_MAX = 65535 };

/* Filter byte + packed row, per row: 4 pixels per byte (B&W) or 2 (color). */
static size_t png_raw_size(int res, int scale, bool color)
{
    const size_t w = (size_t)app_photo_side(res) * scale;
    return w * (1 + w / (color ? 2U : 4U));
}

size_t app_photo_png_bound(int res, int scale)
{
    if (!size_ok(res, scale)) {
        return 0;
    }
    const size_t raw = png_raw_size(res, scale, true);   /* Color is the larger. */
    const size_t blocks = raw / DEFLATE_STORED_MAX + 1;
    /* signature + IHDR + PLTE + IDAT(zlib header, block headers, adler) + IEND */
    return 8 + 25 + (12 + 3 * APP_PHOTO_COLORS) + 12 + 2 + raw + blocks * 5 + 4 + 12;
}

/* Begin a chunk: the CRC runs over its type and data. */
static void chunk_begin(writer_t *w, const char type[4], uint32_t len)
{
    put_be32(w, len);
    w->crc = 0xFFFFFFFFU;
    w->crc_on = true;
    put_bytes(w, type, 4);
}

static void chunk_end(writer_t *w)
{
    w->crc_on = false;
    put_be32(w, ~w->crc);
}

static uint8_t png_row_byte(const app_photo_t *photo, int scale, int y, int byte_index)
{
    const uint8_t *frame = app_photo_frame_const(photo, 0);
    const int side = app_photo_side(app_photo_lens(&photo->header));
    const bool color = app_photo_is_color(&photo->header);
    const int per_byte = color ? 2 : 4, bits = color ? 4 : 2;
    uint8_t b = 0;
    for (int k = 0; k < per_byte; ++k) {
        const int x = (byte_index * per_byte + k) / scale;
        b = (uint8_t)((b << bits) | app_photo_pixel_at(frame, side, color, x, y / scale));
    }
    return b;
}

static size_t png(const app_photo_t *photo, int scale, writer_t w)
{
    if (!scale_ok(photo, scale) || !w.p || !w.cap) {
        return 0;
    }
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    put_bytes(&w, signature, sizeof(signature));

    const bool color = app_photo_is_color(&photo->header);
    const uint32_t width = (uint32_t)app_photo_side(app_photo_lens(&photo->header)) * scale, height = width;
    chunk_begin(&w, "IHDR", 13);
    put_be32(&w, width);
    put_be32(&w, height);
    put(&w, color ? 4 : 2);   /* bit depth */
    put(&w, 3);   /* indexed color */
    put(&w, 0);
    put(&w, 0);
    put(&w, 0);
    chunk_end(&w);

    app_photo_rgb_t palette[APP_PHOTO_COLORS];
    const int colors = app_photo_header_palette(&photo->header, palette);
    chunk_begin(&w, "PLTE", (uint32_t)(3 * colors));
    for (int i = 0; i < colors; ++i) {
        put(&w, palette[i].r);
        put(&w, palette[i].g);
        put(&w, palette[i].b);
    }
    chunk_end(&w);

    /* zlib stream of stored (uncompressed) deflate blocks: the image is tiny. */
    const size_t raw = png_raw_size(app_photo_lens(&photo->header), scale, color);
    const size_t blocks = (raw + DEFLATE_STORED_MAX - 1) / DEFLATE_STORED_MAX;
    chunk_begin(&w, "IDAT", (uint32_t)(2 + raw + blocks * 5 + 4));
    put(&w, 0x78);
    put(&w, 0x01);
    const size_t row_bytes = width / (color ? 2U : 4U);
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
    chunk_end(&w);
    chunk_begin(&w, "IEND", 0);
    chunk_end(&w);
    return finish(&w);
}

size_t app_photo_encode_png(const app_photo_t *photo, int scale, uint8_t *out, size_t cap)
{
    return png(photo, scale, (writer_t) {.p = out, .cap = cap});
}

size_t app_photo_stream_png(const app_photo_t *photo, int scale, uint8_t *buf, size_t buf_len,
                            app_photo_sink_t sink, void *ctx)
{
    return sink ? png(photo, scale, (writer_t) {.p = buf, .cap = buf_len, .sink = sink, .ctx = ctx}) : 0;
}

/* ---- GIF -------------------------------------------------------------- */

enum {
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
    int min_bits, clear, eoi, first;   /* 2 bits for 4 shades, 4 for 16 colors. */
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
    z->next = z->first;
    z->bits = z->min_bits + 1;
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

static uint8_t gif_pixel(const uint8_t *frame, int side, bool color, int scale, int i)
{
    const int width = side * scale;
    return app_photo_pixel_at(frame, side, color, (i % width) / scale, (i / width) / scale);
}

static void lzw_frame(lzw_t *z, const uint8_t *frame, int side, bool color, int scale)
{
    const int count = side * scale * side * scale;
    put(z->w, (uint8_t)z->min_bits);
    z->acc = 0;
    z->acc_bits = 0;
    z->block_n = 0;
    lzw_reset(z);
    lzw_emit(z, z->clear);
    int prefix = gif_pixel(frame, side, color, scale, 0);
    for (int i = 1; i < count; ++i) {
        const int c = gif_pixel(frame, side, color, scale, i);
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
            lzw_emit(z, z->clear);
            lzw_reset(z);
        }
        prefix = c;
    }
    lzw_emit(z, prefix);
    lzw_emit(z, z->eoi);
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
    return 13 + 3 * APP_PHOTO_COLORS + 19 + (size_t)frames * frame + 1;
}

static size_t gif(const app_photo_t *photo, int scale, int frame_ms, void *work, writer_t w)
{
    if (!scale_ok(photo, scale) || !work || !w.p || !w.cap) {
        return 0;
    }
    const int side = app_photo_side(app_photo_lens(&photo->header));
    const bool color = app_photo_is_color(&photo->header);
    const uint16_t width = (uint16_t)(side * scale), height = width;
    put_bytes(&w, "GIF89a", 6);
    put_le16(&w, width);
    put_le16(&w, height);
    put(&w, color ? 0xB3 : 0x91);   /* global table: 16 entries (4-bit) or 4 (2-bit) */
    put(&w, 0);
    put(&w, 0);
    app_photo_rgb_t palette[APP_PHOTO_COLORS];
    const int colors = app_photo_header_palette(&photo->header, palette);
    for (int i = 0; i < colors; ++i) {
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
    const int min_bits = color ? 4 : 2;
    lzw_t z = {.w = &w, .keys = work, .codes = (uint16_t *)((int32_t *)work + LZW_HASH),
               .min_bits = min_bits, .clear = 1 << min_bits, .eoi = (1 << min_bits) + 1,
               .first = (1 << min_bits) + 2};
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
        lzw_frame(&z, app_photo_frame_const(photo, f), side, color, scale);
    }
    put(&w, 0x3B);
    return finish(&w);
}

size_t app_photo_encode_gif(const app_photo_t *photo, int scale, int frame_ms, void *work,
                            uint8_t *out, size_t cap)
{
    return gif(photo, scale, frame_ms, work, (writer_t) {.p = out, .cap = cap});
}

size_t app_photo_stream_gif(const app_photo_t *photo, int scale, int frame_ms, void *work,
                            uint8_t *buf, size_t buf_len, app_photo_sink_t sink, void *ctx)
{
    return sink ? gif(photo, scale, frame_ms, work,
                      (writer_t) {.p = buf, .cap = buf_len, .sink = sink, .ctx = ctx}) : 0;
}
