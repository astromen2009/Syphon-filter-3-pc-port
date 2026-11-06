#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

class SoftGpu {
public:
    std::vector<std::uint16_t> vram = std::vector<std::uint16_t>(1024 * 512, 0);

    int disp_x = 0, disp_y = 0, disp_w = 320, disp_h = 240;
    bool rgb24 = false;
    std::uint64_t frames_drawn = 0;

    template <class F> void state(F&& f) {
        f(vram); f(disp_x); f(disp_y); f(disp_w); f(disp_h); f(rgb24); f(fifo_); f(need_);
        f(img_x_); f(img_y_); f(img_w_); f(img_h_); f(img_i_); f(img_left_);
        f(read_x_); f(read_y_); f(read_w_); f(read_i_); f(read_left_);
        f(clip_x0_); f(clip_y0_); f(clip_x1_); f(clip_y1_); f(off_x_); f(off_y_);
        f(texpage_); f(texwin_); f(mask_set_); f(mask_check_);
    }

    void gp1(std::uint32_t v) {
        const auto c = v >> 24;
        if (c == 0x05) { disp_x = int(v & 0x3fe); disp_y = int((v >> 10) & 0x1ff); }
        if (c == 0x08) {
            static const int widths[4] = {256, 320, 512, 640};
            disp_w = (v & 0x40) ? 368 : widths[v & 3];
            disp_h = (v & 0x4) ? 480 : 240;
            rgb24 = (v & 0x10) != 0;
        }
        if (c == 0x00 || c == 0x01) { fifo_.clear(); need_ = 0; img_left_ = 0; }
    }
    std::uint32_t gpuread() {
        if (read_left_ <= 0) return 0;
        std::uint32_t v = 0;
        for (int k = 0; k < 2; ++k) {
            const int x = read_x_ + (read_i_ % read_w_), y = read_y_ + (read_i_ / read_w_);
            v |= std::uint32_t(vram[(y & 511) * 1024 + (x & 1023)]) << (16 * k);
            ++read_i_;
        }
        --read_left_;
        return v;
    }
    void gp0(std::uint32_t w) {
        if (img_left_ > 0) {
            for (int k = 0; k < 2 && img_i_ < img_w_ * img_h_; ++k) {
                const int x = img_x_ + (img_i_ % img_w_), y = img_y_ + (img_i_ / img_w_);
                vram[(y & 511) * 1024 + (x & 1023)] = std::uint16_t(w >> (16 * k));
                ++img_i_;
            }
            --img_left_;
            return;
        }
        fifo_.push_back(w);
        if (fifo_.size() == 1) need_ = length(w >> 24);

        const auto cmd = fifo_[0] >> 24;
        if (cmd >= 0x48 && cmd < 0x60 && (cmd & 8)) {
            if (fifo_.size() >= 3 && (w & 0xf000f000u) == 0x50005000u) { polyline(); fifo_.clear(); }
            return;
        }
        if (int(fifo_.size()) < need_) return;
        execute();
        fifo_.clear();
    }

private:
    std::vector<std::uint32_t> fifo_;
    int need_ = 0;
    int img_x_ = 0, img_y_ = 0, img_w_ = 0, img_h_ = 0, img_i_ = 0, img_left_ = 0;
    int read_x_ = 0, read_y_ = 0, read_w_ = 1, read_i_ = 0, read_left_ = 0;
    int clip_x0_ = 0, clip_y0_ = 0, clip_x1_ = 1023, clip_y1_ = 511, off_x_ = 0, off_y_ = 0;
    std::uint32_t texpage_ = 0, texwin_ = 0;
    bool mask_set_ = false, mask_check_ = false;

    static int length(std::uint32_t c) {
        if (c >= 0x20 && c < 0x40) {
            const bool quad = c & 8, tex = c & 4, gour = c & 0x10;
            const int v = quad ? 4 : 3;
            return 1 + v + (tex ? v : 0) + (gour ? v - 1 : 0);
        }
        if (c >= 0x40 && c < 0x60) return (c & 0x10) ? 4 : 3;
        if (c >= 0x60 && c < 0x80) {
            int n = 2 + ((c & 4) ? 1 : 0);
            if ((c & 0x18) == 0) n += 1;
            return n;
        }
        if (c == 0x02) return 3;
        if (c >= 0x80 && c < 0xa0) return 4;
        if (c >= 0xa0 && c < 0xe0) return 3;
        return 1;
    }
    static int sx(std::uint32_t v) { return int(std::int16_t(std::uint16_t(v << 5)) >> 5); }
    static int sy(std::uint32_t v) { return int(std::int16_t(std::uint16_t((v >> 16) << 5)) >> 5); }

