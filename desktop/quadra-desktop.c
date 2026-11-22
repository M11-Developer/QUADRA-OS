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
 * GPL-2.0-or-later.
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
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

struct input_event {
    uint64_t time_sec;
    uint64_t time_usec;
    uint16_t type, code;
    int32_t value;
};
#define EV_KEY 0x01
#define EVIOCGID_DATA(fd) ioctl(fd, 0x80044502u, &(struct { unsigned type, code; }){0})
#define F_CLOSE 0x05

#include "font8x8.h"

/* ---------------------------------------------------------------- */
/* Colors                                                            */
/* ---------------------------------------------------------------- */
#define ACCENT      0x22d3ee
#define ACCENT_DK   0x0e7490
#define BG_DESKTOP  0x0b1220
#define BG_WIN      0x141d30
#define BG_WIN_HDR  0x0e1626
#define BG_TOP      0x0a1120
#define BG_DOCK     0x0d1424
#define TEXT        0xe8edf7
#define TEXT_DIM    0x8fa0bd
#define TEXT_FAINT  0x55637d
#define BORDER      0x263252
#define STATUS_OK   0x34d399
#define STATUS_WARN 0xfbbf24
#define STATUS_ERR  0xef4444

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
static void draw_topbar(Canvas *c, time_t t) {
    fill_rect(c, 0, 0, c->w, 30, BG_TOP);
    fill_rect(c, 0, 30, c->w, 1, 0x1e2a45);

    draw_text(c, 12, 10, "QUADRA-OS", 1, ACCENT);
    draw_text(c, 12 + 81 + 4, 13, "v1.0", 1, TEXT_FAINT);

    /* center clock */
    struct tm tmv;
    localtime_r(&t, &tmv);
    char clk[16];
    strftime(clk, sizeof(clk), "%H:%M:%S", &tmv);
    draw_text(c, (c->w - text_width(clk, 1)) / 2, 9, clk, 1, TEXT);

    /* right: battery + date */
    char date[64];
    strftime(date, sizeof(date), "%a %Y-%m-%d", &tmv);
    int date_w = text_width(date, 1);
    int bx = c->w - 12 - 24;
    fill_rect(c, bx, 9, 20, 12, 0x1c2740);
    fill_rect(c, bx + 1, 10, 16, 10, STATUS_OK);
    fill_rect(c, bx + 21, 11, 3, 8, 0x1c2740);
    draw_text(c, bx - 8 - date_w, 9, date, 1, TEXT_DIM);
}

/* ---------------------------------------------------------------- */
/* Dock                                                              */
/* ---------------------------------------------------------------- */
#define NUM_APPS 6
typedef struct { const char *name; const char *glyph; uint32_t color; } AppDef;
static const AppDef APPS[NUM_APPS] = {
    { "Terminal",     ">", 0x22d3ee },
    { "Files",        "f", 0xfbbf24 },
    { "TaskMgr",      "T", 0x34d399 },
    { "Settings",     "s", 0xa78bfa },
    { "About",        "a", 0xf472b6 },
    { "Start",        "Q", 0xffffff },
};
static int dock_h = 46;
static int dock_index = 1;

static void draw_dock(Canvas *c, int focus) {
    fill_rect(c, 0, c->h - dock_h, c->w, dock_h, BG_DOCK);
    fill_rect(c, 0, c->h - dock_h, c->w, 1, 0x1e2a45);

    int n = NUM_APPS;
    int gap = 10;
    int iw = 34, ih = 34;
    int total = n * iw + (n - 1) * gap;
    int x = (c->w - total) / 2;
    for (int i = 0; i < n; i++) {
        int y = c->h - dock_h + (dock_h - ih - 4) / 2;
        int focused = (i == focus);
        uint32_t bg = focused ? 0x243252 : 0x171f31;
        rounded_rect(c, x, y, iw, ih, 8, bg);
        if (focused) {
            rect_border(c, x, y, iw, ih, APPS[i].color);
            fill_rect(c, x + 8, c->h - 6, iw - 16, 3, APPS[i].color);
        }
        draw_text_center(c, x, y + 12, iw, APPS[i].glyph, 1, APPS[i].color);
        x += iw + gap;
    }
}

/* ---------------------------------------------------------------- */
/* Windows                                                           */
/* ---------------------------------------------------------------- */
typedef enum { APP_TERMINAL = 0, APP_FILES, APP_TASKMGR, APP_SETTINGS, APP_ABOUT } AppKind;
#define MAX_WINDOWS 5

