/*
 * quadra-desktop — QUADRA-OS framebuffer desktop shell
 *
 * A dependency-free, keyboard-driven desktop for Linux framebuffer devices
 * (/dev/fb0). Splash/wallpaper, top bar, bottom dock, window manager and
 * real interactive apps:
 *
 *   Terminal    — a live shell (fork/exec, scrollable output)
 *   Files       — directory listing with keyboard navigation
 *   Task Manager — live CPU/memory/load readouts from /proc
 *   Settings    — desktop toggles (wallpaper grid, accent, scale)
 *   About       — brand & build info
 *
 * Runs headless (no /dev/fb0) rendering a static PNG snapshot via
 *   ./quadra-desktop --png OUT [STATE]
 *
 * Build:  gcc -O2 -o quadra-desktop quadra-desktop.c -lm
 *
 * All Credits go to : M-Eleven — Mohamed El-Sayed Abdelfatah
 * YouTube: https://www.youtube.com/@M11-Dev    GitHub: https://github.com/M11-Developer
 * Mail: ma7292537@gmail.com                    TikTok: https://www.tiktok.com/@m11_dev
 * WhatsApp: @m11dev    ·    QUADRA-OS is MIT licensed — see LICENSE / CREDITS.md
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/input.h>
#include <math.h>
#include <strings.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* evdev + framebuffer structs (defined here so the binary is self-contained) */
struct fb_bitfield { uint32_t offset, length, msb_right; };
struct fb_var_screeninfo {
    uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
    uint32_t bits_per_pixel, grayscale;
    struct fb_bitfield red, green, blue, transp;
    uint32_t nonstd, activate, height, width, accel_flags;
    uint32_t pixclock, left_margin, right_margin, upper_margin, lower_margin;
    uint32_t hsync_len, vsync_len, sync, vmode, rotate, colorspace;
    uint32_t reserved[4];
};
struct fb_fix_screeninfo {
    uint32_t smem_len; uint32_t type, visual; uint32_t line_length;
    uint16_t mmio_start, mmio_len; uint8_t accel_flags, reserved[2];
    uint16_t reserved1; uint8_t reserved2[4]; uint16_t reserved3;
    uint8_t reserved4[128];
};
#define FBIOGET_VSCREENINFO 0x4600u
#define FBIOGET_FSCREENINFO 0x4602u

#include "font8x8.h"

/* ---------------------------------------------------------------- */
/* Colors                                                            */
/* ---------------------------------------------------------------- */
#define ACCENT      0x65d8ed
#define ACCENT_DK   0x167d9b
#define BG_DESKTOP  0x0b1220
#define BG_WIN      0x18243a
#define BG_WIN_HDR  0x26334b
#define BG_TOP      0x101827
#define BG_DOCK     0x25334a
#define TEXT        0xf3f6ff
#define TEXT_DIM    0xa9b6cd
#define TEXT_FAINT  0x72819c
#define BORDER      0x52627d
#define STATUS_OK   0x43d9a3
#define STATUS_WARN 0xf5bd4f
#define STATUS_ERR  0xff6c6c

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* ---------------------------------------------------------------- */
/* Canvas                                                            */
/* ---------------------------------------------------------------- */
typedef struct {
    int w, h;
    uint32_t *buf;        /* canvas buffer; RGB (lowest byte = B) packed in 32bpp */
    int is_fb;
    /* fb back-end state (only when is_fb) */
    void *fb_mmap;
    size_t fb_size;
    uint32_t line_length;
    int bits_per_pixel;
    struct fb_bitfield red, green, blue;
} Canvas;

/* Virtual 16:9 canvas. Headless always 800x480. */

static void px(Canvas *c, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    c->buf[(size_t)y * c->w + x] = color;
}

static void fill_rect(Canvas *c, int x, int y, int w, int h, uint32_t color) {
    int x1 = x < 0 ? 0 : x, y1 = y < 0 ? 0 : y;
    int x2 = x + w > c->w ? c->w : x + w, y2 = y + h > c->h ? c->h : y + h;
    if (x2 <= x1 || y2 <= y1) return;
    for (int yy = y1; yy < y2; yy++) {
        uint32_t *row = &c->buf[(size_t)yy * c->w + x1];
        for (int xx = x1; xx < x2; xx++) row[xx - x1] = color;
    }
}

static void rounded_rect(Canvas *c, int x, int y, int w, int h, int r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    fill_rect(c, x, y + r, w, h - 2 * r, color);
    fill_rect(c, x + r, y, w - 2 * r, h, color);
    for (int dy = 0; dy < r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        fill_rect(c, x, y + dy, dx, 1, color);
        fill_rect(c, x + w - dx, y + dy, dx, 1, color);
        fill_rect(c, x, y + h - 1 - dy, dx, 1, color);
        fill_rect(c, x + w - dx, y + h - 1 - dy, dx, 1, color);
    }
}

static void rect_border(Canvas *c, int x, int y, int w, int h, uint32_t color) {
    fill_rect(c, x, y, w, 1, color);
    fill_rect(c, x, y + h - 1, w, 1, color);
    fill_rect(c, x, y, 1, h, color);
    fill_rect(c, x + w - 1, y, 1, h, color);
}

static void grad_v(Canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bot) {
    for (int yy = 0; yy < h; yy++) {
        float t = h > 1 ? (float)yy / (float)(h - 1) : 0.0f;
        uint8_t r = (uint8_t)(((top >> 16) & 0xFF) * (1 - t) + ((bot >> 16) & 0xFF) * t);
        uint8_t g = (uint8_t)(((top >> 8) & 0xFF) * (1 - t) + ((bot >> 8) & 0xFF) * t);
        uint8_t b = (uint8_t)((top & 0xFF) * (1 - t) + (bot & 0xFF) * t);
        fill_rect(c, x, y + yy, w, 1, rgb(r, g, b));
    }
}

/* ---------------------------------------------------------------- */
/* Text rendering                                                   */
/* ---------------------------------------------------------------- */
static int text_width(const char *s, int scale) {
    if (!s) return 0;
    int n = 0;
    while (s[n]) n++;
    return n * 9 * scale;
}

static void draw_text(Canvas *c, int x, int y, const char *s, int scale, uint32_t color) {
    if (!s) return;
    int cx = x;
    const unsigned char (*glyph)[8] = (const unsigned char(*)[8])font8x8;
    while (*s) {
        uint8_t ch = (uint8_t)(*s++);
        if (ch < 0x20 || ch >= 0x7F) ch = '?';
        const unsigned char *g = glyph[ch];
        for (int r = 0; r < 8; r++) {
            uint8_t bits = g[r];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++)
                            px(c, cx + col * scale + dx, y + r * scale + dy, color);
                }
            }
        }
        cx += 9 * scale;
    }
}

static void draw_text_center(Canvas *c, int x, int y, int w, const char *s, int scale, uint32_t color) {
    int tw = text_width(s, scale);
    int sx = x + (w - tw) / 2;
    if (sx < x) sx = x;
    draw_text(c, sx, y, s, scale, color);
}