    std::uint16_t texel(int u, int v, std::uint32_t tpage, std::uint32_t clut) const {

        const int mx = int(texwin_ & 31) * 8, my = int((texwin_ >> 5) & 31) * 8;
        const int ox = int((texwin_ >> 10) & 31) * 8, oy = int((texwin_ >> 15) & 31) * 8;
        u &= 255; v &= 255;
        u = (u & ~mx) | (ox & mx); v = (v & ~my) | (oy & my);
        const int bx = int(tpage & 15) * 64, by = int((tpage >> 4) & 1) * 256;
        const int depth = int((tpage >> 7) & 3);
        const int cx = int(clut & 63) * 16, cy = int((clut >> 6) & 511);
        if (depth == 0) {
            const auto w = vram[((by + v) & 511) * 1024 + ((bx + (u >> 2)) & 1023)];
            return vram[cy * 1024 + ((cx + ((w >> ((u & 3) * 4)) & 15)) & 1023)];
        }
        if (depth == 1) {
            const auto w = vram[((by + v) & 511) * 1024 + ((bx + (u >> 1)) & 1023)];
            return vram[cy * 1024 + ((cx + ((w >> ((u & 1) * 8)) & 255)) & 1023)];
        }
        return vram[((by + v) & 511) * 1024 + ((bx + u) & 1023)];
    }

    void plot(int x, int y, std::uint16_t c, bool semi, int abr) {
        if (x < clip_x0_ || x > clip_x1_ || y < clip_y0_ || y > clip_y1_) return;
        if (static const char* px = std::getenv("SF3N_GPUPIX"); px) {
            static int X = -1, Y = -1, cnt = 0; if (X < 0) std::sscanf(px, "%d:%d", &X, &Y);
            if (x == X && (y & 255) == Y && cnt++ < 3000) { std::fprintf(stderr, "GPUPIX y=%d off=%d,%d c=%04x semi=%d abr=%d |", y, off_x_, off_y_, unsigned(c), int(semi), abr); for (std::size_t q = 0; q < fifo_.size() && q < 13; ++q) std::fprintf(stderr, " %08x", fifo_[q]); std::fprintf(stderr, "\n"); }
        }
        auto& d = vram[(y & 511) * 1024 + (x & 1023)];
        if (mask_check_ && (d & 0x8000)) return;
        if (semi) {
            int br = d & 31, bg = (d >> 5) & 31, bb = (d >> 10) & 31;
            int fr = c & 31, fg = (c >> 5) & 31, fb = (c >> 10) & 31;
            auto mix = [&](int b, int f) {
                switch (abr) {
                case 0: return (b + f) / 2;
                case 1: return std::min(31, b + f);
                case 2: return std::max(0, b - f);
                default: return std::min(31, b + f / 4);
                }
            };
            c = std::uint16_t((c & 0x8000) | mix(br, fr) | (mix(bg, fg) << 5) | (mix(bb, fb) << 10));
        }
        d = std::uint16_t(c | (mask_set_ ? 0x8000 : 0));
    }

    struct V { int x, y, r, g, b, u, v; };

