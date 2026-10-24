#include "echo/ui.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
namespace echo::ui {
namespace {
float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}
// Signed distance from a point to a rounded rectangle (negative inside).
float roundBoxDistance(float px, float py, float x0, float y0, float x1, float y1, float r) {
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
    r = std::clamp(r, 0.0f, std::min(hw, hh));
    float qx = std::abs(px - cx) - (hw - r), qy = std::abs(py - cy) - (hh - r);
    return std::min(std::max(qx, qy), 0.0f) + std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) - r;
}
float segmentDistance(float px, float py, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0, length = dx * dx + dy * dy;
    float t = length > 0 ? clamp01(((px - x0) * dx + (py - y0) * dy) / length) : 0;
    return std::hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}
} // namespace

Color mix(Color a, Color b, float t) {
    auto lerp = [&](std::uint8_t x, std::uint8_t y) { return std::uint8_t(std::lround(x + (y - x) * t)); };
    return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
}
Color alpha(Color c, float a) {
    c.a = std::uint8_t(std::lround(c.a * clamp01(a)));
    return c;
}
COLORREF colorref(Color c) {
    return RGB(c.r, c.g, c.b);
}

int Box::right() const {
    return x + w;
}
int Box::bottom() const {
    return y + h;
}
bool Box::contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
}
Box Box::inset(int dx, int dy) const {
    return {x + dx, y + dy, std::max(0, w - 2 * dx), std::max(0, h - 2 * dy)};
}
Box Box::intersect(const Box& o) const {
    int left = std::max(x, o.x), top = std::max(y, o.y);
    int r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
    return {left, top, std::max(0, r - left), std::max(0, b - top)};
}

const Theme& theme(bool dark) {
    static const Theme light{
        false,          hex(0xEEF0F4), hex(0xFFFFFF), hex(0xFFFFFF), hex(0xF1F3F6), hex(0xFFFFFF),
        hex(0xE2E6EC),  hex(0xCDD3DC), hex(0xEBEEF2), hex(0x1E2430), hex(0x5F6B7A), hex(0x8C96A4),
        hex(0xF0F2F5),  hex(0xE5E9EE), hex(0x2F6BEB), hex(0x2A60D6), hex(0x2453BB), hex(0xE7EEFD),
        hex(0x1D55C7),  hex(0xFFFFFF), hex(0x16A05A), hex(0x12904F), hex(0x0F7C44), hex(0xE5484D),
        hex(0xD43D42),  hex(0xBA3338), hex(0xD9820F), hex(0x1F2530), hex(0xF3F5F8),
        hex(0x101828, 26), hex(0xEEF0F4)};
    static const Theme slate{
        true,           hex(0x15181D), hex(0x1E2128), hex(0x1E2128), hex(0x17191F), hex(0x2A2F38),
        hex(0x2D323B),  hex(0x3B414D), hex(0x282C34), hex(0xE3E6EB), hex(0xA2AAB6), hex(0x767F8D),
        hex(0x272B33),  hex(0x30353F), hex(0x4C82F0), hex(0x5C8EF3), hex(0x3F72DA), hex(0x223150),
        hex(0x8EB1FA),  hex(0xFFFFFF), hex(0x1F9E5B), hex(0x24B067), hex(0x1A8A4F), hex(0xE5484D),
        hex(0xEC5A5F),  hex(0xC93A3F), hex(0xF0A23C), hex(0x3A404C), hex(0xF3F5F8),
        hex(0x000000, 90), hex(0x15181D)};
    return dark ? slate : light;
}

int dp(float value, float scale) {
    return int(std::lround(value * scale));
}
float systemScale() {
    HDC screen = GetDC(nullptr);
    int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen)
        ReleaseDC(nullptr, screen);
    return std::clamp(dpi / 96.0f, 1.0f, 3.0f);
}

void Fonts::create(float next) {
    destroy();
    scale = next;
    auto make = [&](int px, int weight, const wchar_t* face) {
        return CreateFontW(-dp(float(px), scale), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           face);
    };
    body = make(13, FW_NORMAL, L"Microsoft YaHei UI");
    small = make(12, FW_NORMAL, L"Microsoft YaHei UI");
    strong = make(13, FW_BOLD, L"Microsoft YaHei UI");
    title = make(15, FW_BOLD, L"Microsoft YaHei UI");
    mark = make(13, FW_BOLD, L"Segoe UI");
}
void Fonts::destroy() {
    for (HFONT* f : {&body, &small, &strong, &title, &mark}) {
        if (*f)
            DeleteObject(*f);
        *f = nullptr;
    }
}
Fonts::~Fonts() {
    destroy();
}