/* ---------------------------------------------------------------- */
/* PNG writer (24-bit RGB, stored zlib)                              */
/* ---------------------------------------------------------------- */
static uint32_t crc_tab[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        crc_tab[i] = c;
    }
}
static uint32_t crc_of(const uint8_t *data, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = crc_tab[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static uint32_t adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) { a = (a + data[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

static int png_write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t lenb[4] = {(uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)(len)};
    fwrite(lenb, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len > 0 && data) fwrite(data, 1, len, f);
    uint8_t *buf = malloc(4 + len);
    if (!buf) return -1;
    memcpy(buf, type, 4);
    if (len > 0) memcpy(buf + 4, data, len);
    uint32_t crc = crc_of(buf, 4 + len);
    free(buf);
    uint8_t crcb[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)(crc)};
    fwrite(crcb, 1, 4, f);
    return 0;
}

static int canvas_write_png(const Canvas *c, const char *path) {
    static int init_done = 0;
    if (!init_done) { crc_init(); init_done = 1; }
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    static const uint8_t sig[8] = {0x89,'P','N','G','\r','\n','\x1a','\n'};
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(c->w >> 24); ihdr[1] = (uint8_t)(c->w >> 16); ihdr[2] = (uint8_t)(c->w >> 8); ihdr[3] = (uint8_t)(c->w);
    ihdr[4] = (uint8_t)(c->h >> 24); ihdr[5] = (uint8_t)(c->h >> 16); ihdr[6] = (uint8_t)(c->h >> 8); ihdr[7] = (uint8_t)(c->h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_write_chunk(f, "IHDR", ihdr, 13);

    size_t stride = (size_t)c->w * 3;
    size_t raw_len = (stride + 1) * (size_t)c->h;
    uint8_t *raw = malloc(raw_len);
    if (!raw) { fclose(f); return -1; }
    size_t p = 0;
    for (int y = 0; y < c->h; y++) {
        raw[p++] = 0;
        for (int x = 0; x < c->w; x++) {
            uint32_t col = c->buf[(size_t)y * c->w + x];
            raw[p++] = (uint8_t)((col >> 16) & 0xFF);
            raw[p++] = (uint8_t)((col >> 8) & 0xFF);
            raw[p++] = (uint8_t)(col & 0xFF);
        }
    }

    uint8_t *raw_z = malloc(raw_len + 256);
    size_t q = 0;
    raw_z[q++] = 0x78; raw_z[q++] = 0x01;
    size_t remaining = raw_len, off = 0;
    while (remaining > 0) {
        size_t block = remaining > 65535 ? 65535 : remaining;
        int is_final = (off + block >= raw_len);
        raw_z[q++] = (uint8_t)(is_final ? 0x01 : 0x00);
        uint16_t blen = (uint16_t)block;
        raw_z[q++] = (uint8_t)(blen & 0xFF); raw_z[q++] = (uint8_t)(blen >> 8);
        uint16_t nlen = (uint16_t)~blen;
        raw_z[q++] = (uint8_t)(nlen & 0xFF); raw_z[q++] = (uint8_t)(nlen >> 8);
        memcpy(raw_z + q, raw + off, block);
        q += block; off += block; remaining -= block;
    }
    uint32_t adler = adler32(raw, raw_len);
    raw_z[q++] = (uint8_t)(adler >> 24); raw_z[q++] = (uint8_t)(adler >> 16);
    raw_z[q++] = (uint8_t)(adler >> 8);  raw_z[q++] = (uint8_t)(adler);

    png_write_chunk(f, "IDAT", raw_z, (uint32_t)q);
    png_write_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(raw_z);
    return 0;
}

/* ---------------------------------------------------------------- */
/* Canvas init                                                       */
/* ---------------------------------------------------------------- */
static Canvas *canvas_headless(int w, int h) {
    Canvas *c = malloc(sizeof(Canvas));
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    c->w = w; c->h = h; c->is_fb = 0;
    c->buf = malloc((size_t)w * h * sizeof(uint32_t));
    if (!c->buf) { free(c); return NULL; }
    memset(c->buf, 0, (size_t)w * h * sizeof(uint32_t));
    return c;
}

static void canvas_free(Canvas *c) {
    if (!c) return;
    if (c->is_fb && c->fb_mmap) munmap(c->fb_mmap, c->fb_size);
    else if (c->buf) free(c->buf);
    free(c);
}

/* ---------------------------------------------------------------- */
/* Framebuffer canvas                                                */
/* ---------------------------------------------------------------- */
static Canvas *canvas_fb_open(void) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) return NULL;

    struct fb_var_screeninfo vsi;
    memset(&vsi, 0, sizeof(vsi));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vsi) != 0) { close(fd); return NULL; }

    struct fb_fix_screeninfo fsi;
    memset(&fsi, 0, sizeof(fsi));
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fsi) != 0) { close(fd); return NULL; }

    if (vsi.bits_per_pixel != 32) { close(fd); return NULL; }

    Canvas *c = malloc(sizeof(Canvas));
    if (!c) { close(fd); return NULL; }
    memset(c, 0, sizeof(*c));
    c->w = (int)vsi.xres; c->h = (int)vsi.yres;
    c->is_fb = 1;
    c->bits_per_pixel = 32;
    c->red = vsi.red; c->green = vsi.green; c->blue = vsi.blue;
    c->line_length = fsi.line_length ? fsi.line_length : (uint32_t)(c->w * 4u);
    c->fb_size = (size_t)fsi.smem_len;
    c->fb_mmap = mmap(NULL, c->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (c->fb_mmap == MAP_FAILED) { free(c); close(fd); return NULL; }
    c->buf = malloc((size_t)c->w * c->h * sizeof(uint32_t));
    if (!c->buf) { munmap(c->fb_mmap, c->fb_size); free(c); close(fd); return NULL; }
    (void)close(fd);
    return c;
}

static void canvas_blit(const Canvas *c) {
    if (!c->is_fb) return;
    uint32_t *dst = (uint32_t *)c->fb_mmap;
    uint32_t line = c->line_length;
    for (int y = 0; y < c->h; y++) {
        uint32_t *row = dst + (size_t)y * (line / 4);
        const uint32_t *src = &c->buf[(size_t)y * c->w];
        for (int x = 0; x < c->w; x++) {
            uint32_t col = src[x];
            uint32_t r = (col >> 16) & 0xFF;
            uint32_t g = (col >> 8) & 0xFF;
            uint32_t b = col & 0xFF;
            uint32_t px = 0;
            uint32_t mask;
            mask = c->red.length >= 32 ? 0xFF : ((1u << c->red.length) - 1u);
            px |= ((uint32_t)r & mask) << c->red.offset;
            mask = c->green.length >= 32 ? 0xFF : ((1u << c->green.length) - 1u);
            px |= ((uint32_t)g & mask) << c->green.offset;
            mask = c->blue.length >= 32 ? 0xFF : ((1u << c->blue.length) - 1u);
            px |= ((uint32_t)b & mask) << c->blue.offset;
            row[x] = px;
        }
    }
}

/* ---------------------------------------------------------------- */
/* Wallpaper                                                         */
/* ---------------------------------------------------------------- */
static int aurora_on = 1;

static void blob(Canvas *c, int cx, int cy, int rad, uint32_t color, float alpha) {
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            if (d > rad) continue;
            float t = 1.0f - d / (float)rad;
            t = powf(t, 1.8f) * alpha;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= c->w || y >= c->h) continue;
            uint32_t col = c->buf[(size_t)y * c->w + x];
            uint8_t r = (uint8_t)(((color >> 16) & 0xFF) * t + ((col >> 16) & 0xFF) * (1 - t));
            uint8_t g = (uint8_t)(((color >> 8) & 0xFF) * t + ((col >> 8) & 0xFF) * (1 - t));
            uint8_t b = (uint8_t)((color & 0xFF) * t + (col & 0xFF) * (1 - t));
            c->buf[(size_t)y * c->w + x] = rgb(r, g, b);
        }
    }
}

static void draw_wallpaper(Canvas *c, int grid) {
    grad_v(c, 0, 0, c->w, c->h, rgb(0x0a, 0x10, 0x24), rgb(0x16, 0x0e, 0x2e));
    if (grid) {
        for (int x = 0; x < c->w; x += 32) fill_rect(c, x, 0, 1, c->h, rgb(0x18, 0x22, 0x38));
        for (int y = 0; y < c->h; y += 32) fill_rect(c, 0, y, c->w, 1, rgb(0x18, 0x22, 0x38));
    }
    if (aurora_on) {
        blob(c, c->w / 5, c->h / 3, c->w / 4, rgb(0x22, 0xd3, 0xee), 0.22f);
        blob(c, c->w * 3 / 4, c->h / 4, c->w / 4, rgb(0xa8, 0x5a, 0xff), 0.18f);
        blob(c, c->w / 2, c->h * 4 / 5, c->w / 4, rgb(0x06, 0x84, 0xbc), 0.17f);
    }
}

/* ---------------------------------------------------------------- */
/* Top bar                                                           */
/* ---------------------------------------------------------------- */
static void draw_quadra_mark(Canvas *c, int x, int y, int size) {
    int gap = size / 9;
    int tile = (size - gap) / 2;
    rounded_rect(c, x, y, tile, tile, tile / 3, 0x22d3ee);
    rounded_rect(c, x + tile + gap, y, tile, tile, tile / 3, 0x6366f1);
    rounded_rect(c, x, y + tile + gap, tile, tile, tile / 3, 0x8b5cf6);
    rounded_rect(c, x + tile + gap, y + tile + gap, tile, tile, tile / 3, 0x172554);
    fill_rect(c, x + size - 2, y + size - 5, 4, 3, 0x22d3ee);
}

static void draw_topbar(Canvas *c, time_t t) {
    fill_rect(c, 0, 0, c->w, 29, BG_TOP);
    fill_rect(c, 0, 0, c->w, 1, 0x65748e);
    fill_rect(c, 0, 28, c->w, 1, 0x3d4c66);

    draw_quadra_mark(c, 11, 6, 16);
    draw_text(c, 34, 10, "QUADRA", 1, TEXT);
    draw_text(c, 96, 10, "File   Edit   View   Window   Help", 1, TEXT_DIM);

    struct tm tmv;
    localtime_r(&t, &tmv);
    char status[64];
    strftime(status, sizeof(status), "x64   %a %b %d   %H:%M", &tmv);
    int sw = text_width(status, 1);
    draw_text(c, c->w - sw - 14, 10, status, 1, TEXT);
}

/* ---------------------------------------------------------------- */
/* Dock                                                              */
/* ---------------------------------------------------------------- */
typedef enum { APP_TERMINAL = 0, APP_FILES, APP_TASKMGR, APP_SETTINGS, APP_ABOUT } AppKind;
static int win_find(AppKind k);
#define NUM_APPS 6
typedef struct { const char *name; uint32_t color; } AppDef;
static const AppDef APPS[NUM_APPS] = {
    { "Terminal",     0x62d8a5 },
    { "Files",        0xffc857 },
    { "Task Manager", 0x4cb7ff },
    { "Settings",     0xb69cff },
    { "About",        0xff84ad },
    { "Start",        0xffffff },
};
static const int DOCK_ORDER[NUM_APPS] = { 5, 1, 0, 2, 3, 4 };
static int dock_h = 72;
static int dock_index = 1;