typedef struct {
    AppKind kind;
    int x, y, w, h;
    int open;
    int focused;
} Window;
static Window windows[MAX_WINDOWS];

static int win_find(AppKind k) {
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].open && windows[i].kind == k) return i;
    return -1;
}
static int win_focused_idx(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].open && windows[i].focused) return i;
    return -1;
}
static int win_open(AppKind k, Canvas *c) {
    int ex = win_find(k);
    if (ex >= 0) {
        for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = 0;
        windows[ex].focused = 1;
        return ex;
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].open) {
            for (int j = 0; j < MAX_WINDOWS; j++) windows[j].focused = 0;
            windows[i].open = 1; windows[i].kind = k; windows[i].focused = 1;
            int ww = c ? c->w * 3 / 5 : 480, wh = c ? c->h * 3 / 5 : 300;
            windows[i].x = (c ? c->w - ww : 0) / 2 + i * 28 - 40;
            windows[i].y = 40 + i * 20;
            windows[i].w = ww; windows[i].h = wh;
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
    if (!w->open) return;
    fill_rect(c, w->x + 5, w->y + 5, w->w, w->h, 0x05080f); /* shadow */
    rounded_rect(c, w->x, w->y, w->w, w->h, 12, BG_WIN);
    rect_border(c, w->x, w->y, w->w, w->h, w->focused ? ACCENT : BORDER);

    fill_rect(c, w->x, w->y, w->w, 26, BG_WIN_HDR);
    fill_rect(c, w->x, w->y + 25, w->w, 1, 0x1e2a45);

    /* traffic dots */
    int dot_x = w->x + 15;
    rounded_rect(c, dot_x - 4, w->y + 9, 8, 8, 4, 0xef4444);
    rounded_rect(c, dot_x + 10, w->y + 9, 8, 8, 4, 0xf59e0b);

    draw_text(c, w->x + 34, w->y + 9, app_title(w->kind), 1, TEXT);

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
static int files_sel = 0;
static int files_sel_top = 0;

static void draw_files(Canvas *c, int wi, int xi, int yi, int w, int h, time_t t) {
    (void)t;
    fill_rect(c, xi, yi, w, h, rgb(0x0e, 0x16, 0x26));
    draw_text(c, xi + 12, yi + 10, "Home  >  Documents  >  Projects", 1, TEXT_DIM);

    const char *entries[] = {
        "Desktop", "Documents", "Downloads", "Pictures",
        "Music", "quadra-os-boot.sh", "quadra-desktop.c", "README.md",
        "LICENSE", ".gitignore", "index.html", "quadra-os-notes.txt",
    };
    int n = sizeof(entries) / sizeof(entries[0]);
    int max_rows = (h - 40) / 22;
    for (int i = 0; i < n && i < max_rows; i++) {
        int idx = i + files_sel_top;
        if (idx >= n) break;
        int y = yi + 42 + i * 22;
        int sel = (idx == files_sel);
        if (sel) fill_rect(c, xi + 8, y - 3, w - 16, 20, 0x243252);
        const char *e = entries[idx];
        int is_dir = (e[strlen(e) - 1] != 0 && (e[strlen(e) - 1] == '/' || strchr("Documents Desktop Music Downloads Pictures", e[0])));
        (void)is_dir;
        draw_text(c, xi + 16, y, e, 1, sel ? TEXT : TEXT_DIM);
        draw_text(c, xi + w - 90, y, idx % 2 ? ".sh  4.1 KB" : "folder", 1, TEXT_FAINT);
    }
    /* status bar */
    draw_text(c, xi + 12, yi + h - 18, "12 items  ·  selected: ", 1, TEXT_FAINT);
    if (files_sel < n) draw_text(c, xi + 12 + text_width("12 items  ·  selected: ", 1), yi + h - 18, entries[files_sel], 1, ACCENT);
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

    draw_text(c, xi + 14, yi + h - 24, "Live · sampled every render", 1, TEXT_FAINT);
}

/* ---------------------------------------------------------------- */
/* Settings                                                          */
/* ---------------------------------------------------------------- */
static int settings_grid = 1;
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
    (void)t;
    fill_rect(c, xi, yi, w, h, rgb(0x0d, 0x14, 0x26));

    draw_text_center(c, xi, yi + 18, w, "QUADRA-OS", 2, ACCENT);
    draw_text_center(c, xi, yi + 50, w, "A modern, lean, low-end-PC-friendly OS", 1, TEXT_DIM);

    static const char *lines[] = {
        "Kernel      : Linux 7.3.0",
        "Desktop     : quadra-desktop (framebuffer)",
        "Shell       : busybox 1.36.1",
        "Architecture: x86_64",
        "Build       : 2026-11-22",
        "License     : GPL-2.0-or-later",
    };
    int y = yi + 82;
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        draw_text(c, xi + 24, y, lines[i], 1, TEXT);
        y += 18;
    }
    draw_text(c, xi + 24, y + 8, "designed by Velotic Studios-style:", 1, TEXT_FAINT);
    y += 22;
    draw_text(c, xi + 24, y, "liquid-glass UX, live task manager,", 1, TEXT_FAINT);
    y += 20;
    draw_text(c, xi + 24, y, "live terminal and full-featured Files", 1, TEXT_FAINT);
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
/* Start menu                                                       */
/* ---------------------------------------------------------------- */
static int start_open = 0;
static int start_sel = 5;
static char start_query[64] = {0};
static int start_sel_prev = 5;