Painter::~Painter() {
    if (dc_) {
        if (old_)
            SelectObject(dc_, old_);
        DeleteDC(dc_);
    }
    if (bitmap_)
        DeleteObject(bitmap_);
}
void Painter::resize(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (!dc_)
        dc_ = CreateCompatibleDC(nullptr);
    if (bitmap_ && width == width_ && height == height_)
        return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    HBITMAP next = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!next)
        return; // Keep the previous surface; painting is skipped rather than crashing the editor.
    if (old_)
        SelectObject(dc_, old_);
    if (bitmap_)
        DeleteObject(bitmap_);
    bitmap_ = next;
    old_ = HBITMAP(SelectObject(dc_, bitmap_));
    pixels_ = static_cast<std::uint32_t*>(bits);
    width_ = width;
    height_ = height;
    clip_ = {0, 0, width_, height_};
}
HDC Painter::dc() const {
    return dc_;
}
int Painter::width() const {
    return width_;
}
int Painter::height() const {
    return height_;
}
std::uint32_t* Painter::pixels() {
    flush();
    return pixels_;
}
void Painter::clip(Box b) {
    clip_ = b.intersect({0, 0, width_, height_});
}
void Painter::unclip() {
    clip_ = {0, 0, width_, height_};
}
Box Painter::clipBox() const {
    return clip_;
}
void Painter::flush() {
    if (gdi_) {
        GdiFlush();
        gdi_ = false;
    }
}
void Painter::plot(int x, int y, Color c, float coverage) {
    int a = int(c.a * coverage + .5f);
    if (a <= 0)
        return;
    auto& px = pixels_[y * width_ + x];
    if (a >= 255) {
        px = 0xFF000000u | (std::uint32_t(c.r) << 16) | (std::uint32_t(c.g) << 8) | c.b;
        return;
    }
    int r = (px >> 16) & 255, g = (px >> 8) & 255, b = px & 255;
    r += (int(c.r) - r) * a / 255;
    g += (int(c.g) - g) * a / 255;
    b += (int(c.b) - b) * a / 255;
    px = 0xFF000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b);
}
template <class Distance>
void Painter::shape(float x0, float y0, float x1, float y1, Color c, Distance distance) {
    if (!pixels_ || !c.a)
        return;
    flush();
    int left = std::max(clip_.x, int(std::floor(x0)) - 1), top = std::max(clip_.y, int(std::floor(y0)) - 1);
    int right = std::min(clip_.right(), int(std::ceil(x1)) + 1),
        bottom = std::min(clip_.bottom(), int(std::ceil(y1)) + 1);
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) {
            float d = distance(x + .5f, y + .5f);
            if (d < .5f)
                plot(x, y, c, clamp01(.5f - d));
        }
}
void Painter::fill(Box b, Color c) {
    if (!pixels_ || !c.a)
        return;
    flush();
    Box area = b.intersect(clip_);
    for (int y = area.y; y < area.bottom(); ++y) {
        if (c.a == 255) {
            std::fill_n(pixels_ + y * width_ + area.x, area.w,
                        0xFF000000u | (std::uint32_t(c.r) << 16) | (std::uint32_t(c.g) << 8) | c.b);
            continue;
        }
        for (int x = area.x; x < area.right(); ++x)
            plot(x, y, c, 1);
    }
}
void Painter::round(Box b, float radius, Color fillColor, Color stroke, float strokeWidth) {
    if (!pixels_ || b.w <= 0 || b.h <= 0)
        return;
    bool stroked = stroke.a > 0 && strokeWidth > 0;
    if (radius <= 0 && !stroked) {
        fill(b, fillColor);
        return;
    }
    flush();
    Box area = b.intersect(clip_);
    float r = std::clamp(radius, 0.0f, std::min(b.w, b.h) / 2.0f);
    int edge = int(std::ceil(std::max(r, stroked ? strokeWidth : 0.0f))) + 1;
    Box inner{b.x + edge, b.y + edge, std::max(0, b.w - 2 * edge), std::max(0, b.h - 2 * edge)};
    for (int y = area.y; y < area.bottom(); ++y) {
        bool innerRow = inner.w > 0 && y >= inner.y && y < inner.bottom();
        int x = area.x;
        while (x < area.right()) {
            if (innerRow && x >= inner.x && x < inner.right()) {
                int end = std::min(inner.right(), area.right());
                if (fillColor.a)
                    for (int i = x; i < end; ++i)
                        plot(i, y, fillColor, 1);
                x = end;
                continue;
            }
            float d = roundBoxDistance(x + .5f, y + .5f, float(b.x), float(b.y), float(b.right()),
                                       float(b.bottom()), r);
            float outer = clamp01(.5f - d);
            if (outer > 0) {
                if (stroked) {
                    float inside = clamp01(.5f - (d + strokeWidth));
                    if (fillColor.a && inside > 0)
                        plot(x, y, fillColor, inside);
                    plot(x, y, stroke, outer - inside);
                } else
                    plot(x, y, fillColor, outer);
            }
            ++x;
        }
    }
}
void Painter::circle(float cx, float cy, float r, Color c) {
    shape(cx - r, cy - r, cx + r, cy + r, c, [&](float x, float y) { return std::hypot(x - cx, y - cy) - r; });
}
void Painter::ring(float cx, float cy, float r, float width, Color c) {
    float outer = r + width / 2;
    shape(cx - outer, cy - outer, cx + outer, cy + outer, c,
          [&](float x, float y) { return std::abs(std::hypot(x - cx, y - cy) - r) - width / 2; });
}
void Painter::line(float x0, float y0, float x1, float y1, float width, Color c) {
    float pad = width / 2 + 1;
    shape(std::min(x0, x1) - pad, std::min(y0, y1) - pad, std::max(x0, x1) + pad, std::max(y0, y1) + pad, c,
          [&](float x, float y) { return segmentDistance(x, y, x0, y0, x1, y1) - width / 2; });
}
void Painter::polyline(const float* xy, int points, float width, Color c, bool closed) {
    if (points < 2)
        return;
    float minX = xy[0], maxX = xy[0], minY = xy[1], maxY = xy[1];
    for (int i = 1; i < points; ++i) {
        minX = std::min(minX, xy[i * 2]);
        maxX = std::max(maxX, xy[i * 2]);
        minY = std::min(minY, xy[i * 2 + 1]);
        maxY = std::max(maxY, xy[i * 2 + 1]);
    }
    int segments = closed ? points : points - 1;
    float pad = width / 2 + 1;
    // One pass over the minimum distance keeps joints from being blended twice.
    shape(minX - pad, minY - pad, maxX + pad, maxY + pad, c, [&](float x, float y) {
        float best = 1e9f;
        for (int i = 0; i < segments; ++i) {
            int j = (i + 1) % points;
            best = std::min(best, segmentDistance(x, y, xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1]));
        }
        return best - width / 2;
    });
}
void Painter::polygon(const float* xy, int points, Color c) {
    if (points < 3)
        return;
    float minX = xy[0], maxX = xy[0], minY = xy[1], maxY = xy[1], cx = 0, cy = 0;
    for (int i = 0; i < points; ++i) {
        minX = std::min(minX, xy[i * 2]);
        maxX = std::max(maxX, xy[i * 2]);
        minY = std::min(minY, xy[i * 2 + 1]);
        maxY = std::max(maxY, xy[i * 2 + 1]);
        cx += xy[i * 2] / points;
        cy += xy[i * 2 + 1] / points;
    }
    auto edge = [&](int i, float x, float y) {
        int j = (i + 1) % points;
        float ex = xy[j * 2] - xy[i * 2], ey = xy[j * 2 + 1] - xy[i * 2 + 1];
        float length = std::max(1e-6f, std::hypot(ex, ey));
        return ((x - xy[i * 2]) * ey - (y - xy[i * 2 + 1]) * ex) / length;
    };
    float orientation = edge(0, cx, cy) > 0 ? -1.0f : 1.0f;
    shape(minX, minY, maxX, maxY, c, [&](float x, float y) {
        float d = -1e9f;
        for (int i = 0; i < points; ++i)
            d = std::max(d, edge(i, x, y) * orientation);
        return d;
    });
}
void Painter::crescent(float cx, float cy, float r, float cutX, float cutY, float cutR, Color c) {
    shape(cx - r, cy - r, cx + r, cy + r, c, [&](float x, float y) {
        return std::max(std::hypot(x - cx, y - cy) - r, cutR - std::hypot(x - cutX, y - cutY));
    });
}
void Painter::text(Box b, std::wstring_view s, HFONT font, Color c, UINT format) {
    Box area = b.intersect(clip_);
    if (!dc_ || s.empty() || area.w <= 0 || area.h <= 0)
        return;
    gdi_ = true;
    int saved = SaveDC(dc_);
    IntersectClipRect(dc_, area.x, area.y, area.right(), area.bottom());
    SelectObject(dc_, font);
    SetBkMode(dc_, TRANSPARENT);
    SetTextColor(dc_, colorref(c));
    RECT r{b.x, b.y, b.right(), b.bottom()};
    DrawTextW(dc_, s.data(), int(s.size()), &r, format | DT_NOPREFIX);
    RestoreDC(dc_, saved);
}
int Painter::measure(std::wstring_view s, HFONT font) {
    if (!dc_)
        dc_ = CreateCompatibleDC(nullptr);
    if (s.empty())
        return 0;
    auto old = SelectObject(dc_, font);
    SIZE size{};
    GetTextExtentPoint32W(dc_, s.data(), int(s.size()), &size);
    SelectObject(dc_, old);
    return size.cx;
}
void Painter::image(HBITMAP bitmap, int sx, int sy, int sw, int sh, Box dst) {
    Box area = dst.intersect(clip_);
    if (!dc_ || !bitmap || area.w <= 0 || area.h <= 0)
        return;
    gdi_ = true;
    HDC source = CreateCompatibleDC(dc_);
    auto old = SelectObject(source, bitmap);
    int saved = SaveDC(dc_);
    IntersectClipRect(dc_, area.x, area.y, area.right(), area.bottom());
    SetStretchBltMode(dc_, COLORONCOLOR);
    StretchBlt(dc_, dst.x, dst.y, dst.w, dst.h, source, sx, sy, sw, sh, SRCCOPY);
    RestoreDC(dc_, saved);
    SelectObject(source, old);
    DeleteDC(source);
}
void Painter::present(HDC target, Box area, int targetX, int targetY) {
    if (!dc_ || !target)
        return;
    flush();
    BitBlt(target, targetX, targetY, area.w, area.h, dc_, area.x, area.y, SRCCOPY);
}