static void draw_app_icon(Canvas *c, int app, int x, int y, int size) {
    uint32_t base = app == 1 ? 0x564a2a : app == 3 ? 0x3d315f : app == 4 ? 0x432b49 : 0x1d3b50;
    rounded_rect(c, x, y, size, size, 10, base);
    switch (app) {
        case 0: /* Terminal */
            rounded_rect(c, x + 6, y + 7, size - 12, size - 14, 4, 0x101722);
            draw_text(c, x + 11, y + size / 2 - 4, ">_", 1, 0x7ce6bf);
            break;
        case 1: /* Files */
            rounded_rect(c, x + 6, y + 12, size - 12, size - 19, 4, 0xf1b93f);
            rounded_rect(c, x + 8, y + 8, size / 2, 8, 3, 0xffd36d);
            break;
        case 2: /* Task Manager */
            rounded_rect(c, x + 7, y + 7, size - 14, size - 14, 4, 0x11283a);
            fill_rect(c, x + 11, y + size - 14, 4, 7, 0x60d6c5);
            fill_rect(c, x + 18, y + size - 19, 4, 12, 0x64a9ff);
            fill_rect(c, x + 25, y + size - 24, 4, 17, 0xb69cff);
            break;
        case 3: /* Settings */
            rounded_rect(c, x + 8, y + 8, size - 16, size - 16, 9, 0xd1c7ff);
            rounded_rect(c, x + 14, y + 14, size - 28, size - 28, 6, 0x514374);
            fill_rect(c, x + size / 2 - 2, y + 3, 4, 7, 0xd1c7ff);
            fill_rect(c, x + size / 2 - 2, y + size - 10, 4, 7, 0xd1c7ff);
            break;
        case 4: /* About / QUADRA brand */
            draw_quadra_mark(c, x + 7, y + 7, size - 14);
            break;
        case 5: /* Windows-friendly launcher tile */
            rounded_rect(c, x + 8, y + 8, 10, 10, 3, 0x22d3ee);
            rounded_rect(c, x + 21, y + 8, 10, 10, 3, 0x6366f1);
            rounded_rect(c, x + 8, y + 21, 10, 10, 3, 0x8b5cf6);
            rounded_rect(c, x + 21, y + 21, 10, 10, 3, 0xffffff);
            break;
    }
}

static int dock_panel_x(const Canvas *c) {
    int item = 42, gap = 7, pad = 12;
    int width = NUM_APPS * item + (NUM_APPS - 1) * gap + pad * 2;
    return (c->w - width) / 2;
}

static int dock_item_x(const Canvas *c, int position) {
    int panel_x = dock_panel_x(c);
    return panel_x + 12 + position * (42 + 7);
}

static void draw_dock(Canvas *c, int focus) {
    const int item = 42, gap = 7, pad = 12, panel_h = 60;
    int width = NUM_APPS * item + (NUM_APPS - 1) * gap + pad * 2;
    int px = (c->w - width) / 2;
    int py = c->h - panel_h - 12;
    rounded_rect(c, px + 3, py + 4, width, panel_h, 16, 0x080c16);
    rounded_rect(c, px, py, width, panel_h, 16, BG_DOCK);
    rect_border(c, px, py, width, panel_h, 0x65758f);
    fill_rect(c, px + 14, py + 1, width - 28, 1, 0x8290a7);

    for (int p = 0; p < NUM_APPS; p++) {
        int app = DOCK_ORDER[p];
        int x = dock_item_x(c, p);
        int y = py + 8;
        int focused = (app == focus);
        if (focused) {
            rounded_rect(c, x - 2, y - 2, item + 4, item + 4, 12, 0x41516b);
            rect_border(c, x - 2, y - 2, item + 4, item + 4, 0xa3b4cc);
        }
        draw_app_icon(c, app, x, y, item);

        int running = (app < 5 && win_find((AppKind)app) >= 0);
        if (running) rounded_rect(c, x + 18, py + 52, 6, 3, 2, APPS[app].color);

        if (focused) {
            int label_w = text_width(APPS[app].name, 1) + 14;
            int label_x = x + (item - label_w) / 2;
            if (label_x < 3) label_x = 3;
            if (label_x + label_w > c->w - 3) label_x = c->w - label_w - 3;
            rounded_rect(c, label_x, py - 20, label_w, 16, 6, 0x111a2a);
            draw_text_center(c, label_x, py - 16, label_w, APPS[app].name, 1, TEXT);
        }
    }
}

static void dock_move(int delta) {
    int pos = 0;
    for (int i = 0; i < NUM_APPS; i++) if (DOCK_ORDER[i] == dock_index) pos = i;
    pos = (pos + delta + NUM_APPS) % NUM_APPS;
    dock_index = DOCK_ORDER[pos];
}

/* ---------------------------------------------------------------- */
/* Windows                                                           */
/* ---------------------------------------------------------------- */
#define MAX_WINDOWS 5

typedef struct {
    AppKind kind;
    int x, y, w, h;
    int restore_x, restore_y, restore_w, restore_h;
    int open;
    int focused;
    int minimized;
    int maximized;
} Window;
static Window windows[MAX_WINDOWS];
static int mouse_x = 400, mouse_y = 240;
static int mouse_down = 0;
static int mouse_visible = 0;
static int drag_window = -1;
static int welcome_open = 1;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define FILES_MAX_ENTRIES 128
typedef struct { char name[256]; int is_dir; } FileEntry;
static FileEntry file_entries[FILES_MAX_ENTRIES];
static int file_count = 0;
static int files_sel = 0;
static int files_sel_top = 0;
static int files_last_click = -1;
static long long files_last_click_ms = 0;
static char files_path[PATH_MAX] = "/root";
static void files_refresh(void);
static void files_enter_selected(void);

static int win_find(AppKind k) {
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].open && windows[i].kind == k) return i;
    return -1;
}
static int win_focused_idx(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].open && windows[i].focused && !windows[i].minimized) return i;
    return -1;
}

static int file_entry_compare(const void *a, const void *b) {
    const FileEntry *ea = a, *eb = b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return strcasecmp(ea->name, eb->name);
}

static void files_refresh(void) {
    DIR *dir = opendir(files_path);
    if (!dir && strcmp(files_path, "/root") == 0) {
        strcpy(files_path, "/");
        dir = opendir(files_path);
    }
    file_count = 0;
    if (!dir) return;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL && file_count < FILES_MAX_ENTRIES) {
        if (strcmp(de->d_name, ".") == 0) continue;
        FileEntry *entry = &file_entries[file_count];
        snprintf(entry->name, sizeof(entry->name), "%s", de->d_name);
        entry->is_dir = strcmp(de->d_name, "..") == 0;
        if (!entry->is_dir) {
            char full[PATH_MAX];
            int n = snprintf(full, sizeof(full), "%s%s%s", files_path,
                             strcmp(files_path, "/") == 0 ? "" : "/", de->d_name);
            struct stat st;
            if (n > 0 && (size_t)n < sizeof(full) && stat(full, &st) == 0)
                entry->is_dir = S_ISDIR(st.st_mode);
        }
        file_count++;
    }
    closedir(dir);
    qsort(file_entries, (size_t)file_count, sizeof(file_entries[0]), file_entry_compare);
    if (file_count == 0) files_sel = files_sel_top = 0;
    else if (files_sel >= file_count) files_sel = file_count - 1;
}

static void files_enter_selected(void) {
    if (files_sel < 0 || files_sel >= file_count || !file_entries[files_sel].is_dir) return;
    const char *name = file_entries[files_sel].name;
    if (strcmp(name, "..") == 0) {
        if (strcmp(files_path, "/") != 0) {
            size_t len = strlen(files_path);
            while (len > 1 && files_path[len - 1] == '/') files_path[--len] = 0;
            char *slash = strrchr(files_path, '/');
            if (slash == files_path) files_path[1] = 0;
            else if (slash) *slash = 0;
        }
    } else {
        size_t len = strlen(files_path);
        size_t name_len = strlen(name);
        if (len + name_len + 2 < sizeof(files_path)) {
            if (strcmp(files_path, "/") == 0) strncat(files_path, name, sizeof(files_path) - len - 1);
            else {
                strncat(files_path, "/", sizeof(files_path) - len - 1);
                strncat(files_path, name, sizeof(files_path) - strlen(files_path) - 1);
            }
        }
    }
    files_sel = 0;
    files_sel_top = 0;
    files_refresh();
}

static int win_open(AppKind k, Canvas *c) {
    int ex = win_find(k);
    if (ex >= 0) {
        for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = 0;
        windows[ex].focused = 1;
        windows[ex].minimized = 0;
        if (k == APP_FILES) files_refresh();
        return ex;
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].open) {
            for (int j = 0; j < MAX_WINDOWS; j++) windows[j].focused = 0;
            windows[i].open = 1;
            windows[i].kind = k;
            windows[i].focused = 1;
            windows[i].minimized = 0;
            windows[i].maximized = 0;
            int ww = c ? c->w * 3 / 5 : 480;
            int wh = c ? c->h * 3 / 5 : 300;
            int screen_w = c ? c->w : 800;
            windows[i].w = ww;
            windows[i].h = wh;
            windows[i].x = (screen_w - ww) / 2 + (i - 2) * 14;
            windows[i].y = 46 + i * 12;
            if (windows[i].x < 8) windows[i].x = 8;
            if (c && windows[i].x + ww > c->w - 8) windows[i].x = c->w - ww - 8;
            if (k == APP_FILES) files_refresh();
            return i;
        }
    }
    return -1;
}