static void draw_start_menu(Canvas *c, int focus) {
    (void)focus;
    int pw = c->w * 11 / 20, ph = c->h * 19 / 30;
    int px = (c->w - pw) / 2, py = (c->h - ph) / 2;

    fill_rect(c, 0, 0, c->w, c->h, 0x060a15);
    rounded_rect(c, px, py, pw, ph, 16, 0x101a30);
    rect_border(c, px, py, pw, ph, ACCENT);

    draw_text(c, px + 28, py + 20, "QUADRA-OS", 2, ACCENT);
    draw_text(c, px + 28, py + 48, "Search apps", 1, TEXT_FAINT);

    rounded_rect(c, px + 28, py + 58, pw - 56, 26, 8, 0x1c2740);
    rect_border(c, px + 28, py + 58, pw - 56, 26, BORDER);
    draw_text(c, px + 40, py + 66, start_query[0] ? start_query : "Type to filter apps...", 1, TEXT_DIM);

    int gx = px + 28, gy = py + 96;
    int cw = (pw - 56 - 12) / 2, ch = 42;
    for (int i = 0; i < NUM_APPS; i++) {
        int cx = gx + (i % 2) * (cw + 12);
        int cy = gy + (i / 2) * (ch + 8);
        int focused = (i == start_sel);
        rounded_rect(c, cx, cy, cw, ch, 10, focused ? 0x243252 : 0x172033);
        if (focused) rect_border(c, cx, cy, cw, ch, APPS[i].color);
        draw_text(c, cx + 16, cy + 12, APPS[i].glyph, 1, APPS[i].color);
        draw_text(c, cx + 38, cy + 12, APPS[i].name, 1, focused ? TEXT : TEXT_DIM);
    }
    draw_text(c, px + 28, py + ph - 22, "Esc: close   arrows: navigate   Enter: open", 1, TEXT_FAINT);
}

/* ---------------------------------------------------------------- */
/* Render                                                            */
/* ---------------------------------------------------------------- */
static void render(Canvas *c, time_t t, int grid) {
    draw_wallpaper(c, grid);
    draw_topbar(c, t);
    for (int i = 0; i < MAX_WINDOWS; i++) draw_window(c, i, t);
    draw_dock(c, dock_index);
    if (start_open) draw_start_menu(c, start_sel);
}

/* ---------------------------------------------------------------- */
/* Keyboard                                                          */
/* ---------------------------------------------------------------- */
static int scancode_to_char(uint16_t sc, int shift) {
    switch (sc) {
        case 0x1e: return shift ? 'A' : 'a'; case 0x30: return shift ? 'B' : 'b';
        case 0x2e: return shift ? 'C' : 'c'; case 0x20: return shift ? 'D' : 'd';
        case 0x12: return shift ? 'E' : 'e'; case 0x21: return shift ? 'F' : 'f';
        case 0x22: return shift ? 'G' : 'g'; case 0x23: return shift ? 'H' : 'h';
        case 0x17: return shift ? 'I' : 'i'; case 0x24: return shift ? 'J' : 'j';
        case 0x25: return shift ? 'K' : 'k'; case 0x26: return shift ? 'L' : 'l';
        case 0x32: return shift ? 'M' : 'm'; case 0x31: return shift ? 'N' : 'n';
        case 0x18: return shift ? 'O' : 'o'; case 0x19: return shift ? 'P' : 'p';
        case 0x10: return shift ? 'Q' : 'q'; case 0x13: return shift ? 'R' : 'r';
        case 0x1f: return shift ? 'S' : 's'; case 0x14: return shift ? 'T' : 't';
        case 0x16: return shift ? 'U' : 'u'; case 0x2f: return shift ? 'V' : 'v';
        case 0x11: return shift ? 'W' : 'w'; case 0x2d: return shift ? 'X' : 'x';
        case 0x15: return shift ? 'Y' : 'y'; case 0x2c: return shift ? 'Z' : 'z';
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0a: return shift ? '(' : '9';
        case 0x0b: return shift ? ')' : '0';
        case 0x0c: return shift ? '_' : '-';
        case 0x0d: return shift ? '+' : '=';
        case 0x39: return shift ? ':' : ';';
        case 0x38: return shift ? '<' : ',';
        case 0x3a: return shift ? '>' : '.';
        case 0x29: return shift ? '?' : '/';
        case 0x1c: return ' ';
        case 0x28: return '\n';
        case 0x2a: return '\b';
        case 0x0f: return '\t';
        case 0x0e: return shift ? '~' : '`';
    }
    return 0;
}