    void triangle(V a, V b, V c, bool tex, bool gour, bool semi, bool raw, std::uint32_t tpage, std::uint32_t clut) {
        const int abr = int((tpage >> 5) & 3);

        if (std::max({a.x, b.x, c.x}) - std::min({a.x, b.x, c.x}) > 1023 ||
            std::max({a.y, b.y, c.y}) - std::min({a.y, b.y, c.y}) > 511) return;
        const int minx = std::max(clip_x0_, std::min({a.x, b.x, c.x})), maxx = std::min(clip_x1_, std::max({a.x, b.x, c.x}));
        const int miny = std::max(clip_y0_, std::min({a.y, b.y, c.y})), maxy = std::min(clip_y1_, std::max({a.y, b.y, c.y}));
        if (maxx - minx > 1023 || maxy - miny > 511) return;
        const long area = long(b.x - a.x) * (c.y - a.y) - long(c.x - a.x) * (b.y - a.y);
        if (area == 0) return;
        const double A = double(std::labs(area));
        const long sgn = area < 0 ? -1 : 1;

        const long d0 = sgn * long(b.y - c.y), d1 = sgn * long(c.y - a.y), d2 = sgn * long(a.y - b.y);
        for (int y = miny; y <= maxy; ++y) {
            long w0 = sgn * (long(b.x - minx) * (c.y - y) - long(c.x - minx) * (b.y - y));
            long w1 = sgn * (long(c.x - minx) * (a.y - y) - long(a.x - minx) * (c.y - y));
            long w2 = sgn * (long(a.x - minx) * (b.y - y) - long(b.x - minx) * (a.y - y));
            bool inside = false;
            for (int x = minx; x <= maxx; ++x, w0 += d0, w1 += d1, w2 += d2) {
                if (w0 < 0 || w1 < 0 || w2 < 0) { if (inside) break; continue; }
                inside = true;
                const double fa = w0 / A, fb = w1 / A, fc = w2 / A;
                int r = a.r, g = a.g, bl = a.b;
                if (gour) {
                    r = int(a.r * fa + b.r * fb + c.r * fc);
                    g = int(a.g * fa + b.g * fb + c.g * fc);
                    bl = int(a.b * fa + b.b * fb + c.b * fc);
                }
                std::uint16_t col;
                bool s = semi;
                if (tex) {
                    const int u = int(a.u * fa + b.u * fb + c.u * fc), v = int(a.v * fa + b.v * fb + c.v * fc);
                    const auto t = texel(u, v, tpage, clut);
                    if (t == 0) continue;
                    s = semi && (t & 0x8000);
                    if (raw) col = t;
                    else {
                        const int tr = std::min(31, (t & 31) * r / 128), tg = std::min(31, ((t >> 5) & 31) * g / 128),
                                  tb = std::min(31, ((t >> 10) & 31) * bl / 128);
                        col = std::uint16_t((t & 0x8000) | tr | (tg << 5) | (tb << 10));
                    }
                } else {
                    col = std::uint16_t((r >> 3) | ((g >> 3) << 5) | ((bl >> 3) << 10));
                }
                plot(x, y, col, s, abr);
            }
        }
    }

    void polygon() {
        const auto c = fifo_[0] >> 24;
        const bool quad = c & 8, tex = c & 4, gour = c & 0x10, semi = c & 2, raw = c & 1;
        const int n = quad ? 4 : 3;
        V v[4]{};
        std::size_t i = 0;
        std::uint32_t color = fifo_[i++] & 0xffffff;
        std::uint32_t clut = 0, tpage = texpage_;
        for (int k = 0; k < n; ++k) {
            if (k > 0 && gour) color = fifo_[i++] & 0xffffff;
            const auto xy = fifo_[i++];
            v[k].x = sx(xy) + off_x_; v[k].y = sy(xy) + off_y_;
            v[k].r = int(color & 0xff); v[k].g = int((color >> 8) & 0xff); v[k].b = int((color >> 16) & 0xff);
            if (tex) {
                const auto uv = fifo_[i++];
                v[k].u = int(uv & 0xff); v[k].v = int((uv >> 8) & 0xff);
                if (k == 0) clut = uv >> 16;
                if (k == 1) { tpage = uv >> 16; texpage_ = (texpage_ & ~0x1ffu) | (tpage & 0x1ff); }
            }
        }
        if (static const char* bx = std::getenv("SF3N_GPUBOX"); bx) {
            static int b0 = 0, b1 = 0, b2 = 0, b3 = 0, cnt = 0; if (!b2) std::sscanf(bx, "%d:%d:%d:%d", &b0, &b1, &b2, &b3);
            int mx = 9999, my = 9999, Mx = -9999, My = -9999;
            for (int k = 0; k < n; ++k) { mx = std::min(mx, v[k].x - off_x_); Mx = std::max(Mx, v[k].x - off_x_); my = std::min(my, v[k].y - off_y_); My = std::max(My, v[k].y - off_y_); }
            if (mx <= b2 && Mx >= b0 && my <= b3 && My >= b1 && Mx - mx < 1000 && cnt < 4000) { ++cnt; std::fprintf(stderr, "GPUBOX tp=%03x clut=%04x |", unsigned(tpage & 0x1ff), unsigned(clut)); for (std::size_t q = 0; q < i; ++q) std::fprintf(stderr, " %08x", fifo_[q]); std::fprintf(stderr, "\n"); }
        }
        triangle(v[0], v[1], v[2], tex, gour, semi, raw, tpage, clut);
        if (quad) triangle(v[1], v[2], v[3], tex, gour, semi, raw, tpage, clut);
    }