/* app content */
static void draw_app_content(Canvas *c, int idx, time_t t);

static const char *app_title(AppKind k) {
    switch (k) {
        case APP_TERMINAL:  return "Terminal";
        case APP_FILES:     return "Files";
        case APP_TASKMGR:   return "Task Manager";
        case APP_SETTINGS:  return "Settings";
        case APP_ABOUT:     return "About QUADRA-OS";
    }
    return "Window";
}

static void draw_window(Canvas *c, int idx, time_t t) {
    Window *w = &windows[idx];
    if (!w->open || w->minimized) return;

    /* Floating glass-card treatment with a soft offset shadow. */
    rounded_rect(c, w->x + 5, w->y + 7, w->w, w->h, 13, 0x070b15);
    rounded_rect(c, w->x, w->y, w->w, w->h, 13, BG_WIN);
    rect_border(c, w->x, w->y, w->w, w->h, w->focused ? 0x8bd9ef : BORDER);
    fill_rect(c, w->x + 2, w->y + 1, w->w - 4, 1, 0x65758f);

    rounded_rect(c, w->x + 1, w->y + 1, w->w - 2, 27, 11, BG_WIN_HDR);
    fill_rect(c, w->x + 1, w->y + 20, w->w - 2, 8, BG_WIN_HDR);
    fill_rect(c, w->x + 1, w->y + 27, w->w - 2, 1, 0x3a4963);

    /* Familiar macOS-style traffic lights, all functional with a mouse. */
    rounded_rect(c, w->x + 11, w->y + 9, 9, 9, 5, STATUS_ERR);
    rounded_rect(c, w->x + 26, w->y + 9, 9, 9, 5, STATUS_WARN);
    rounded_rect(c, w->x + 41, w->y + 9, 9, 9, 5, STATUS_OK);
    draw_text_center(c, w->x + 56, w->y + 9, w->w - 112, app_title(w->kind), 1, TEXT);

    draw_app_content(c, idx, t);
}

/* ---------------------------------------------------------------- */
/* Terminal                                                          */
/* ---------------------------------------------------------------- */
#define TERM_LINES 200
#define TERM_LEN 160
typedef struct {
    char lines[TERM_LINES][TERM_LEN];
    int count;
    char cmd[256];
    int cmd_len;
} TermState;
static TermState term = {0};

static void term_push(const char *s) {
    if (term.count >= TERM_LINES) {
        memmove(&term.lines[0], &term.lines[1], (size_t)(TERM_LINES - 1) * TERM_LEN);
        term.count = TERM_LINES - 1;
    }
    snprintf(term.lines[term.count++], TERM_LEN, "%s", s);
}

/* Run a command, capturing stdout lines into the terminal */
static void term_run(const char *cmd, int from_snapshot) {
    if (!cmd || !*cmd) return;
    term_push(cmd);
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
        execvp("/bin/sh", argv);
        _exit(127);
    }
    if (pid < 0) { term_push("(no shell)"); return; }
    int status;
    waitpid(pid, &status, 0);
    if (!from_snapshot) {
        /* best-effort: nothing more to print (output goes to /dev/tty of child;
         * in real boot the user sees it; in headless mode it is discarded) */
    }
}

/* Capture a command's stdout into the terminal using pipe redirection */
static void term_run_capture(const char *cmd) {
    if (!cmd || !*cmd) return;
    term_push(cmd);
    int pipefd[2];
    if (pipe(pipefd) < 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
        execvp("/bin/sh", argv);
        _exit(127);
    }
    close(pipefd[1]);
    char buf[2048];
    ssize_t n;
    int fd = pipefd[0];
    /* read with timeout by non-blocking trick */
    int total = 0;
    while ((n = read(fd, buf + total, sizeof(buf) - 1 - total)) > 0) {
        total += (int)n;
        if (total >= (int)sizeof(buf) - 1) break;
    }
    close(fd);
    int status;
    waitpid(pid, &status, 0);
    buf[total] = 0;
    /* split into lines */
    char *save = NULL;
    char *tok = strtok_r(buf, "\n", &save);
    while (tok) {
        term_push(tok);
        tok = strtok_r(NULL, "\n", &save);
    }
}

static void draw_terminal(Canvas *c, int wi, int xi, int yi, int w, int h) {
    fill_rect(c, xi, yi, w, h, rgb(0x0a, 0x10, 0x1e));
    int max_lines = (h - 34) / 14;
    int start = term.count - max_lines;
    if (start < 0) start = 0;
    int y = yi + 10;
    for (int i = start; i < term.count; i++) {
        uint32_t col = (term.lines[i][0] == '$') ? ACCENT : TEXT_DIM;
        draw_text(c, xi + 12, y, term.lines[i], 1, col);
        y += 14;
    }
    /* prompt line */
    int line_y = yi + h - 18;
    draw_text(c, xi + 12, line_y, "$", 1, ACCENT);
    draw_text(c, xi + 26, line_y, term.cmd, 1, TEXT);
    fill_rect(c, xi + 26 + text_width(term.cmd, 1), line_y + 2, 10 * 1, 12, ACCENT);
}

/* ---------------------------------------------------------------- */
/* Files                                                            */
/* ---------------------------------------------------------------- */
static void draw_files(Canvas *c, int wi, int xi, int yi, int w, int h, time_t t) {
    (void)wi; (void)t;
    fill_rect(c, xi, yi, w, h, rgb(0x10, 0x19, 0x29));
    draw_text(c, xi + 12, yi + 10, "Location:", 1, TEXT_FAINT);
    draw_text(c, xi + 88, yi + 10, files_path, 1, TEXT_DIM);
    fill_rect(c, xi + 10, yi + 28, w - 20, 1, 0x33435d);

    int max_rows = (h - 68) / 22;
    if (max_rows < 1) max_rows = 1;
    for (int row = 0; row < max_rows; row++) {
        int idx = files_sel_top + row;
        if (idx >= file_count) break;
        int y = yi + 40 + row * 22;
        int selected = idx == files_sel;
        if (selected) rounded_rect(c, xi + 7, y - 4, w - 14, 20, 5, 0x34455f);
        draw_text(c, xi + 16, y, file_entries[idx].is_dir ? "[DIR]" : "[FILE]", 1,
                  file_entries[idx].is_dir ? 0xffd36d : 0x8bb9ff);
        draw_text(c, xi + 74, y, file_entries[idx].name, 1, selected ? TEXT : TEXT_DIM);
    }
    if (file_count == 0) draw_text(c, xi + 16, yi + 48, "This folder is empty.", 1, TEXT_DIM);

    char status[96];
    snprintf(status, sizeof(status), "%d items | Up/Down select | Enter opens folders", file_count);
    draw_text(c, xi + 12, yi + h - 18, status, 1, TEXT_FAINT);
}