// Icons are authored on a 16 x 16 grid and scaled into the destination box.
void Painter::icon(Icon kind, Box b, Color c) {
    if (kind == Icon::None)
        return;
    float side = float(std::min(b.w, b.h)), k = side / 16;
    float ox = b.x + (b.w - side) / 2, oy = b.y + (b.h - side) / 2;
    float sw = std::max(1.25f, 1.45f * k);
    std::vector<float> v;
    auto X = [&](float x) { return ox + x * k; };
    auto Y = [&](float y) { return oy + y * k; };
    auto points = [&](std::initializer_list<float> list, bool mirror = false) {
        v.clear();
        int i = 0;
        for (float value : list)
            v.push_back(i++ % 2 ? Y(value) : X(mirror ? 16 - value : value));
        return int(v.size() / 2);
    };
    auto stroke = [&](std::initializer_list<float> list, bool closed = false, bool mirror = false) {
        int n = points(list, mirror);
        polyline(v.data(), n, sw, c, closed);
    };
    auto solid = [&](std::initializer_list<float> list) {
        int n = points(list);
        polygon(v.data(), n, c);
    };
    auto segment = [&](float a, float b2, float c2, float d, float width = 0) {
        line(X(a), Y(b2), X(c2), Y(d), width ? width * k : sw, c);
    };
    auto dot = [&](float x, float y, float r) { circle(X(x), Y(y), r * k, c); };
    auto box = [&](float x0, float y0, float x1, float y1, float r, bool filled) {
        float l = X(x0), t = Y(y0), rr = X(x1), bb = Y(y1), radius = r * k;
        shape(l - sw, t - sw, rr + sw, bb + sw, c, [&](float x, float y) {
            float d = roundBoxDistance(x, y, l, t, rr, bb, radius);
            return filled ? d : std::abs(d) - sw / 2;
        });
    };
    auto arc = [&](float cx, float cy, float rx, float ry, float a0, float a1, bool mirror = false) {
        int steps = 10;
        for (int i = 0; i <= steps; ++i) {
            float a = (a0 + (a1 - a0) * i / steps) * 3.14159265f / 180;
            float x = cx + rx * std::cos(a);
            v.push_back(X(mirror ? 16 - x : x));
            v.push_back(Y(cy + ry * std::sin(a)));
        }
    };
    switch (kind) {
    case Icon::New:
        stroke({3.5f, 1.75f, 9.25f, 1.75f, 12.5f, 5, 12.5f, 14.25f, 3.5f, 14.25f}, true);
        stroke({9.25f, 1.75f, 9.25f, 5, 12.5f, 5});
        segment(5.75f, 9.75f, 10.25f, 9.75f);
        segment(8, 7.5f, 8, 12);
        break;
    case Icon::Open:
    case Icon::Folder:
        stroke({1.75f, 3.75f, 6, 3.75f, 7.5f, 5.5f, 14.25f, 5.5f, 14.25f, 12.75f, 1.75f, 12.75f}, true);
        if (kind == Icon::Open)
            segment(1.75f, 8, 14.25f, 8);
        break;
    case Icon::Save:
        box(2.25f, 2.25f, 13.75f, 13.75f, 2, false);
        solid({5.25f, 2.5f, 10.5f, 2.5f, 10.5f, 5.75f, 5.25f, 5.75f});
        stroke({4.75f, 13.5f, 4.75f, 9.5f, 11.25f, 9.5f, 11.25f, 13.5f});
        break;
    case Icon::Undo:
    case Icon::Redo: {
        bool mirror = kind == Icon::Redo;
        stroke({5.5f, 2.75f, 2.5f, 5.75f, 5.5f, 8.75f}, false, mirror);
        v.clear();
        v.push_back(X(mirror ? 16 - 2.75f : 2.75f));
        v.push_back(Y(5.75f));
        arc(9.25f, 9.25f, 3.5f, 3.5f, -90, 90, mirror);
        v.push_back(X(mirror ? 16 - 5.5f : 5.5f));
        v.push_back(Y(12.75f));
        polyline(v.data(), int(v.size() / 2), sw, c);
        break;
    }
    case Icon::Terrain:
        box(2.25f, 2.25f, 7.25f, 7.25f, 1.25f, true);
        box(8.75f, 2.25f, 13.75f, 7.25f, 1.25f, false);
        box(2.25f, 8.75f, 7.25f, 13.75f, 1.25f, false);
        box(8.75f, 8.75f, 13.75f, 13.75f, 1.25f, false);
        break;
    case Icon::Event:
        box(2, 2.25f, 14, 11, 2.5f, false);
        stroke({5, 11, 5, 14, 8.5f, 11});
        segment(8, 4.75f, 8, 7);
        dot(8, 8.9f, .95f);
        break;
    case Icon::Object:
        ring(X(8), Y(4.75f), 2.6f * k, sw, c);
        v.clear();
        v.push_back(X(3.25f));
        v.push_back(Y(14.25f));
        arc(8, 12.75f, 4.75f, 4.5f, 180, 360);
        v.push_back(X(12.75f));
        v.push_back(Y(14.25f));
        polyline(v.data(), int(v.size() / 2), sw, c);
        break;
    case Icon::Barrage:
        dot(8, 8, 2.1f);
        for (int i = 0; i < 6; ++i) {
            float a = (i * 60 - 90) * 3.14159265f / 180;
            dot(8 + 5.4f * std::cos(a), 8 + 5.4f * std::sin(a), 1.35f);
        }
        break;
    case Icon::Feature:
        ring(X(8), Y(8), 3.2f * k, sw, c);
        for (int i = 0; i < 8; ++i) {
            float a = i * 45 * 3.14159265f / 180;
            segment(8 + 4.9f * std::cos(a), 8 + 4.9f * std::sin(a), 8 + 6.2f * std::cos(a),
                    8 + 6.2f * std::sin(a), 2.3f);
        }
        break;
    case Icon::Pencil:
    case Icon::Edit:
        stroke({10.75f, 2.5f, 13.5f, 5.25f, 5.5f, 13.25f, 2.25f, 13.75f, 2.75f, 10.5f}, true);
        segment(9.1f, 4.15f, 11.85f, 6.9f);
        break;
    case Icon::Rectangle:
        box(2.25f, 3, 13.75f, 13, 1.75f, false);
        box(5.25f, 6, 10.75f, 10, .75f, true);
        break;
    case Icon::Bucket:
        stroke({2.25f, 7.5f, 7.25f, 2.5f, 12.25f, 7.5f, 7.25f, 12.5f}, true);
        segment(2.75f, 7.5f, 11.75f, 7.5f);
        dot(13.5f, 12.4f, 1.5f);
        solid({13.5f, 9.3f, 12.15f, 11.9f, 14.85f, 11.9f});
        break;
    case Icon::Select:
        for (float a : {2.25f, 6.75f, 11.25f}) {
            segment(a, 2.25f, a + 2.5f, 2.25f);
            segment(a, 13.75f, a + 2.5f, 13.75f);
            segment(2.25f, a, 2.25f, a + 2.5f);
            segment(13.75f, a, 13.75f, a + 2.5f);
        }
        break;
    case Icon::Picker:
        segment(2.75f, 13.25f, 9.25f, 6.75f);
        segment(9.5f, 6.5f, 12.5f, 3.5f, 3.4f);
        segment(7.5f, 4.75f, 11.25f, 8.5f);
        break;
    case Icon::Sliders:
        for (float y : {4.0f, 8.0f, 12.0f})
            segment(2.25f, y, 13.75f, y);
        dot(10.5f, 4, 1.9f);
        dot(5.5f, 8, 1.9f);
        dot(9, 12, 1.9f);
        break;
    case Icon::Database:
        v.clear();
        arc(8, 4, 5.25f, 2.1f, 0, 360);
        polyline(v.data(), int(v.size() / 2), sw, c, true);
        segment(2.75f, 4, 2.75f, 12);
        segment(13.25f, 4, 13.25f, 12);
        for (float y : {8.0f, 12.0f}) {
            v.clear();
            arc(8, y, 5.25f, 2.1f, 0, 180);
            polyline(v.data(), int(v.size() / 2), sw, c);
        }
        break;
    case Icon::Play:
        solid({4.25f, 2.5f, 13.25f, 8, 4.25f, 13.5f});
        break;
    case Icon::Stop:
        box(3.25f, 3.25f, 12.75f, 12.75f, 1.75f, true);
        break;
    case Icon::Sun:
        ring(X(8), Y(8), 2.8f * k, sw, c);
        for (int i = 0; i < 8; ++i) {
            float a = i * 45 * 3.14159265f / 180;
            segment(8 + 4.9f * std::cos(a), 8 + 4.9f * std::sin(a), 8 + 6.5f * std::cos(a),
                    8 + 6.5f * std::sin(a));
        }
        break;
    case Icon::Moon:
        crescent(X(8), Y(8.25f), 5.75f * k, X(11.25f), Y(5), 4.6f * k, c);
        break;
    case Icon::Plus:
        segment(8, 3, 8, 13);
        segment(3, 8, 13, 8);
        break;
    case Icon::Map:
        stroke({1.75f, 3.75f, 5.75f, 2.25f, 10.25f, 3.75f, 14.25f, 2.25f, 14.25f, 12.25f, 10.25f, 13.75f,
                5.75f, 12.25f, 1.75f, 13.75f},
               true);
        segment(5.75f, 2.25f, 5.75f, 12.25f);
        segment(10.25f, 3.75f, 10.25f, 13.75f);
        break;
    case Icon::Import:
        segment(8, 2, 8, 10);
        stroke({4.75f, 6.75f, 8, 10, 11.25f, 6.75f});
        stroke({2.25f, 10.25f, 2.25f, 13.75f, 13.75f, 13.75f, 13.75f, 10.25f});
        break;
    case Icon::ChevronDown:
        stroke({4.5f, 6.25f, 8, 9.75f, 11.5f, 6.25f});
        break;
    case Icon::ChevronRight:
        stroke({6.25f, 4.5f, 9.75f, 8, 6.25f, 11.5f});
        break;
    case Icon::Check:
        stroke({3.25f, 8.5f, 6.5f, 11.75f, 12.75f, 4.75f});
        break;
    case Icon::Dot:
        dot(8, 8, 3);
        break;
    case Icon::Flag:
        segment(4, 2.25f, 4, 14);
        solid({4.5f, 2.5f, 12.75f, 5.4f, 4.5f, 8.3f});
        break;
    case Icon::Close:
        segment(4, 4, 12, 12);
        segment(12, 4, 4, 12);
        break;
    case Icon::None:
        break;
    }
}