    void rect() {
        const auto c = fifo_[0] >> 24;
        const bool tex = c & 4, semi = c & 2, raw = c & 1;
        std::size_t i = 0;
        const auto color = fifo_[i++];
        const auto xy = fifo_[i++];
        int u0 = 0, v0 = 0;
        std::uint32_t clut = 0;
        if (tex) { const auto uv = fifo_[i++]; u0 = int(uv & 0xff); v0 = int((uv >> 8) & 0xff); clut = uv >> 16; }
        int w = 1, h = 1;
        switch ((c >> 3) & 3) {
        case 0: { const auto wh = fifo_[i++]; w = int(wh & 0x3ff); h = int((wh >> 16) & 0x1ff); break; }
        case 1: w = h = 1; break;
        case 2: w = h = 8; break;
        case 3: w = h = 16; break;
        }
        const int x0 = sx(xy) + off_x_, y0 = sy(xy) + off_y_;
        const int abr = int((texpage_ >> 5) & 3);
        const int r = int(color & 0xff), g = int((color >> 8) & 0xff), b = int((color >> 16) & 0xff);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint16_t col;
                bool s = semi;
                if (tex) {
                    const auto t = texel(u0 + x, v0 + y, texpage_, clut);
                    if (t == 0) continue;
                    s = semi && (t & 0x8000);
                    col = raw ? t : std::uint16_t((t & 0x8000) | std::min(31, (t & 31) * r / 128) |
                                                  (std::min(31, ((t >> 5) & 31) * g / 128) << 5) |
                                                  (std::min(31, ((t >> 10) & 31) * b / 128) << 10));
                } else {
                    col = std::uint16_t((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
                }
                plot(x0 + x, y0 + y, col, s, abr);
            }
    }

    void line(int x0, int y0, int x1, int y1, std::uint32_t color, bool semi) {
        const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        const auto col = std::uint16_t(((color & 0xff) >> 3) | (((color >> 8) & 0xff) >> 3 << 5) | (((color >> 16) & 0xff) >> 3 << 10));
        for (int i = 0; i <= n && n < 2048; ++i) {
            const int x = n ? x0 + (x1 - x0) * i / n : x0, y = n ? y0 + (y1 - y0) * i / n : y0;
            plot(x, y, col, semi, int((texpage_ >> 5) & 3));
        }
    }
    void polyline() {
        const bool gour = (fifo_[0] >> 24) & 0x10;
        std::size_t i = 1;
        auto color = fifo_[0];
        int px = sx(fifo_[i]) + off_x_, py = sy(fifo_[i]) + off_y_;
        ++i;
        while (i < fifo_.size() && (fifo_[i] & 0xf000f000u) != 0x50005000u) {
            if (gour) { color = fifo_[i++]; if (i >= fifo_.size()) break; }
            const int x = sx(fifo_[i]) + off_x_, y = sy(fifo_[i]) + off_y_;
            line(px, py, x, y, color, (fifo_[0] >> 24) & 2);
            px = x; py = y; ++i;
        }
    }

    void execute() {
        const auto w0 = fifo_[0];
        const auto c = w0 >> 24;
        if (c >= 0x20 && c < 0x40) { polygon(); return; }
        if (static const char* bx = std::getenv("SF3N_GPUBOX"); bx && c >= 0x40 && c < 0x80 && fifo_.size() > 1) {
            static int cnt2 = 0; int b0 = 0, b1 = 0, b2 = 0, b3 = 0; std::sscanf(bx, "%d:%d:%d:%d", &b0, &b1, &b2, &b3);
            const int x = sx(fifo_[1]), y = sy(fifo_[1]);
            if (std::abs(x - b0) < 60 && std::abs(y - b1) < 40 && cnt2++ < 4000) { std::fprintf(stderr, "GPUBOX2 c=%02x |", unsigned(c)); for (auto q : fifo_) std::fprintf(stderr, " %08x", q); std::fprintf(stderr, "\n"); }
        }
        if (c >= 0x40 && c < 0x60) {
            const bool g = c & 0x10;
            line(sx(fifo_[1]) + off_x_, sy(fifo_[1]) + off_y_, sx(fifo_[g ? 3 : 2]) + off_x_, sy(fifo_[g ? 3 : 2]) + off_y_, w0, c & 2);
            return;
        }
        if (c >= 0x60 && c < 0x80) { rect(); return; }
        if (c == 0x02) {
            const int x = int(fifo_[1] & 0x3f0), y = int((fifo_[1] >> 16) & 0x1ff);
            const int w = int(((fifo_[2] & 0x3ff) + 15) & ~15), h = int((fifo_[2] >> 16) & 0x1ff);
            const auto col = std::uint16_t(((w0 & 0xff) >> 3) | (((w0 >> 8) & 0xff) >> 3 << 5) | (((w0 >> 16) & 0xff) >> 3 << 10));
            for (int yy = 0; yy < h; ++yy)
                for (int xx = 0; xx < w; ++xx) vram[((y + yy) & 511) * 1024 + ((x + xx) & 1023)] = col;
            return;
        }
        if (c >= 0x80 && c < 0xa0) {
            const int sxp = int(fifo_[1] & 0x3ff), syp = int((fifo_[1] >> 16) & 0x1ff);
            const int dx = int(fifo_[2] & 0x3ff), dy = int((fifo_[2] >> 16) & 0x1ff);
            int w = int(fifo_[3] & 0x3ff), h = int((fifo_[3] >> 16) & 0x1ff);
            if (!w) w = 1024; if (!h) h = 512;
            std::vector<std::uint16_t> tmp(std::size_t(w) * h);
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) tmp[std::size_t(y) * w + x] = vram[((syp + y) & 511) * 1024 + ((sxp + x) & 1023)];
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) vram[((dy + y) & 511) * 1024 + ((dx + x) & 1023)] = tmp[std::size_t(y) * w + x];
            return;
        }
        if (c >= 0xa0 && c < 0xc0) {
            img_x_ = int(fifo_[1] & 0x3ff); img_y_ = int((fifo_[1] >> 16) & 0x1ff);
            img_w_ = int(fifo_[2] & 0x3ff); img_h_ = int((fifo_[2] >> 16) & 0x1ff);
            if (!img_w_) img_w_ = 1024; if (!img_h_) img_h_ = 512;
            img_i_ = 0; img_left_ = (img_w_ * img_h_ + 1) / 2;
            return;
        }
        if (c >= 0xc0 && c < 0xe0) {
            read_x_ = int(fifo_[1] & 0x3ff); read_y_ = int((fifo_[1] >> 16) & 0x1ff);
            read_w_ = std::max(1, int(fifo_[2] & 0x3ff)); const int h = int((fifo_[2] >> 16) & 0x1ff);
            read_i_ = 0; read_left_ = (read_w_ * h + 1) / 2;
            return;
        }
        switch (c) {
        case 0xe1: texpage_ = w0 & 0x3fff; break;
        case 0xe2: texwin_ = w0 & 0xfffff; break;
        case 0xe3: clip_x0_ = int(w0 & 0x3ff); clip_y0_ = int((w0 >> 10) & 0x1ff); break;
        case 0xe4: clip_x1_ = int(w0 & 0x3ff); clip_y1_ = int((w0 >> 10) & 0x1ff); break;
        case 0xe5: off_x_ = int(std::int32_t(w0 << 21) >> 21); off_y_ = int(std::int32_t((w0 >> 11) << 21) >> 21); break;
        case 0xe6: mask_set_ = w0 & 1; mask_check_ = w0 & 2; break;
        default: break;
        }
    }
};