/* ---------------------------------------------------------------- */
/* Task Manager                                                      */
/* ---------------------------------------------------------------- */
static void draw_taskmgr(Canvas *c, int wi, int xi, int yi, int w, int h, time_t t) {
    (void)t;
    fill_rect(c, xi, yi, w, h, rgb(0x0e, 0x16, 0x26));

    uint64_t mem_total = 0, mem_avail = 0;
    float load[3] = {0, 0, 0};
    long uptime = 0;
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            uint64_t v = 0;
            if (sscanf(line, "MemTotal: %" SCNu64 " kB", &v) == 1) mem_total = v;
            else if (sscanf(line, "MemAvailable: %" SCNu64 " kB", &v) == 1) mem_avail = v;
        }
        fclose(f);
    }
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%f %f %f", &load[0], &load[1], &load[2]) == 3) {}; fclose(f); }
    f = fopen("/proc/uptime", "r");
    if (f) { uint64_t sec = 0; float frac = 0; if (fscanf(f, "%" SCNu64 ".%f", &sec, &frac) == 2) uptime = (long)sec; fclose(f); }

    draw_text(c, xi + 14, yi + 12, "MEMORY", 1, TEXT_FAINT);
    uint64_t used = mem_total - mem_avail;
    uint64_t up_mb = used / 1024, tot_mb = mem_total / 1024;
    draw_text(c, xi + 14, yi + 26, "used / total", 1, TEXT_DIM);
    char mem_line[128];
    snprintf(mem_line, sizeof(mem_line), "%llu MB / %llu MB", (unsigned long long)up_mb, (unsigned long long)tot_mb);
    draw_text(c, xi + 14, yi + 40, mem_line, 1, TEXT);

    int bar_x = xi + 14, bar_w = w - 28, bar_y = yi + 64, bar_h = 22;
    rounded_rect(c, bar_x, bar_y, bar_w, bar_h, 6, 0x1a2338);
    int fill_w = 0;
    if (mem_total > 0) fill_w = (int)(((double)used / (double)mem_total) * bar_w);
    if (fill_w > 2) rounded_rect(c, bar_x, bar_y, fill_w, bar_h, 6, ACCENT);
    rect_border(c, bar_x, bar_y, bar_w, bar_h, BORDER);

    int proc_y = bar_y + 36;
    draw_text(c, xi + 14, proc_y, "LOAD", 1, TEXT_FAINT);
    char load_buf[128];
    snprintf(load_buf, sizeof(load_buf), "1m:%.2f  5m:%.2f  15m:%.2f", load[0], load[1], load[2]);
    draw_text(c, xi + 14, proc_y + 14, load_buf, 1, TEXT);

    char up_buf[64];
    snprintf(up_buf, sizeof(up_buf), "Uptime %lum %lds", uptime / 3600, uptime % 3600);
    draw_text(c, xi + 14, proc_y + 32, up_buf, 1, TEXT_DIM);

    /* CPU bar */
    int cpu_y = proc_y + 52;
    draw_text(c, xi + 14, cpu_y, "CPU", 1, TEXT_FAINT);
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 4;
    int load_pct = (int)(load[0] * 100.0f / (float)cores);
    if (load_pct > 100) load_pct = 100;
    rounded_rect(c, bar_x, cpu_y + 14, bar_w, 16, 5, 0x1a2338);
    int fw = (bar_w * load_pct) / 100;
    if (fw > 2) rounded_rect(c, bar_x, cpu_y + 14, fw, 16, 5, 0x34d399);
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", load_pct);
    draw_text(c, bar_x + bar_w - 46, cpu_y + 14 + 3, pct, 1, TEXT);

    draw_text(c, xi + 14, yi + h - 24, "Live - sampled every render", 1, TEXT_FAINT);
}

/* ---------------------------------------------------------------- */
/* Settings                                                          */
/* ---------------------------------------------------------------- */
static int settings_grid = 0;
static int settings_bright = 100;
static int settings_scale = 3;

static void canvas_scale_marker(Canvas *c, int xi, int yi, int w, int h) {
    (void)w; (void)h;
    draw_text(c, xi + 14, yi + 126, "interface scale", 1, TEXT_DIM);
    for (int i = 0; i < 4; i++) {
        uint32_t col = (i + 1 <= settings_scale) ? rgb(0xa8, 0x8a, 0xff) : 0x1a2338;
        rounded_rect(c, xi + 14 + i * 44, yi + 142, 36, 16, 5, col);
    }
}

static void draw_settings(Canvas *c, int wi, int xi, int yi, int w, int h, time_t t) {
    (void)t;
    fill_rect(c, xi, yi, w, h, rgb(0x0e, 0x16, 0x26));

    draw_text(c, xi + 14, yi + 12, "DESKTOP", 1, TEXT_FAINT);

    draw_text(c, xi + 14, yi + 30, "Wallpaper grid", 1, TEXT);
    rounded_rect(c, xi + w - 74, yi + 26, 46, 22, 11, settings_grid ? ACCENT_DK : 0x1a2338);
    rounded_rect(c, xi + w - 74 + (settings_grid ? 28 : 4), yi + 30, 16, 14, 7, TEXT);

    draw_text(c, xi + 14, yi + 64, "Brightness", 1, TEXT);
    rounded_rect(c, xi + 14, yi + 80, w - 28, 14, 6, 0x1a2338);
    int fw = ((w - 28) * settings_bright / 100);
    if (fw > 2) rounded_rect(c, xi + 14, yi + 80, fw, 14, 6, ACCENT);

    draw_text(c, xi + 14, yi + 112, "Interface scale", 1, TEXT);
    canvas_scale_marker(c, xi, yi, w, h);

    draw_text(c, xi + 14, yi + h - 22, "up/down: scale   -/+: brightness   G: grid", 1, TEXT_FAINT);
}



/* ---------------------------------------------------------------- */
/* About                                                            */
/* ---------------------------------------------------------------- */
static void draw_about(Canvas *c, int wi, int xi, int yi, int w, int h, time_t t) {
    (void)wi; (void)t; (void)h;
    fill_rect(c, xi, yi, w, h, rgb(0x11, 0x1b, 0x2b));

    draw_text_center(c, xi, yi + 10, w, "QUADRA-OS", 2, ACCENT);
    draw_text_center(c, xi, yi + 39, w, "Mac-inspired shell | Linux 7.3 | x86_64", 1, TEXT_DIM);
    draw_text(c, xi + 16, yi + 66, "Credits: M-Eleven", 1, 0x9ce7f2);
    draw_text(c, xi + 16, yi + 84, "Mohamed El-Sayed Abdelfatah", 1, TEXT);
    draw_text(c, xi + 16, yi + 106, "YouTube: youtube.com/@M11-Dev", 1, TEXT_DIM);
    draw_text(c, xi + 16, yi + 124, "GitHub: github.com/M11-Developer", 1, TEXT_DIM);
    draw_text(c, xi + 16, yi + 142, "Mail: ma7292537@gmail.com", 1, TEXT_DIM);
    draw_text(c, xi + 16, yi + 160, "TikTok: @m11_dev  |  WhatsApp: @m11dev", 1, TEXT_DIM);
    draw_text(c, xi + 16, yi + 184, "MIT covers all QUADRA-authored code and art.", 1, TEXT_FAINT);
    draw_text(c, xi + 16, yi + 202, "Kernel and BusyBox keep GPL-2.0 upstream licenses.", 1, TEXT_FAINT);
}

static void draw_app_content(Canvas *c, int idx, time_t t) {
    Window *w = &windows[idx];
    int xi = w->x + 8, yi = w->y + 34;
    int ww = w->w - 16, hh = w->h - 42;
    switch (w->kind) {
        case APP_TERMINAL: draw_terminal(c, idx, xi, yi, ww, hh); break;
        case APP_FILES:    draw_files(c, idx, xi, yi, ww, hh, t); break;
        case APP_TASKMGR:  draw_taskmgr(c, idx, xi, yi, ww, hh, t); break;
        case APP_SETTINGS: draw_settings(c, idx, xi, yi, ww, hh, t); break;
        case APP_ABOUT:    draw_about(c, idx, xi, yi, ww, hh, t); break;
    }
}

/* ---------------------------------------------------------------- */
/* Start menu / app launcher                                        */
/* ---------------------------------------------------------------- */
static int start_open = 0;
static int start_sel = 0;
static char start_query[64] = {0};
static int start_visible[NUM_APPS] = { 0, 1, 2, 3, 4 };
static int start_visible_count = 5;

static int app_matches_query(const char *name, const char *query) {
    if (!query || !query[0]) return 1;
    size_t qlen = strlen(query);
    size_t nlen = strlen(name);
    if (qlen > nlen) return 0;
    for (size_t i = 0; i + qlen <= nlen; i++) {
        size_t j = 0;
        while (j < qlen && tolower((unsigned char)name[i + j]) == tolower((unsigned char)query[j])) j++;
        if (j == qlen) return 1;
    }
    return 0;
}

static void start_rebuild_filter(void) {
    start_visible_count = 0;
    for (int i = 0; i < 5; i++) {
        if (app_matches_query(APPS[i].name, start_query))
            start_visible[start_visible_count++] = i;
    }
    if (start_sel >= start_visible_count) start_sel = start_visible_count ? start_visible_count - 1 : 0;
}

static int start_menu_bounds(const Canvas *c, int *px, int *py, int *pw, int *ph) {
    *pw = c->w * 11 / 20;
    if (*pw < 360) *pw = c->w - 28;
    *ph = c->h * 19 / 30;
    if (*ph < 260) *ph = c->h - 56;
    *px = (c->w - *pw) / 2;
    *py = (c->h - *ph) / 2;
    return 1;
}

static int start_item_bounds(const Canvas *c, int pos, int *x, int *y, int *w, int *h) {
    int px, py, pw, ph;
    start_menu_bounds(c, &px, &py, &pw, &ph);
    (void)ph;
    int gx = px + 24, gy = py + 102;
    int gap = 10, ch = 42;
    int cw = (pw - 48 - gap) / 2;
    *x = gx + (pos % 2) * (cw + gap);
    *y = gy + (pos / 2) * (ch + 8);
    *w = cw; *h = ch;
    return pos >= 0 && pos < start_visible_count;
}