Box sliderTrack(Box row, float s) {
    int knob = dp(8, s);
    return {row.x + knob, row.bottom() - dp(14, s), std::max(1, row.w - 2 * knob), dp(4, s)};
}
float sliderValue(const Node& n, Box row, int x, float s) {
    Box track = sliderTrack(row, s);
    float t = clamp01(float(x - track.x) / track.w);
    return n.minimum + (n.maximum - n.minimum) * t;
}
Box dropdownField(Box row, float s) {
    int w = std::min(row.w, std::max(dp(120, s), row.w * 56 / 100));
    return {row.right() - w, row.y + dp(3, s), w, row.h - dp(6, s)};
}

void View::clear() {
    nodes.clear();
    areas.clear();
}
Node& View::add(Node n) {
    nodes.push_back(std::move(n));
    return nodes.back();
}
int View::scrollArea(int id, Box box, int step) {
    areas.push_back({id, box, 0, std::max(1, step)});
    return int(areas.size()) - 1;
}
void View::reveal(int area, int selected, int rowTop, int rowHeight) {
    const auto& a = areas[area];
    auto found = revealed.find(a.id);
    if (found != revealed.end() && found->second == selected)
        return;
    revealed[a.id] = selected;
    if (selected < 0)
        return;
    int& value = offsets[a.id];
    int top = rowTop - a.box.y;
    if (top < value)
        value = top;
    else if (top + rowHeight > value + a.box.h)
        value = top + rowHeight - a.box.h;
}
void View::finishArea(int index, int contentBottom) {
    auto& a = areas[index];
    a.content = std::max(0, contentBottom - a.box.y);
    int& value = offsets[a.id];
    value = std::clamp(value, 0, std::max(0, a.content - a.box.h));
}
int View::offset(int area) const {
    if (area < 0 || area >= int(areas.size()))
        return 0;
    auto found = offsets.find(areas[area].id);
    return found == offsets.end() ? 0 : found->second;
}
Box View::screenBox(const Node& n) const {
    Box b = n.box;
    b.y -= offset(n.scroll);
    return b;
}
const Node* View::hit(int x, int y, bool disabledToo) const {
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        const Node& n = *it;
        if (!n.action || ((n.flags & Disabled) && !disabledToo))
            continue;
        if (n.scroll >= 0 && !areas[n.scroll].box.contains(x, y))
            continue;
        if (screenBox(n).contains(x, y))
            return &n;
    }
    return nullptr;
}
const Node* View::find(int action, int value) const {
    for (const auto& n : nodes)
        if (n.action == action && n.value == value)
            return &n;
    return nullptr;
}
bool View::wheel(int x, int y, int delta) {
    for (auto& a : areas)
        if (a.box.contains(x, y) && a.content > a.box.h) {
            int& value = offsets[a.id];
            int next = std::clamp(value - delta * a.step / WHEEL_DELTA, 0, a.content - a.box.h);
            if (next == value)
                return false;
            value = next;
            return true;
        }
    return false;
}
void View::paint(Painter& p, const Theme& t, const Fonts& f, const CustomPaint& custom) const {
    for (const auto& n : nodes) {
        Box b = screenBox(n);
        if (n.scroll >= 0) {
            Box area = areas[n.scroll].box;
            if (b.intersect(area).w <= 0 || b.intersect(area).h <= 0)
                continue;
            p.clip(area);
            paintNode(p, t, f, n, b, custom);
            p.unclip();
        } else
            paintNode(p, t, f, n, b, custom);
    }
    for (int i = 0; i < int(areas.size()); ++i) {
        const auto& a = areas[i];
        if (a.content <= a.box.h)
            continue;
        int width = dp(4, f.scale), height = std::max(dp(28, f.scale), a.box.h * a.box.h / a.content);
        int top = a.box.y + int(float(offset(i)) * (a.box.h - height) / float(a.content - a.box.h));
        p.round({a.box.right() - width - dp(2, f.scale), top, width, height}, width / 2.0f,
                alpha(t.faint, .55f));
    }
}
void View::paintNode(Painter& p, const Theme& t, const Fonts& f, const Node& n, Box b,
                     const CustomPaint& custom) const {
    float s = f.scale;
    bool hovered = hot == Key{n.action, n.value} && n.action && !(n.flags & Disabled);
    bool down = hovered && pressed == hot;
    bool selected = n.flags & Selected, disabled = n.flags & Disabled;
    auto textColor = [&](Color base) {
        if (n.color.a && n.kind == Kind::Label)
            return n.color;
        if (disabled)
            return t.faint;
        if (n.flags & Accent)
            return t.accentText;
        if (n.flags & Warn)
            return t.warn;
        if (n.flags & Muted)
            return t.muted;
        if (n.flags & Faint)
            return t.faint;
        return base;
    };
    auto font = [&] {
        return (n.flags & Strong) ? f.strong : (n.flags & Small) ? f.small : f.body;
    };
    auto content = [&](Box area, Color fg, HFONT typeface, int iconSize) {
        int gap = dp(6, s), textWidth = n.text.empty() ? 0 : p.measure(n.text, typeface);
        int total = (n.icon != Icon::None ? iconSize : 0) + (n.icon != Icon::None && textWidth ? gap : 0) +
                    textWidth;
        int x = area.x + std::max(0, (area.w - total) / 2);
        if (n.icon != Icon::None) {
            Box ib{x, area.y + (area.h - iconSize) / 2, iconSize, iconSize};
            p.icon(n.icon, ib, fg);
            if (n.flags & Mark)
                p.circle(ib.right() - dp(1, s), float(ib.y + dp(2, s)), dp(3.5f, s) * 1.0f, t.warn);
            x += iconSize + (textWidth ? gap : 0);
        }
        if (textWidth)
            p.text({x, area.y, area.right() - x + dp(2, s), area.h}, n.text, typeface, fg);
    };
    switch (n.kind) {
    case Kind::Fill:
        p.round(b, float(n.radius), n.color, n.stroke);
        break;
    case Kind::Divider:
        p.fill(b, n.color.a ? n.color : t.divider);
        break;
    case Kind::Label: {
        UINT format = (n.flags & Wrap)     ? DT_LEFT | DT_WORDBREAK
                      : (n.flags & Center) ? DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
                      : (n.flags & Right)  ? DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
                                           : DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
        Box area = b;
        if (n.icon != Icon::None) {
            int size = dp(16, s);
            p.icon(n.icon, {area.x, area.y + (area.h - size) / 2, size, size}, textColor(t.muted));
            area.x += size + dp(8, s);
            area.w -= size + dp(8, s);
        }
        p.text(area, n.text, font(), textColor(t.text), format);
        break;
    }
    case Kind::Badge: {
        Color bg = t.accentSoft, fg = t.accentText;
        if (n.flags & Warn) {
            bg = mix(t.panel, t.warn, t.dark ? .2f : .13f);
            fg = t.warn;
        } else if (n.flags & Positive) {
            bg = mix(t.panel, t.play, t.dark ? .2f : .12f);
            fg = t.play;
        } else if (n.flags & Muted) {
            bg = t.sunken;
            fg = t.muted;
        }
        p.round(b, n.radius ? float(n.radius) : b.h / 2.0f, bg);
        Box area = b;
        if (n.icon != Icon::None) {
            int size = dp(8, s);
            p.icon(n.icon, {area.x + dp(8, s), area.y + (area.h - size) / 2, size, size}, fg);
            area.x += dp(10, s);
            area.w -= dp(10, s);
        }
        p.text(area, n.text, f.small, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case Kind::Button: {
        float radius = n.radius ? float(n.radius) : dp(6, s);
        Color bg{0, 0, 0, 0}, border{0, 0, 0, 0}, fg = textColor(t.text);
        bool filled = n.flags & (Primary | Positive | Negative);
        if (filled) {
            Color base = (n.flags & Positive) ? t.play : (n.flags & Negative) ? t.stop : t.accent;
            Color over = (n.flags & Positive) ? t.playHover : (n.flags & Negative) ? t.stopHover : t.accentHover;
            Color push = (n.flags & Positive)   ? t.playPressed
                         : (n.flags & Negative) ? t.stopPressed
                                                : t.accentPressed;
            bg = disabled ? t.sunken : down ? push : hovered ? over : base;
            fg = disabled ? t.faint : t.onAccent;
        } else if (selected) {
            bg = down ? mix(t.accentSoft, t.accent, .12f) : t.accentSoft;
            border = mix(t.accentSoft, t.accent, t.dark ? .35f : .28f);
            fg = disabled ? t.faint : t.accentText;
        } else {
            bool quiet = n.flags & Quiet;
            bg = down ? t.pressed : hovered ? t.hover : quiet ? Color{0, 0, 0, 0} : t.raised;
            border = quiet ? (hovered ? t.border : Color{0, 0, 0, 0}) : t.border;
            if (quiet && n.text.empty() && !disabled)
                fg = hovered ? t.text : mix(t.text, t.muted, .35f);
        }
        if (bg.a || border.a)
            p.round(b, radius, bg, border);
        content(b, fg, filled ? f.strong : font(), dp((n.flags & Small) ? 16.0f : 18.0f, s));
        break;
    }
    case Kind::Segment: {
        float radius = n.radius ? float(n.radius) : dp(6, s);
        Color fg = textColor(selected ? t.text : t.muted);
        if (selected) {
            p.round({b.x, b.y + 1, b.w, b.h}, radius, t.dark ? alpha(hex(0), .35f) : alpha(hex(0x101828), .07f));
            p.round(b, radius, t.raised, t.dark ? t.borderStrong : t.border);
            if (!disabled && !(n.flags & Muted))
                fg = t.dark ? t.text : t.accentText;
        } else if (hovered)
            p.round(b, radius, down ? t.pressed : t.hover);
        content(b, fg, selected ? f.strong : f.body, dp(16, s));
        break;
    }
    case Kind::Row: {
        float radius = dp(6, s);
        if (selected)
            p.round(b, radius, t.accentSoft);
        else if (hovered)
            p.round(b, radius, down ? t.pressed : t.hover);
        if (selected)
            p.round({b.x + dp(1, s), b.y + dp(7, s), dp(3, s), b.h - dp(14, s)}, dp(1.5f, s), t.accent);
        int x = b.x + dp(10, s) + n.indent;
        if (n.icon != Icon::None) {
            int size = dp(16, s);
            p.icon(n.icon, {x, b.y + (b.h - size) / 2, size, size},
                   selected ? t.accent : (n.flags & Header) ? t.muted : t.faint);
            x += size + dp(8, s);
        }
        int right = b.right() - dp(10, s);
        if (!n.badge.empty()) {
            int width = p.measure(n.badge, f.small) + dp(14, s), height = dp(20, s);
            Box pill{right - width, b.y + (b.h - height) / 2, width, height};
            p.round(pill, height / 2.0f, selected ? t.raised : t.accentSoft);
            p.text(pill, n.badge, f.small, t.accentText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            right = pill.x - dp(6, s);
        }
        if (!n.note.empty()) {
            int width = p.measure(n.note, f.small);
            p.text({right - width, b.y, width + dp(2, s), b.h}, n.note, f.small, selected ? t.accentText : t.faint,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            right -= width + dp(8, s);
        }
        p.text({x, b.y, std::max(0, right - x), b.h}, n.text, (n.flags & Header) ? f.strong : f.body,
               selected ? t.accentText : textColor(t.text), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case Kind::MenuTitle:
        if (hovered || selected)
            p.round(b, dp(5, s), down || selected ? t.pressed : t.hover);
        p.text(b, n.text, f.body, textColor(t.text), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    case Kind::Splitter:
        if (hovered || pressed == Key{n.action, n.value})
            p.fill({b.x + b.w / 2 - dp(1, s), b.y, std::max(2, dp(2, s)), b.h}, t.accent);
        break;
    case Kind::Section:
        p.text({b.x, b.y, b.w, b.h}, n.text, f.strong, t.text);
        if (!n.note.empty())
            p.text(b, n.note, f.small, t.faint, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        break;
    case Kind::Toggle: {
        if (hovered)
            p.round(b.inset(-dp(6, s), 0), dp(6, s), t.hover);
        int tw = dp(34, s), th = dp(20, s);
        Box track{b.right() - tw, b.y + (b.h - th) / 2, tw, th};
        Color on = disabled ? t.faint : t.accent;
        p.round(track, th / 2.0f, selected ? on : t.borderStrong);
        float r = th / 2.0f - dp(2.5f, s);
        float cx = selected ? track.right() - th / 2.0f : track.x + th / 2.0f;
        p.circle(cx, track.y + th / 2.0f, r, hex(0xFFFFFF));
        p.text({b.x, b.y, b.w - tw - dp(8, s), b.h}, n.text, f.body, textColor(t.text),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case Kind::Slider: {
        Box head{b.x, b.y, b.w, b.h - dp(20, s)};
        p.text(head, n.text, f.body, textColor(t.text), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        bool active = hovered || (pressed == Key{n.action, n.value} && n.action);
        p.text(head, n.note, f.small, active ? t.accentText : t.muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        Box track = sliderTrack(b, s);
        float range = n.maximum - n.minimum, u = range > 0 ? clamp01((n.number - n.minimum) / range) : 0;
        p.round(track, track.h / 2.0f, t.dark ? t.borderStrong : t.border);
        Box done{track.x, track.y, std::max(track.h, int(track.w * u)), track.h};
        p.round(done, track.h / 2.0f, disabled ? t.faint : t.accent);
        float kx = track.x + track.w * u, ky = track.y + track.h / 2.0f, kr = dp(7, s);
        bool dragging = pressed == Key{n.action, n.value} && n.action;
        if (hovered || dragging)
            p.circle(kx, ky, kr + dp(4, s), alpha(t.accent, dragging ? .26f : .16f));
        p.circle(kx, ky, kr, disabled ? t.faint : t.accent);
        p.circle(kx, ky, kr - dp(2, s), t.raised);
        break;
    }
    case Kind::Dropdown: {
        Box field = dropdownField(b, s);
        p.text({b.x, b.y, field.x - b.x - dp(8, s), b.h}, n.text, f.body, textColor(t.text),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        bool open = pressed == Key{n.action, n.value} && n.action; // its popup is showing
        p.round(field, dp(6, s), hovered || open ? t.hover : t.sunken,
                open ? t.accent : hovered ? t.borderStrong : t.border);
        int ic = dp(14, s);
        p.icon(Icon::ChevronDown, {field.right() - ic - dp(8, s), field.y + (field.h - ic) / 2, ic, ic}, t.muted);
        p.text({field.x + dp(10, s), field.y, field.w - ic - dp(22, s), field.h}, n.note, f.body,
               disabled ? t.faint : t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case Kind::Custom:
        if (custom)
            custom(p, n, b);
        break;
    }
}

void paintPushButton(Painter& p, Box b, std::wstring_view text, HFONT font, const Theme& t, Color surface,
                     const ControlState& s) {
    p.fill(b, surface);
    Color bg = t.raised, border = t.border, fg = t.text;
    if (s.primary) {
        bg = s.disabled ? t.sunken : s.pressed ? t.accentPressed : s.hovered ? t.accentHover : t.accent;
        border = bg;
        fg = s.disabled ? t.faint : t.onAccent;
    } else if (s.active) {
        bg = s.pressed ? mix(t.accentSoft, t.accent, .12f) : t.accentSoft;
        border = mix(t.accentSoft, t.accent, t.dark ? .35f : .28f);
        fg = t.accentText;
    } else if (s.disabled) {
        border = t.divider;
        fg = t.faint;
    } else if (s.pressed) {
        bg = t.pressed;
        border = t.borderStrong;
    } else if (s.hovered) {
        bg = t.hover;
        border = t.borderStrong;
    }
    p.round(b, 6, bg, border);
    if (s.focused) // keyboard focus ring
        p.round(b, 6, {0, 0, 0, 0}, s.primary ? mix(t.accentPressed, t.text, .25f) : t.accent, 2);
    p.text(b.inset(3, 0), text, font, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
void paintCheckBox(Painter& p, Box b, std::wstring_view text, HFONT font, const Theme& t, Color surface,
                   const ControlState& s) {
    p.fill(b, surface);
    int size = 18;
    Box box{b.x + 1, b.y + (b.h - size) / 2, size, size};
    if (s.checked)
        p.round(box, 4.5f, s.disabled ? t.faint : s.hovered ? t.accentHover : t.accent);
    else
        p.round(box, 4.5f, s.disabled ? t.sunken : t.raised, s.disabled ? t.border : s.hovered ? t.accent : t.borderStrong,
                1.25f);
    if (s.checked)
        p.icon(Icon::Check, box.inset(3, 3), t.onAccent);
    if (s.focused) // keyboard focus ring
        p.round(box.inset(-3, -3), 7, {0, 0, 0, 0}, alpha(t.accent, .6f), 2);
    p.text({box.right() + 8, b.y, std::max(0, b.right() - box.right() - 8), b.h}, text, font,
           s.disabled ? t.faint : t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
Color fieldFill(const Theme& t, Color surface) {
    bool window = surface.r == t.window.r && surface.g == t.window.g && surface.b == t.window.b;
    return window ? t.panel : t.sunken;
}
void paintFieldFrame(Painter& p, Box b, const Theme& t, Color surface, const ControlState& s, float radius) {
    p.fill(b, surface);
    // The fill matches the native client area (App::colorControl returns the same colour).
    // Inputs keep a clearly visible outline so they read as editable.
    Color border = s.disabled ? t.border
                   : s.focused ? t.accent
                   : s.hovered ? mix(t.borderStrong, t.muted, .35f)
                               : t.borderStrong;
    p.round(b, radius, fieldFill(t, surface), border, s.focused ? 1.5f : 1);
}
void paintComboFace(Painter& p, Box b, std::wstring_view text, HFONT font, const Theme& t, Color surface,
                    const ControlState& s) {
    paintFieldFrame(p, b, t, surface, s);
    int ic = 14;
    p.icon(Icon::ChevronDown, {b.right() - ic - 9, b.y + (b.h - ic) / 2, ic, ic}, s.disabled ? t.faint : t.muted);
    p.text({b.x + 10, b.y, std::max(0, b.w - ic - 26), b.h}, text, font, s.disabled ? t.faint : t.text,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
void paintListItem(Painter& p, Box b, std::wstring_view text, HFONT font, const Theme& t, const ControlState& s,
                   Color fill) {
    p.fill(b, fill);
    if (s.active) {
        p.round({b.x + 3, b.y + 1, b.w - 6, b.h - 2}, 5, t.accentSoft);
        p.round({b.x + 4, b.y + 6, 3, std::max(2, b.h - 12)}, 1.5f, t.accent);
    }
    p.text({b.x + 12, b.y, std::max(0, b.w - 16), b.h}, text, font,
           s.disabled ? t.faint : s.active ? t.accentText : t.text,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_EXPANDTABS);
}

namespace {
struct MenuMetrics {
    int padding, item, separator, left, right, gap, width;
};
MenuMetrics menuMetrics(Painter& p, const std::vector<MenuItem>& items, const Fonts& f) {
    float s = f.scale;
    MenuMetrics m{dp(6, s), dp(30, s), dp(9, s), dp(34, s), dp(16, s), dp(36, s), dp(188, s)};
    for (const auto& item : items) {
        if (item.separator)
            continue;
        int width = m.left + p.measure(item.text, f.body) + m.right;
        if (!item.shortcut.empty())
            width += m.gap + p.measure(item.shortcut, f.small);
        m.width = std::max(m.width, width);
    }
    return m;
}
std::vector<Box> menuRows(const MenuMetrics& m, const std::vector<MenuItem>& items) {
    std::vector<Box> rows;
    int y = m.padding;
    for (const auto& item : items) {
        int h = item.separator ? m.separator : m.item;
        rows.push_back({m.padding, y, m.width - 2 * m.padding, h});
        y += h;
    }
    return rows;
}
LRESULT CALLBACK popupProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (m == WM_NCHITTEST && GetWindowLongPtrW(h, GWLP_USERDATA) == 1)
        return HTTRANSPARENT;
    return DefWindowProcW(h, m, w, l);
}
const wchar_t* popupClass() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = popupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"EchoUiPopup";
        RegisterClassW(&wc);
        registered = true;
    }
    return L"EchoUiPopup";
}
// Pushes opaque `content` to a per-pixel-alpha window with anti-aliased corners and a soft shadow.
void upload(HWND window, POINT at, Painter& content, float radius, int margin) {
    int w = content.width(), h = content.height(), W = w + 2 * margin, H = h + 2 * margin;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = W;
    info.bmiHeader.biHeight = -H;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    void* bits{};
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return;
    }
    auto old = SelectObject(dc, bitmap);
    auto* out = static_cast<std::uint32_t*>(bits);
    const auto* in = content.pixels();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float px = x + .5f - margin, py = y + .5f - margin;
            float body = clamp01(.5f - roundBoxDistance(px, py, 0, 0, float(w), float(h), radius));
            float shade = roundBoxDistance(px, py - margin * .3f, 0, 0, float(w), float(h), radius);
            float falloff = shade <= 0 ? 1 : std::max(0.0f, 1 - shade / (margin * .85f));
            float a = body + falloff * falloff * .2f * (1 - body);
            std::uint32_t c = 0;
            int sx = x - margin, sy = y - margin;
            if (body > 0 && sx >= 0 && sy >= 0 && sx < w && sy < h)
                c = in[sy * w + sx];
            auto channel = [&](int shift) { return std::uint32_t(((c >> shift) & 255) * body + .5f) << shift; };
            out[y * W + x] = (std::uint32_t(a * 255 + .5f) << 24) | channel(16) | channel(8) | channel(0);
        }
    POINT source{0, 0}, destination{at.x - margin, at.y - margin};
    SIZE size{W, H};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, screen, &destination, &size, dc, &source, 0, &blend, ULW_ALPHA);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}
RECT workArea(POINT at) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &info);
    return info.rcWork;
}
} // namespace

Box menuSize(Painter& p, const std::vector<MenuItem>& items, const Fonts& f) {
    auto m = menuMetrics(p, items, f);
    auto rows = menuRows(m, items);
    return {0, 0, m.width, (rows.empty() ? m.padding : rows.back().bottom()) + m.padding};
}
void paintMenu(Painter& p, const std::vector<MenuItem>& items, int hot, const Theme& t, const Fonts& f) {
    float s = f.scale;
    auto m = menuMetrics(p, items, f);
    auto rows = menuRows(m, items);
    Box full{0, 0, p.width(), p.height()};
    p.fill(full, t.raised);
    p.round(full, dp(8, s), t.raised, t.dark ? t.borderStrong : t.border);
    for (int i = 0; i < int(items.size()); ++i) {
        const auto& item = items[i];
        Box r = rows[i];
        if (item.separator) {
            p.fill({r.x + dp(8, s), r.y + r.h / 2, r.w - dp(16, s), 1}, t.divider);
            continue;
        }
        bool active = i == hot && !item.disabled;
        if (active)
            p.round(r, dp(5, s), t.accent);
        Color fg = item.disabled ? t.faint : active ? t.onAccent : t.text;
        if (item.checked) {
            int size = dp(14, s);
            p.icon(Icon::Check, {r.x + dp(10, s), r.y + (r.h - size) / 2, size, size},
                   active ? t.onAccent : t.accent);
        }
        p.text({r.x + m.left - m.padding, r.y, r.w, r.h}, item.text, f.body, fg);
        if (!item.shortcut.empty())
            p.text({r.x, r.y, r.w - m.right + m.padding, r.h}, item.shortcut, f.small,
                   item.disabled ? t.faint : active ? t.onAccent : t.muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}
int popupMenu(HWND owner, POINT screen, const std::vector<MenuItem>& items, bool dark, const Fonts& f,
              const std::function<int(POINT)>& menuBarHit, int current, int* switchTo, bool keyboard) {
    if (switchTo)
        *switchTo = -1;
    if (items.empty())
        return 0;
    const auto& t = theme(dark);
    Painter content;
    Box size = menuSize(content, items, f);
    content.resize(size.w, size.h);
    auto rows = menuRows(menuMetrics(content, items, f), items);
    RECT work = workArea(screen);
    screen.x = std::max(int(work.left), std::min(int(screen.x), int(work.right) - size.w));
    screen.y = std::max(int(work.top), std::min(int(screen.y), int(work.bottom) - size.h));
    int margin = dp(14, f.scale);
    HWND popup = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                 popupClass(), L"", WS_POPUP, 0, 0, 0, 0, owner, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    if (!popup)
        return 0;
    int hot = -1, result = 0;
    auto redraw = [&] {
        paintMenu(content, items, hot, t, f);
        upload(popup, screen, content, float(dp(8, f.scale)), margin);
    };
    auto itemAt = [&](POINT p) {
        for (int i = 0; i < int(rows.size()); ++i)
            if (!items[i].separator && rows[i].contains(p.x - screen.x, p.y - screen.y))
                return i;
        return -1;
    };
    auto step = [&](int direction) {
        for (int n = 0; n < int(items.size()); ++n) {
            hot = hot < 0 ? (direction > 0 ? 0 : int(items.size()) - 1)
                          : (hot + direction + int(items.size())) % int(items.size());
            if (!items[hot].separator && !items[hot].disabled)
                break;
        }
        redraw();
    };
    if (keyboard)
        step(1); // also paints
    else
        redraw();
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    SetCapture(popup);
    bool done = false;
    MSG msg;
    while (!done && GetMessageW(&msg, nullptr, 0, 0)) {
        POINT cursor{};
        GetCursorPos(&cursor);
        RECT inside{screen.x, screen.y, screen.x + size.w, screen.y + size.h};
        switch (msg.message) {
        case WM_MOUSEMOVE: {
            int next = itemAt(cursor);
            if (next >= 0 && items[next].disabled)
                next = -1;
            if (next != hot && (next >= 0 || !PtInRect(&inside, cursor))) {
                hot = next;
                redraw();
            }
            if (menuBarHit) {
                int bar = menuBarHit(cursor);
                if (bar >= 0 && bar != current) {
                    if (switchTo)
                        *switchTo = bar;
                    done = true;
                }
            }
            continue;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (!PtInRect(&inside, cursor)) {
                done = true;
                if (menuBarHit && switchTo) {
                    int bar = menuBarHit(cursor);
                    if (bar >= 0 && bar != current)
                        *switchTo = bar;
                }
            }
            continue;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            int chosen = itemAt(cursor);
            if (chosen >= 0 && !items[chosen].disabled) {
                result = items[chosen].command;
                done = true;
            }
            continue;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (msg.wParam == VK_ESCAPE || msg.wParam == VK_MENU)
                done = true;
            else if (msg.wParam == VK_UP)
                step(-1);
            else if (msg.wParam == VK_DOWN)
                step(1);
            else if ((msg.wParam == VK_RETURN || msg.wParam == VK_SPACE) && hot >= 0) {
                result = items[hot].command;
                done = true;
            } else if ((msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT) && switchTo) {
                *switchTo = msg.wParam == VK_LEFT ? -2 : -3;
                done = true;
            }
            continue;
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
            continue;
        default:
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (GetCapture() != popup)
            done = true;
    }
    if (msg.message == WM_QUIT)
        PostQuitMessage(int(msg.wParam));
    if (GetCapture() == popup)
        ReleaseCapture();
    DestroyWindow(popup);
    return result;
}

Tooltip::~Tooltip() {
    if (window_ && IsWindow(window_))
        DestroyWindow(window_);
}
void Tooltip::show(HWND owner, POINT screen, const std::wstring& text, bool dark, const Fonts& f) {
    if (text.empty()) {
        hide();
        return;
    }
    const auto& t = theme(dark);
    float s = f.scale;
    Painter content;
    content.resize(1, 1);
    RECT bounds{0, 0, dp(300, s), 0};
    auto old = SelectObject(content.dc(), f.small);
    DrawTextW(content.dc(), text.c_str(), int(text.size()), &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(content.dc(), old);
    int padX = dp(10, s), padY = dp(6, s);
    int w = bounds.right + 2 * padX, h = bounds.bottom + 2 * padY;
    content.resize(w, h);
    content.fill({0, 0, w, h}, t.tooltip);
    content.text({padX, padY, bounds.right + 1, bounds.bottom}, text, f.small, t.tooltipText,
                 DT_LEFT | DT_TOP | DT_WORDBREAK);
    RECT work = workArea(screen);
    screen.x = std::max(int(work.left), std::min(int(screen.x), int(work.right) - w));
    screen.y = std::max(int(work.top), std::min(int(screen.y), int(work.bottom) - h));
    if (!window_ || !IsWindow(window_)) {
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                                      WS_EX_TRANSPARENT,
                                  popupClass(), L"", WS_POPUP, 0, 0, 0, 0, owner, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
        if (!window_)
            return;
        SetWindowLongPtrW(window_, GWLP_USERDATA, 1);
    }
    text_ = text;
    upload(window_, screen, content, float(dp(6, s)), dp(8, s));
    ShowWindow(window_, SW_SHOWNOACTIVATE);
}
void Tooltip::hide() {
    if (window_ && IsWindow(window_))
        ShowWindow(window_, SW_HIDE);
    text_.clear();
}
bool Tooltip::visible() const {
    return window_ && IsWindowVisible(window_);
}
} // namespace echo::ui