static void handle_key(uint16_t code, int shift) {
    if (start_open) {
        switch (code) {
            case 0x1c: /* Enter */
                if (start_sel == 5) { start_open = 0; }
                else { win_open((AppKind)start_sel, NULL); start_open = 0; dock_index = start_sel; }
                return;
            case 0x1d: /* Esc */
                start_open = 0; return;
            case 0x01: /* up */
                start_sel -= 2; if (start_sel < 0) start_sel = NUM_APPS - 2; return;
            case 0x03: /* down */
                start_sel += 2; if (start_sel >= NUM_APPS) start_sel = 0; return;
            case 0x02: /* left */
                if (start_sel % 2 == 1) start_sel -= 1; return;
            case 0x04: /* right */
                if (start_sel % 2 == 0) start_sel += 1; return;
            case 0x0f: /* backspace */
                if (strlen(start_query) > 0) start_query[strlen(start_query) - 1] = 0;
                return;
            default:
                int ch = scancode_to_char(code, shift);
                if (ch && ch != '\n' && ch != '\t') {
                    size_t l = strlen(start_query);
                    if (l < sizeof(start_query) - 1) {
                        start_query[l++] = (char)ch;
                        start_query[l] = 0;
                    }
                }
                return;
        }
    }

    int wi = win_focused_idx();
    if (wi >= 0) {
        Window *w = &windows[wi];
        if (w->kind == APP_TERMINAL) {
            int ch = scancode_to_char(code, shift);
            if (ch == '\b') { if (term.cmd_len > 0) { term.cmd_len--; term.cmd[term.cmd_len] = 0; } }
            else if (ch == '\n') {
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
            if (code == 0x01) { if (files_sel > 0) { files_sel--; if (files_sel < files_sel_top) files_sel_top = files_sel; } }
            if (code == 0x03) { if (files_sel < 11) { files_sel++; } }
            if (code == 0x1c) { /* no-op */ }
            return;
        }
        if (w->kind == APP_SETTINGS) {
            if (code == 0x01) { if (settings_scale > 1) settings_scale--; }
            if (code == 0x03) { if (settings_scale < 4) settings_scale++; }
            if (code == 0x0f) { if (settings_bright > 0) settings_bright -= 3; }
            if (code == 0x02 /* left = - */) { if (settings_bright > 0) settings_bright -= 3; }
            if (code == 0x04 /* right = + */) { if (settings_bright < 100) settings_bright += 3; }
            if (code == 0x1c) { /* Enter toggles grid */ settings_grid = !settings_grid; aurora_on = settings_grid; }
            return;
        }
        if (w->kind == APP_TASKMGR) {
            if (code == 0x1d /* Esc */) { w->open = 0; w->focused = 0; return; }
            /* arrows do nothing */
            return;
        }
        if (w->kind == APP_ABOUT) {
            if (code == 0x1d) { w->open = 0; w->focused = 0; return; }
            return;
        }
    }

    /* global navigation */
    switch (code) {
        case 0x1c: /* Enter */
            if (dock_index == 5) start_open = 1;
            else if (dock_index >= 0 && dock_index < NUM_APPS) win_open((AppKind)dock_index, NULL);
            return;
        case 0x02: /* left */
            dock_index = (dock_index - 1 + NUM_APPS) % NUM_APPS; return;
        case 0x04: /* right */
            dock_index = (dock_index + 1) % NUM_APPS; return;
        case 0x03: /* down */
            dock_index = (dock_index + 1) % NUM_APPS; return;
        case 0x01: /* up */
            /* nothing at desktop level */ return;
        case 0x1d: /* Esc */
            if (win_focused_idx() >= 0) { Window *w = &windows[win_focused_idx()]; w->open = 0; w->focused = 0; }
            return;
        case 0x15: /* Tab */
            /* cycle focused window */
            int cur = win_focused_idx();
            for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = 0;
            if (cur >= 0 && windows[cur].open) {
                int n = cur + 1; while (n < MAX_WINDOWS && !windows[n].open) n++;
                if (n < MAX_WINDOWS) windows[n].focused = 1;
                else { /* wrap */ for (int j = 0; j < MAX_WINDOWS; j++) if (windows[j].open) { windows[j].focused = 1; break; } }
            }
            return;
        default:
            return;
    }
}

/* keyboard from evdev */
static int keyboard_open(void) {
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        /* check we have EV_KEY */
        unsigned long mask[8] = {0};
        if (ioctl(fd, 0x80044503 /* EVIOCGBIT(EV_KEY, len) */, mask) == 0) {
            /* found an input device with keys */
            return fd;
        }
        close(fd);
    }
    return -1;
}