static void draw_start_menu(Canvas *c, int focus) {
    (void)focus;
    int px, py, pw, ph;
    start_menu_bounds(c, &px, &py, &pw, &ph);

    fill_rect(c, 0, 0, c->w, c->h, 0x09111e);
    rounded_rect(c, px + 4, py + 6, pw, ph, 18, 0x050a12);
    rounded_rect(c, px, py, pw, ph, 18, 0x1d2a40);
    rect_border(c, px, py, pw, ph, 0x71829f);
    fill_rect(c, px + 14, py + 1, pw - 28, 1, 0x9aa8bd);

    draw_quadra_mark(c, px + 24, py + 17, 20);
    draw_text(c, px + 53, py + 23, "QUADRA Launcher", 1, TEXT);
    draw_text(c, px + 24, py + 45, "Simple apps, familiar controls", 1, TEXT_DIM);

    rounded_rect(c, px + 24, py + 63, pw - 48, 28, 9, 0x101a2b);
    rect_border(c, px + 24, py + 63, pw - 48, 28, 0x586985);
    draw_text(c, px + 36, py + 73, start_query[0] ? start_query : "Search apps...", 1, start_query[0] ? TEXT : TEXT_FAINT);

    if (start_visible_count == 0) {
        draw_text(c, px + 28, py + 118, "No apps match. Try another search.", 1, TEXT_DIM);
    }
    for (int pos = 0; pos < start_visible_count; pos++) {
        int x, y, w, h;
        start_item_bounds(c, pos, &x, &y, &w, &h);
        int app = start_visible[pos];
        int selected = (pos == start_sel);
        rounded_rect(c, x, y, w, h, 11, selected ? 0x34455f : 0x243249);
        if (selected) rect_border(c, x, y, w, h, APPS[app].color);
        draw_app_icon(c, app, x + 8, y + 5, 32);
        draw_text(c, x + 50, y + 16, APPS[app].name, 1, selected ? TEXT : TEXT_DIM);
    }
    draw_text(c, px + 24, py + ph - 21, "Type to search  |  Enter opens  |  Esc closes", 1, TEXT_FAINT);
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void launcher_show(void) {
    start_open = 1;
    start_query[0] = 0;
    start_sel = 0;
    start_rebuild_filter();
}

static void focus_window(int idx) {
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = 0;
    if (idx >= 0 && windows[idx].open && !windows[idx].minimized)
        windows[idx].focused = 1;
}

static void close_focused_window(void) {
    int idx = win_focused_idx();
    if (idx < 0) return;
    windows[idx].open = 0;
    windows[idx].focused = 0;
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        if (windows[i].open && !windows[i].minimized) {
            windows[i].focused = 1;
            break;
        }
    }
}

static void open_app_from_launcher(int app, Canvas *c) {
    if (app < 0 || app >= 5) return;
    win_open((AppKind)app, c);
    dock_index = app;
    start_open = 0;
}

static void draw_welcome(Canvas *c) {
    int pw = c->w * 3 / 5;
    if (pw < 420) pw = c->w - 32;
    int ph = 218;
    if (ph > c->h - 48) ph = c->h - 48;
    int px = (c->w - pw) / 2;
    int py = (c->h - ph) / 2;

    fill_rect(c, 0, 0, c->w, c->h, 0x101a2b);
    rounded_rect(c, px + 4, py + 6, pw, ph, 18, 0x080d17);
    rounded_rect(c, px, py, pw, ph, 18, 0x24334b);
    rect_border(c, px, py, pw, ph, 0x788aa7);
    draw_quadra_mark(c, px + 24, py + 22, 30);
    draw_text(c, px + 66, py + 25, "Welcome to QUADRA-OS", 1, TEXT);
    draw_text(c, px + 24, py + 67, "Mac-inspired look, Windows-friendly controls.", 1, TEXT_DIM);
    draw_text(c, px + 24, py + 94, "Start or press Win to search for apps.", 1, TEXT);
    draw_text(c, px + 24, py + 116, "Alt+F4 closes a window.", 1, TEXT);
    draw_text(c, px + 24, py + 138, "Ctrl+Shift+Esc opens Task Manager.", 1, TEXT);
    draw_text(c, px + 24, py + 160, "Linux base; other app layers are not bundled.", 1, TEXT_FAINT);
    draw_text(c, px + 24, py + 182, "All Credits: M-Eleven (MIT)", 1, 0x8fb4d8);

    int bw = 126, bh = 30;
    int bx = px + pw - bw - 22, by = py + ph - bh - 16;
    rounded_rect(c, bx, by, bw, bh, 10, 0x147c9a);
    rect_border(c, bx, by, bw, bh, 0x9de7f2);
    draw_text_center(c, bx, by + 11, bw, "Enter desktop", 1, TEXT);
}

static void draw_mouse_pointer(Canvas *c) {
    if (!mouse_visible) return;
    for (int row = 0; row < 14; row++) {
        int width = row / 2 + 1;
        if (width > 8) width = 8;
        for (int col = 0; col < width; col++) {
            uint32_t color = (col == 0 || col == width - 1 || row == 13) ? 0x080b10 : 0xf7f8ff;
            px(c, mouse_x + col, mouse_y + row, color);
        }
    }
    for (int i = 0; i < 5; i++) px(c, mouse_x + 5 + i, mouse_y + 9 + i, 0x080b10);
}

static void mouse_move(Canvas *c, int dx, int dy) {
    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= c->w) mouse_x = c->w - 1;
    if (mouse_y >= c->h) mouse_y = c->h - 1;

    if (mouse_down && drag_window >= 0 && windows[drag_window].open && !windows[drag_window].maximized) {
        Window *w = &windows[drag_window];
        w->x += dx; w->y += dy;
        if (w->x < 0) w->x = 0;
        if (w->x + w->w > c->w) w->x = c->w - w->w;
        if (w->y < 30) w->y = 30;
        if (w->y + w->h > c->h - dock_h) w->y = c->h - dock_h - w->h;
    }
}

static void toggle_maximize(Window *w, Canvas *c) {
    if (!w->maximized) {
        w->restore_x = w->x; w->restore_y = w->y;
        w->restore_w = w->w; w->restore_h = w->h;
        w->x = 10; w->y = 34;
        w->w = c->w - 20; w->h = c->h - dock_h - 48;
        w->maximized = 1;
    } else {
        w->x = w->restore_x; w->y = w->restore_y;
        w->w = w->restore_w; w->h = w->restore_h;
        w->maximized = 0;
    }
}

static void mouse_press(Canvas *c) {
    if (welcome_open) {
        int pw = c->w * 3 / 5;
        if (pw < 420) pw = c->w - 32;
        int ph = c->h - 48 < 218 ? c->h - 48 : 218;
        int px = (c->w - pw) / 2, py = (c->h - ph) / 2;
        int bx = px + pw - 126 - 22, by = py + ph - 30 - 16;
        welcome_open = 0;
        if (point_in_rect(mouse_x, mouse_y, bx, by, 126, 30)) launcher_show();
        return;
    }

    if (start_open) {
        int px, py, pw, ph;
        start_menu_bounds(c, &px, &py, &pw, &ph);
        if (!point_in_rect(mouse_x, mouse_y, px, py, pw, ph)) {
            start_open = 0;
            return;
        }
        for (int pos = 0; pos < start_visible_count; pos++) {
            int x, y, w, h;
            start_item_bounds(c, pos, &x, &y, &w, &h);
            if (point_in_rect(mouse_x, mouse_y, x, y, w, h)) {
                start_sel = pos;
                open_app_from_launcher(start_visible[pos], c);
                return;
            }
        }
        return;
    }

    /* The floating dock keeps a Windows-like Start launcher at the left. */
    int panel_y = c->h - 60 - 12;
    for (int pos = 0; pos < NUM_APPS; pos++) {
        int x = dock_item_x(c, pos);
        int app = DOCK_ORDER[pos];
        if (point_in_rect(mouse_x, mouse_y, x - 2, panel_y + 6, 46, 46)) {
            dock_index = app;
            if (app == 5) launcher_show();
            else win_open((AppKind)app, c);
            return;
        }
    }

    /* Focus the window under the pointer; its traffic lights work like a Mac. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
            Window *w = &windows[i];
            if (!w->open || w->minimized || (!!w->focused != (pass == 0))) continue;
            if (!point_in_rect(mouse_x, mouse_y, w->x, w->y, w->w, w->h)) continue;
            focus_window(i);
            if (mouse_y < w->y + 28) {
                if (point_in_rect(mouse_x, mouse_y, w->x + 8, w->y + 5, 15, 18)) {
                    w->open = 0;
                    w->focused = 0;
                    drag_window = -1;
                } else if (point_in_rect(mouse_x, mouse_y, w->x + 23, w->y + 5, 15, 18)) {
                    w->minimized = 1;
                    w->focused = 0;
                    drag_window = -1;
                } else if (point_in_rect(mouse_x, mouse_y, w->x + 38, w->y + 5, 15, 18)) {
                    toggle_maximize(w, c);
                    drag_window = -1;
                } else {
                    drag_window = i;
                }
            } else if (w->kind == APP_FILES) {
                int list_y = w->y + 34 + 40;
                int row = (mouse_y - list_y) / 22;
                int visible_rows = (w->h - 42 - 68) / 22;
                int idx = files_sel_top + row;
                if (mouse_y >= list_y && row >= 0 && row < visible_rows && idx < file_count) {
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    long long now_ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
                    if (files_last_click == idx && now_ms - files_last_click_ms < 500) {
                        files_sel = idx;
                        files_enter_selected();
                        files_last_click = -1;
                    } else {
                        files_sel = idx;
                        files_last_click = idx;
                        files_last_click_ms = now_ms;
                    }
                }
            }
            return;
        }
    }
}

static void mouse_button(Canvas *c, int pressed) {
    if (pressed) {
        mouse_down = 1;
        mouse_press(c);
    } else {
        mouse_down = 0;
        drag_window = -1;
    }
}

static void apply_brightness(Canvas *c) {
    if (settings_bright >= 100) return;
    int level = settings_bright < 0 ? 0 : settings_bright;
    size_t count = (size_t)c->w * (size_t)c->h;
    for (size_t i = 0; i < count; i++) {
        uint32_t p = c->buf[i];
        uint32_t r = (((p >> 16) & 0xff) * (unsigned)level) / 100;
        uint32_t g = (((p >> 8) & 0xff) * (unsigned)level) / 100;
        uint32_t b = ((p & 0xff) * (unsigned)level) / 100;
        c->buf[i] = (r << 16) | (g << 8) | b;
    }
}

/* ---------------------------------------------------------------- */
/* Render                                                            */
/* ---------------------------------------------------------------- */
static void render(Canvas *c, time_t t, int grid) {
    draw_wallpaper(c, grid);
    draw_topbar(c, t);
    /* Focused window is painted last so clicking it brings it to the front. */
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].open && !windows[i].focused) draw_window(c, i, t);
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].open && windows[i].focused) draw_window(c, i, t);
    draw_dock(c, dock_index);
    if (start_open) draw_start_menu(c, start_sel);
    if (welcome_open) draw_welcome(c);
    apply_brightness(c);
    draw_mouse_pointer(c);
}