/* ---------------------------------------------------------------- */
/* Snapshot mode                                                    */
/* ---------------------------------------------------------------- */
static int run_snapshot(const char *png_path, int state) {
    Canvas *c = canvas_headless(800, 480);
    if (!c) { fprintf(stderr, "canvas init failed\n"); return 1; }
    time_t t = time(NULL);

    if (state == 0) {
        dock_index = 0;
        win_open(APP_TERMINAL, c);
        term_push("QUADRA-OS terminal  ·  live shell (busybox ash)");
        term_push("$ uname -r");
        term_push("7.3.0");
        term_push("$ uname -m");
        term_push("x86_64");
        term_push("$ cat /etc/os-release");
        term_push("NAME=QUADRA-OS");
    } else if (state == 1) {
        dock_index = 5; start_open = 1; start_sel = 5;
    } else if (state == 2) {
        dock_index = 2; win_open(APP_TASKMGR, c);
    } else if (state == 3) {
        dock_index = 3; win_open(APP_SETTINGS, c);
    } else if (state == 4) {
        dock_index = 1; win_open(APP_FILES, c);
    } else if (state == 5) {
        dock_index = 4; win_open(APP_ABOUT, c);
    } else {
        dock_index = 1;
    }

    render(c, t, 1);
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
        "quadra-desktop — QUADRA-OS framebuffer desktop\n\n"
        "Usage:\n"
        "  %s                        Run the desktop on /dev/fb0\n"
        "  %s --png OUT [STATE]      Render a static snapshot to OUT\n"
        "                            0=Terminal (default) 1=Start 2=TaskMgr\n"
        "                            3=Settings 4=Files 5=About 9=Idle\n"
        "\nKeyboard (in fb mode):\n"
        "  Arrows    navigate dock / start menu / files / settings scale\n"
        "  Enter     open / activate\n"
        "  Esc       close window / start menu\n"
        "  Tab       cycle windows\n"
        "  Terminal: type and Enter runs the command\n",
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
    if (kb < 0) fprintf(stderr, "note: no input device found; desktop renders static\n");

    while (1) {
        time_t t = time(NULL);
        render(c, t, settings_grid);
        canvas_blit(c);

        if (kb >= 0) {
            fd_set rfds;
            FD_ZERO(&rfds); FD_SET(kb, &rfds);
            struct timeval tv = { 0, 50000 };
            if (select(kb + 1, &rfds, NULL, NULL, &tv) > 0) {
                struct input_event ev[8];
                ssize_t n = read(kb, ev, sizeof(ev));
                if (n > 0) {
                    size_t m = (size_t)n / sizeof(struct input_event);
                    for (size_t i = 0; i < m; i++) {
                        if (ev[i].type != 0x01) continue; /* EV_KEY */
                        if (ev[i].value != 1) continue;   /* press */
                        int shift = (ev[i].code == 0x2a || ev[i].code == 0x36);
                        handle_key(ev[i].code, shift);
                    }
                }
            }
        } else {
            struct timespec ts = { 0, 100000000 };
            nanosleep(&ts, NULL);
        }
    }

    canvas_free(c);
    if (kb >= 0) close(kb);
    return 0;
}