/* ---------------------------------------------------------------- */
/* Keyboard + input events                                            */
/* ---------------------------------------------------------------- */
static int keycode_to_char(uint16_t code, int shift) {
    switch (code) {
        case KEY_A: return shift ? 'A' : 'a'; case KEY_B: return shift ? 'B' : 'b';
        case KEY_C: return shift ? 'C' : 'c'; case KEY_D: return shift ? 'D' : 'd';
        case KEY_E: return shift ? 'E' : 'e'; case KEY_F: return shift ? 'F' : 'f';
        case KEY_G: return shift ? 'G' : 'g'; case KEY_H: return shift ? 'H' : 'h';
        case KEY_I: return shift ? 'I' : 'i'; case KEY_J: return shift ? 'J' : 'j';
        case KEY_K: return shift ? 'K' : 'k'; case KEY_L: return shift ? 'L' : 'l';
        case KEY_M: return shift ? 'M' : 'm'; case KEY_N: return shift ? 'N' : 'n';
        case KEY_O: return shift ? 'O' : 'o'; case KEY_P: return shift ? 'P' : 'p';
        case KEY_Q: return shift ? 'Q' : 'q'; case KEY_R: return shift ? 'R' : 'r';
        case KEY_S: return shift ? 'S' : 's'; case KEY_T: return shift ? 'T' : 't';
        case KEY_U: return shift ? 'U' : 'u'; case KEY_V: return shift ? 'V' : 'v';
        case KEY_W: return shift ? 'W' : 'w'; case KEY_X: return shift ? 'X' : 'x';
        case KEY_Y: return shift ? 'Y' : 'y'; case KEY_Z: return shift ? 'Z' : 'z';
        case KEY_1: return shift ? '!' : '1'; case KEY_2: return shift ? '@' : '2';
        case KEY_3: return shift ? '#' : '3'; case KEY_4: return shift ? '$' : '4';
        case KEY_5: return shift ? '%' : '5'; case KEY_6: return shift ? '^' : '6';
        case KEY_7: return shift ? '&' : '7'; case KEY_8: return shift ? '*' : '8';
        case KEY_9: return shift ? '(' : '9'; case KEY_0: return shift ? ')' : '0';
        case KEY_MINUS: return shift ? '_' : '-'; case KEY_EQUAL: return shift ? '+' : '=';
        case KEY_LEFTBRACE: return shift ? '{' : '['; case KEY_RIGHTBRACE: return shift ? '}' : ']';
        case KEY_BACKSLASH: return shift ? '|' : '\\';
        case KEY_SEMICOLON: return shift ? ':' : ';';
        case KEY_APOSTROPHE: return shift ? '"' : '\'';
        case KEY_GRAVE: return shift ? '~' : '`';
        case KEY_COMMA: return shift ? '<' : ',';
        case KEY_DOT: return shift ? '>' : '.';
        case KEY_SLASH: return shift ? '?' : '/';
        case KEY_SPACE: return ' ';
        case KEY_ENTER: return '\n';
        case KEY_BACKSPACE: return '\b';
        case KEY_TAB: return '\t';
        default: return 0;
    }
}

static void cycle_window_focus(void) {
    int cur = win_focused_idx();
    int start = cur < 0 ? 0 : (cur + 1) % MAX_WINDOWS;
    for (int n = 0; n < MAX_WINDOWS; n++) {
        int i = (start + n) % MAX_WINDOWS;
        if (windows[i].open && !windows[i].minimized) {
            focus_window(i);
            return;
        }
    }
}

static void handle_key(Canvas *c, uint16_t code, int shift, int ctrl, int alt, int meta) {
    if (ctrl && shift && code == KEY_ESC) {
        welcome_open = 0;
        start_open = 0;
        win_open(APP_TASKMGR, c);
        dock_index = APP_TASKMGR;
        return;
    }
    if (meta && code == KEY_E) {
        welcome_open = 0;
        start_open = 0;
        win_open(APP_FILES, c);
        dock_index = APP_FILES;
        return;
    }
    if ((alt && code == KEY_F4) || (ctrl && code == KEY_W)) {
        close_focused_window();
        return;
    }
    if (ctrl && alt && code == KEY_T) {
        welcome_open = 0;
        win_open(APP_TERMINAL, c);
        dock_index = APP_TERMINAL;
        return;
    }
    if (code == KEY_LEFTMETA || code == KEY_RIGHTMETA) {
        welcome_open = 0;
        if (start_open) start_open = 0;
        else launcher_show();
        return;
    }
    if (ctrl && code == KEY_ESC) {
        welcome_open = 0;
        if (start_open) start_open = 0;
        else launcher_show();
        return;
    }

    if (welcome_open) {
        if (code == KEY_ESC || code == KEY_ENTER) welcome_open = 0;
        return;
    }

    if (start_open) {
        switch (code) {
            case KEY_ENTER:
                if (start_visible_count > 0 && start_sel < start_visible_count)
                    open_app_from_launcher(start_visible[start_sel], c);
                return;
            case KEY_ESC:
                start_open = 0;
                return;
            case KEY_UP:
                if (start_visible_count > 0) start_sel = (start_sel - 2 + start_visible_count) % start_visible_count;
                return;
            case KEY_DOWN:
                if (start_visible_count > 0) start_sel = (start_sel + 2) % start_visible_count;
                return;
            case KEY_LEFT:
                if (start_visible_count > 0) start_sel = (start_sel - 1 + start_visible_count) % start_visible_count;
                return;
            case KEY_RIGHT:
                if (start_visible_count > 0) start_sel = (start_sel + 1) % start_visible_count;
                return;
            case KEY_BACKSPACE: {
                size_t len = strlen(start_query);
                if (len) start_query[len - 1] = 0;
                start_rebuild_filter();
                return;
            }
            default: {
                int ch = keycode_to_char(code, shift);
                if (ch && ch != '\n' && ch != '\t') {
                    size_t len = strlen(start_query);
                    if (len < sizeof(start_query) - 1) {
                        start_query[len++] = (char)ch;
                        start_query[len] = 0;
                        start_rebuild_filter();
                    }
                }
                return;
            }
        }
    }

    if (code == KEY_ESC && win_focused_idx() >= 0) {
        close_focused_window();
        return;
    }
    if (code == KEY_TAB) {
        cycle_window_focus();
        return;
    }

    int wi = win_focused_idx();
    if (wi >= 0) {
        Window *w = &windows[wi];
        if (w->kind == APP_TERMINAL) {
            int ch = keycode_to_char(code, shift);
            if (ch == '\b') {
                if (term.cmd_len > 0) { term.cmd_len--; term.cmd[term.cmd_len] = 0; }
            } else if (ch == '\n') {
                term.cmd[term.cmd_len] = 0;
                term_run_capture(term.cmd);
                term.cmd_len = 0; term.cmd[0] = 0;
            } else if (ch && ch != '\t' && term.cmd_len < 254) {
                term.cmd[term.cmd_len++] = (char)ch;
                term.cmd[term.cmd_len] = 0;
            }
            return;
        }
        if (w->kind == APP_FILES) {
            if (code == KEY_UP && files_sel > 0) files_sel--;
            if (code == KEY_DOWN && files_sel + 1 < file_count) files_sel++;
            if (files_sel < files_sel_top) files_sel_top = files_sel;
            if (files_sel >= files_sel_top + 8) files_sel_top = files_sel - 7;
            if (code == KEY_ENTER) files_enter_selected();
            if (code == KEY_BACKSPACE && strcmp(files_path, "/") != 0) {
                size_t len = strlen(files_path);
                while (len > 1 && files_path[len - 1] == '/') files_path[--len] = 0;
                char *slash = strrchr(files_path, '/');
                if (slash == files_path) files_path[1] = 0;
                else if (slash) *slash = 0;
                files_sel = files_sel_top = 0;
                files_refresh();
            }
            return;
        }
        if (w->kind == APP_SETTINGS) {
            if (code == KEY_UP && settings_scale > 1) settings_scale--;
            if (code == KEY_DOWN && settings_scale < 4) settings_scale++;
            if (code == KEY_LEFT && settings_bright > 0) settings_bright -= 5;
            if (code == KEY_RIGHT && settings_bright < 100) settings_bright += 5;
            if (code == KEY_G || code == KEY_ENTER) settings_grid = !settings_grid;
            return;
        }
        return;
    }

    /* Global navigation remains usable without a mouse. */
    switch (code) {
        case KEY_ENTER:
            if (dock_index == 5) launcher_show();
            else win_open((AppKind)dock_index, c);
            return;
        case KEY_LEFT: dock_move(-1); return;
        case KEY_RIGHT: dock_move(1); return;
        case KEY_UP: dock_move(-1); return;
        case KEY_DOWN: dock_move(1); return;
        default: return;
    }
}

#define INPUT_BITS(max_bit) (((max_bit) + 1 + (int)(sizeof(unsigned long) * 8) - 1) / (int)(sizeof(unsigned long) * 8))
static int bit_is_set(const unsigned long *bits, unsigned bit) {
    return (bits[bit / (sizeof(unsigned long) * 8)] >> (bit % (sizeof(unsigned long) * 8))) & 1UL;
}

static int keyboard_open(void) {
    for (int i = 0; i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long evbits[INPUT_BITS(EV_MAX)] = {0};
        unsigned long keybits[INPUT_BITS(KEY_MAX)] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0 || !bit_is_set(evbits, EV_KEY) ||
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0 ||
            !bit_is_set(keybits, KEY_A) || !bit_is_set(keybits, KEY_ENTER)) {
            close(fd);
            continue;
        }
        return fd;
    }
    return -1;
}

static int mouse_open(void) {
    for (int i = 0; i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long evbits[INPUT_BITS(EV_MAX)] = {0};
        unsigned long relbits[INPUT_BITS(REL_MAX)] = {0};
        unsigned long keybits[INPUT_BITS(KEY_MAX)] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0 ||
            !bit_is_set(evbits, EV_REL) || !bit_is_set(evbits, EV_KEY) ||
            ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits) < 0 ||
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0 ||
            !bit_is_set(relbits, REL_X) || !bit_is_set(relbits, REL_Y) ||
            !bit_is_set(keybits, BTN_LEFT)) {
            close(fd);
            continue;
        }
        return fd;
    }
    return -1;
}

/* Snapshot mode                                                    */
/* ---------------------------------------------------------------- */
static int run_snapshot(const char *png_path, int state) {
    Canvas *c = canvas_headless(800, 480);
    if (!c) { fprintf(stderr, "canvas init failed\n"); return 1; }
    memset(windows, 0, sizeof(windows));
    welcome_open = 0;
    mouse_visible = 0;
    start_open = 0;
    start_query[0] = 0;
    start_rebuild_filter();
    time_t t = time(NULL);

    if (state == 0) {
        dock_index = APP_TERMINAL;
        win_open(APP_TERMINAL, c);
        term_push("QUADRA-OS terminal - live shell (busybox ash)");
        term_push("$ uname -r");
        term_push("7.3.0");
        term_push("$ uname -m");
        term_push("x86_64");
        term_push("$ cat /etc/os-release");
        term_push("NAME=QUADRA-OS");
    } else if (state == 1) {
        dock_index = 5; start_open = 1; start_sel = 0;
    } else if (state == 2) {
        dock_index = APP_TASKMGR; win_open(APP_TASKMGR, c);
    } else if (state == 3) {
        dock_index = APP_SETTINGS; win_open(APP_SETTINGS, c);
    } else if (state == 4) {
        dock_index = APP_FILES; win_open(APP_FILES, c);
    } else if (state == 5) {
        dock_index = APP_ABOUT; win_open(APP_ABOUT, c);
    } else if (state == 6) {
        welcome_open = 1;
    } else {
        dock_index = APP_FILES;
    }

    render(c, t, settings_grid);
    int rc = canvas_write_png(c, png_path);
    canvas_free(c);
    if (rc == 0) fprintf(stderr, "snapshot -> %s\n", png_path);
    else perror("png");
    return rc;
}

/* ---------------------------------------------------------------- */
/* main                                                              */
/* ---------------------------------------------------------------- */
static void usage(const char *argv0) {
    fprintf(stderr,
        "quadra-desktop — QUADRA-OS native framebuffer desktop\n\n"
        "Usage:\n"
        "  %s                        Run on /dev/fb0\n"
        "  %s --png OUT [STATE]      Render a snapshot\n"
        "                            0=Terminal 1=Launcher 2=TaskMgr\n"
        "                            3=Settings 4=Files 5=About 6=Welcome\n"
        "\nMouse: click dock/apps, drag title bars, use traffic-light controls\n"
        "Keyboard: Windows key opens Launcher; Win+E opens Files\n"
        "  Ctrl+Shift+Esc Task Manager | Alt+F4 close | arrows navigate\n"
        "  Enter open | Esc close | Tab cycle windows | type to search\n",
        argv0, argv0);
}

int main(int argc, char **argv) {
    int snap_mode = 0;
    int state = 0;
    const char *png_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--png") == 0 && i + 1 < argc) {
            snap_mode = 1;
            png_path = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') state = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]); return 0;
        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]); usage(argv[0]); return 2;
        }
    }

    if (snap_mode) return run_snapshot(png_path, state);

    /* ---- framebuffer mode ---- */
    Canvas *c = canvas_fb_open();
    if (!c) {
        fprintf(stderr, "no usable /dev/fb0 — try --png for headless snapshot\n");
        return 1;
    }

    int kb = keyboard_open();
    int mouse = mouse_open();
    if (mouse >= 0) {
        mouse_visible = 1;
        mouse_x = c->w / 2;
        mouse_y = c->h / 2;
    }
    if (kb < 0 && mouse < 0)
        fprintf(stderr, "note: no keyboard or mouse input device; desktop renders static\n");
    else if (kb < 0)
        fprintf(stderr, "note: no keyboard found; mouse input is available\n");
    else if (mouse < 0)
        fprintf(stderr, "note: no mouse found; use keyboard navigation\n");

    int mod_shift = 0, mod_ctrl = 0, mod_alt = 0, mod_meta = 0;
    while (1) {
        time_t t = time(NULL);
        render(c, t, settings_grid);
        canvas_blit(c);

        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        if (kb >= 0) { FD_SET(kb, &rfds); if (kb > maxfd) maxfd = kb; }
        if (mouse >= 0) { FD_SET(mouse, &rfds); if (mouse > maxfd) maxfd = mouse; }
        struct timeval tv = { 0, 50000 };
        int ready = maxfd >= 0 ? select(maxfd + 1, &rfds, NULL, NULL, &tv) : 0;
        if (ready > 0 && kb >= 0 && FD_ISSET(kb, &rfds)) {
            struct input_event ev[16];
            ssize_t n = read(kb, ev, sizeof(ev));
            if (n > 0) {
                size_t count = (size_t)n / sizeof(struct input_event);
                for (size_t i = 0; i < count; i++) {
                    if (ev[i].type != EV_KEY) continue;
                    int down = ev[i].value != 0;
                    if (ev[i].code == KEY_LEFTSHIFT || ev[i].code == KEY_RIGHTSHIFT) { mod_shift = down; continue; }
                    if (ev[i].code == KEY_LEFTCTRL || ev[i].code == KEY_RIGHTCTRL) { mod_ctrl = down; continue; }
                    if (ev[i].code == KEY_LEFTALT || ev[i].code == KEY_RIGHTALT) { mod_alt = down; continue; }
                    if (ev[i].code == KEY_LEFTMETA || ev[i].code == KEY_RIGHTMETA) mod_meta = down;
                    if (ev[i].value == 1)
                        handle_key(c, ev[i].code, mod_shift, mod_ctrl, mod_alt, mod_meta);
                }
            }
        }
        if (ready > 0 && mouse >= 0 && FD_ISSET(mouse, &rfds)) {
            struct input_event ev[16];
            ssize_t n = read(mouse, ev, sizeof(ev));
            if (n > 0) {
                size_t count = (size_t)n / sizeof(struct input_event);
                for (size_t i = 0; i < count; i++) {
                    if (ev[i].type == EV_REL) {
                        if (ev[i].code == REL_X) mouse_move(c, ev[i].value, 0);
                        else if (ev[i].code == REL_Y) mouse_move(c, 0, ev[i].value);
                    } else if (ev[i].type == EV_KEY && ev[i].code == BTN_LEFT && ev[i].value != 2) {
                        mouse_button(c, ev[i].value == 1);
                    }
                }
            }
        }
        if (ready == 0 && maxfd < 0) {
            struct timespec ts = { 0, 100000000 };
            nanosleep(&ts, NULL);
        }
    }

    canvas_free(c);
    if (kb >= 0) close(kb);
    if (mouse >= 0) close(mouse);
    return 0;
}
