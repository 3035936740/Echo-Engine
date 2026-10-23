#include "echo/focus.hpp"
#include "echo/animations.hpp"
#include "echo/simulation.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>
namespace echo {
namespace {
bool range(float x, float a, float b) {
    return std::isfinite(x) && x >= a && x <= b;
}
float cross(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
Vec2 rotate(Vec2 p, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}
Vec2 sub(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}
bool circle(Vec2 a, Vec2 b, float r, bool touching, float& t) {
    double dx = double(b.x) - a.x, dy = double(b.y) - a.y,
           c = double(a.x) * a.x + double(a.y) * a.y - double(r) * r;
    if (c < 0 || (touching && c == 0)) {
        t = 0;
        return true;
    }
    double qa = dx * dx + dy * dy, qb = 2 * (a.x * dx + a.y * dy), disc = qb * qb - 4 * qa * c;
    if (qa < 1e-20 || disc < 0 || (!touching && disc <= 0))
        return false;
    double enter = (-qb - std::sqrt(std::max(0., disc))) / (2 * qa),
           leave = (-qb + std::sqrt(std::max(0., disc))) / (2 * qa);
    if (leave < 0 || enter > 1 || (!touching && (leave <= 0 || enter >= 1)))
        return false;
    t = float(std::max(0., enter));
    return true;
}
bool polygon(Vec2 a, Vec2 b, const auto& points, bool touching, float& t) {
    if (points.count < 3)
        return false;
    double area = 0;
    for (std::uint32_t i = 0; i < points.count; ++i) {
        auto p = points[i], q = points[(i + 1) % points.count];
        area += double(p.x) * q.y - double(p.y) * q.x;
    }
    float sign = area >= 0 ? 1.f : -1.f;
    double lo = 0, hi = 1;
    for (std::uint32_t i = 0; i < points.count; ++i) {
        auto p = points[i], q = points[(i + 1) % points.count];
        double u = sign * cross(p, q, a), v = sign * cross(p, q, b), delta = v - u;
        if (std::abs(delta) < 1e-12) {
            if (u < 0 || (!touching && u <= 0))
                return false;
            continue;
        }
        double cut = -u / delta;
        if (delta > 0)
            lo = std::max(lo, cut);
        else
            hi = std::min(hi, cut);
        if (lo > hi || (!touching && lo >= hi))
            return false;
    }
    t = float(lo);
    return lo <= 1 && hi >= 0;
}
bool capsule(Vec2 a, Vec2 b, Vec2 p, Vec2 q, float radius, bool touching, float& t) {
    float best = 2, candidate;
    auto d = sub(q, p);
    float length = std::hypot(d.x, d.y);
    if (circle(sub(a, p), sub(b, p), radius, touching, candidate))
        best = candidate;
    if (circle(sub(a, q), sub(b, q), radius, touching, candidate))
        best = std::min(best, candidate);
    if (length > 1e-8f) {
        Vec2 n{-d.y / length * radius, d.x / length * radius};
        FixedList<Vec2, 32> poly;
        poly.push({p.x + n.x, p.y + n.y});
        poly.push({q.x + n.x, q.y + n.y});
        poly.push({q.x - n.x, q.y - n.y});
        poly.push({p.x - n.x, p.y - n.y});
        if (polygon(a, b, poly, touching, candidate))
            best = std::min(best, candidate);
    }
    t = best;
    return best <= 1;
}
FixedList<Vec2, 64> hull(std::array<Vec2, 128>& v, std::size_t count) {
    std::sort(v.begin(), v.begin() + count,
              [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::array<Vec2, 256> h{};
    std::size_t k = 0;
    for (std::size_t i = 0; i < count; ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], v[i]) <= 0)
            --k;
        h[k++] = v[i];
    }
    auto lower = k;
    for (std::size_t i = count - 1; i-- > 0;) {
        while (k > lower && cross(h[k - 2], h[k - 1], v[i]) <= 0)
            --k;
        h[k++] = v[i];
    }
    FixedList<Vec2, 64> out;
    for (std::size_t i = 0; i + 1 < k; ++i)
        out.push(h[i]);
    return out;
}
bool sweepRegion(const KinoState& k, const HurtConfig& config, const HurtRegion& region, Vec2 a, Vec2 b,
                 float radius, bool box, float& t, bool proximity = false) {
    bool touching = proximity || config.touching;
    auto center = hurtCenter(k, config, region);
    if (region.shape == HurtShape::Circle && box) {
        Rect square{center.x - radius, center.y - radius, radius * 2, radius * 2};
        float best = 2, candidate;
        FixedList<Vec2, 32> vertices;
        vertices.push({square.x, square.y});
        vertices.push({square.x + square.w, square.y});
        vertices.push({square.x + square.w, square.y + square.h});
        vertices.push({square.x, square.y + square.h});
        if (polygon(a, b, vertices, touching, candidate))
            best = candidate;
        for (std::uint32_t i = 0; i < 4; ++i)
            if (capsule(a, b, vertices[i], vertices[(i + 1) % 4], region.radius, touching, candidate))
                best = std::min(best, candidate);
        t = best;
        return best <= 1;
    }
    if (region.shape == HurtShape::Circle && !box)
        return circle(sub(a, center), sub(b, center), region.radius + radius, touching, t);
    if (region.shape == HurtShape::Capsule && !box) {
        bool vertical = region.height >= region.width;
        float r = std::min(region.width, region.height) * .5f;
        float length = (std::max(region.width, region.height) * .5f - r);
        Vec2 axis = rotate(vertical ? Vec2{0, length} : Vec2{length, 0}, region.angle);
        if (config.mirror && k.facing < 0)
            axis.x = -axis.x;
        return capsule(a, b, {center.x - axis.x, center.y - axis.y},
                       {center.x + axis.x, center.y + axis.y}, r + radius, touching, t);
    }
    auto poly = hurtPolygon(k, config, region);
    if (box && radius > 0) {
        std::array<Vec2, 128> cloud{};
        std::size_t n = 0;
        for (auto v : poly)
            for (float x : {-radius, radius})
                for (float y : {-radius, radius})
                    cloud[n++] = {v.x + x, v.y + y};
        auto expanded = hull(cloud, n);
        return polygon(a, b, expanded, touching, t);
    }
    float best = 2, candidate;
    if (polygon(a, b, poly, touching, candidate))
        best = candidate;
    if (!box && radius > 0)
        for (std::uint32_t i = 0; i < poly.count; ++i)
            if (capsule(a, b, poly[i], poly[(i + 1) % poly.count], radius, touching, candidate))
                best = std::min(best, candidate);
    t = best;
    return best <= 1;
}
} // namespace
void validateFocus(const FocusConfig& c) {
    if (int(c.mode) > 2 || int(c.when) > 3 || !range(c.speedRatio, .01f, 1) || c.key < 1 ||
        c.key > 254 || c.gateSwitch < -1 || c.gateSwitch >= 64 || c.map < -1 || c.map > 7)
        throw std::runtime_error("Focus: ratio .01..1, key 1..254, switch -1..63, map -1..7");
}
void validateHurt(const HurtConfig& c) {
    if (int(c.source) > 2 || int(c.method) > 1 || int(c.show) > 4 || c.count < 1 || c.count > 8 ||
        c.style < 0 || c.style > 3 || c.showKey < 1 || c.showKey > 254 ||
        !range(c.displayScale, .25f, 8) || !range(c.opacity, 0, 1) || !range(c.outline, .25f, 8))
        throw std::runtime_error("Invalid hurt configuration / capacity (1..8)");
    for (auto v : c.color)
        if (!range(v, 0, 1))
            throw std::runtime_error("Hurt color: 0..1");
    for (std::uint32_t i = 0; i < c.count; ++i) {
        auto& r = c.regions[i];
        if (int(r.shape) > 4 || r.mask > 7 || !range(r.anchorX, -2, 3) || !range(r.anchorY, -2, 3) ||
            !range(r.offsetX, -4096, 4096) || !range(r.offsetY, -4096, 4096) ||
            !range(r.radius, .01f, 512) || !range(r.width, .02f, 1024) || !range(r.height, .02f, 1024) ||
            !range(r.angle, -100, 100) || r.vertices.count > 16)
            throw std::runtime_error("Invalid hurt region");
        if (r.shape == HurtShape::Polygon) {
            if (r.vertices.count < 3)
                throw std::runtime_error("Polygon needs 3..16 convex vertices");
            float sign = 0;
            for (std::uint32_t j = 0; j < r.vertices.count; ++j) {
                auto a = r.vertices[j], b = r.vertices[(j + 1) % r.vertices.count],
                     d = r.vertices[(j + 2) % r.vertices.count];
                if (!range(a.x, -512, 512) || !range(a.y, -512, 512))
                    throw std::runtime_error("Polygon points -512..512");
                float v = cross(a, b, d);
                if (std::abs(v) < .00001f)
                    throw std::runtime_error("Degenerate polygon edge");
                if (sign && v * sign <= 0)
                    throw std::runtime_error("Polygon must be convex and ordered");
                sign = v;
            }
            // A convex turn test alone would admit a star; every vertex must lie inside every edge.
            for (std::uint32_t j = 0; j < r.vertices.count; ++j)
                for (auto v : r.vertices)
                    if (cross(r.vertices[j], r.vertices[(j + 1) % r.vertices.count], v) * sign <
                        -.00001f)
                        throw std::runtime_error("Self-intersecting polygon");
        }
    }
}
std::string writeFocus(const FocusConfig& c) {
    validateFocus(c);
    std::ostringstream s;
    s << std::setprecision(9) << "1 " << c.enabled << ' ' << int(c.mode) << ' ' << int(c.when) << ' '
      << c.speedRatio << ' ' << c.key << ' ' << c.gateSwitch << ' ' << c.map << ' ' << c.ground << ' '
      << c.air << ' ' << c.instant << ' ' << c.showIndicator;
    return s.str();
}
FocusConfig readFocus(const std::string& text) {
    FocusConfig c;
    int version, mode, when;
    std::istringstream s(text);
    s >> version >> c.enabled >> mode >> when >> c.speedRatio >> c.key >> c.gateSwitch >> c.map >>
        c.ground >> c.air >> c.instant >> c.showIndicator;
    bool ok = bool(s);
    s >> std::ws;
    if (!ok || !s.eof() || version != 1 || mode < 0 || mode > 2 || when < 0 || when > 3)
        throw std::runtime_error("Invalid focus settings");
    c.mode = FocusMode(mode);
    c.when = FocusWhen(when);
    validateFocus(c);
    return c;
}
std::string writeHurt(const HurtConfig& c) {
    validateHurt(c);
    std::ostringstream s;
    s << std::setprecision(9) << "1 " << int(c.source) << ' ' << int(c.method) << ' ' << int(c.show)
      << ' ' << c.touching << ' ' << c.mirror << ' ' << c.projectileBox << ' ' << c.count << ' '
      << c.showKey << ' ' << c.style << ' ' << c.displayScale << ' ' << c.opacity << ' ' << c.outline;
    for (auto v : c.color)
        s << ' ' << v;
    for (std::uint32_t i = 0; i < c.count; ++i) {
        auto& r = c.regions[i];
        s << ' ' << int(r.shape) << ' ' << r.enabled << ' ' << r.focusOnly << ' ' << int(r.mask) << ' '
          << r.anchorX << ' ' << r.anchorY << ' ' << r.offsetX << ' ' << r.offsetY << ' ' << r.radius
          << ' ' << r.width << ' ' << r.height << ' ' << r.angle << ' ' << r.vertices.count;
        for (auto p : r.vertices)
            s << ' ' << p.x << ' ' << p.y;
    }
    return s.str();
}
HurtConfig readHurt(const std::string& text) {
    if (text.size() > 8192)
        throw std::runtime_error("Hurt settings max 8 KiB");
    HurtConfig c;
    int version, source, method, show;
    std::istringstream s(text);
    s >> version >> source >> method >> show >> c.touching >> c.mirror >> c.projectileBox >> c.count >>
        c.showKey >> c.style >> c.displayScale >> c.opacity >> c.outline;
    for (auto& v : c.color)
        s >> v;
    if (!s || version != 1 || source < 0 || source > 2 || method < 0 || method > 1 || show < 0 ||
        show > 4 || c.count < 1 || c.count > 8)
        throw std::runtime_error("Invalid hurt settings");
    c.source = HurtSource(source);
    c.method = HurtMethod(method);
    c.show = HurtShow(show);
    for (std::uint32_t i = 0; i < c.count; ++i) {
        auto& r = c.regions[i];
        int shape, mask, n;
        s >> shape >> r.enabled >> r.focusOnly >> mask >> r.anchorX >> r.anchorY >> r.offsetX >>
            r.offsetY >> r.radius >> r.width >> r.height >> r.angle >> n;
        if (!s || shape < 0 || shape > 4 || mask < 0 || mask > 7 || n < 0 || n > 16)
            throw std::runtime_error("Invalid hurt region record");
        r.shape = HurtShape(shape);
        r.mask = std::uint8_t(mask);
        for (int j = 0; j < n; ++j) {
            Vec2 p;
            s >> p.x >> p.y;
            r.vertices.push(p);
        }
    }
    bool ok = bool(s);
    s >> std::ws;
    if (!ok || !s.eof())
        throw std::runtime_error("Truncated / trailing hurt settings");
    validateHurt(c);
    return c;
}
void stepFocus(WorldState& w, bool held, bool show, bool controlAllowed) {
    auto& c = w.focus;
    auto& k = w.kino;
    bool rise = held && !k.previousFocus;
    k.previousFocus = held;
    k.showHurtHeld = show;
    bool permit = c.enabled && controlAllowed && (k.grounded ? c.ground : c.air) &&
                  (c.map < 0 || c.map == w.activeMap) &&
                  (c.gateSwitch < 0 || w.events.switches[c.gateSwitch]);
    if (c.when == FocusWhen::Bullets)
        permit = permit && w.bullets.count > 0;
    if (c.when == FocusWhen::Boss)
        permit = permit && w.aster.enabled && w.aster.hp > 0;
    if (c.when == FocusWhen::Switch)
        permit = permit && c.gateSwitch >= 0;
    if (!c.enabled || !controlAllowed)
        k.focusLatched = false;
    if (c.mode == FocusMode::Toggle && rise && permit)
        k.focusLatched = !k.focusLatched;
    k.focusActive = permit && (c.mode == FocusMode::Automatic ||
                               (c.mode == FocusMode::Toggle ? k.focusLatched : held));
}
bool showHurt(const WorldState& w) {
    switch (w.hurt.show) {
    case HurtShow::Never:
        return false;
    case HurtShow::Always:
        return true;
    case HurtShow::Focus:
        return w.kino.focusActive;
    case HurtShow::Key:
        return w.kino.showHurtHeld;
    case HurtShow::Danger:
        return w.bullets.count > 0 || w.state == TimeState::Hit;
    }
    return false;
}
HurtRegion effectiveHurt(const KinoState& k, const HurtConfig& config, std::uint32_t index) {
    if (config.source == HurtSource::Custom)
        return config.regions[index];
    auto r = config.regions[0];
    r.anchorX = r.anchorY = 0;
    r.offsetX = r.offsetY = 0;
    r.angle = 0;
    if (config.source == HurtSource::Animation && k.animation.hurt.enabled) {
        auto b = k.animation.hurt;
        r.shape = HurtShape::Rectangle;
        r.width = b.w;
        r.height = b.h;
        r.offsetX = b.x + b.w * .5f;
        r.offsetY = b.y + b.h * .5f;
    } else if (config.source == HurtSource::Whole) {
        r.shape = HurtShape::Rectangle;
        r.width = k.width;
        r.height = k.height;
        r.offsetX = r.width * .5f;
        r.offsetY = r.height * .5f;
    } else {
        r = config.regions[0];
    }
    return r;
}
HurtRegion effectiveHurt(const WorldState& w, std::uint32_t index) {
    return effectiveHurt(w.kino, w.hurt, index);
}
Vec2 hurtCenter(const KinoState& k, const HurtConfig& c, const HurtRegion& r) {
    float x = k.width * r.anchorX + r.offsetX;
    if (c.mirror && k.facing < 0)
        x = k.width - x;
    return {k.x + x, k.y + k.height * r.anchorY + r.offsetY};
}
FixedList<Vec2, 32> hurtPolygon(const KinoState& k, const HurtConfig& c, const HurtRegion& r) {
    FixedList<Vec2, 32> p;
    float w = r.width * .5f, h = r.height * .5f;
    if (r.shape == HurtShape::Rectangle) {
        p.push({-w, -h});
        p.push({w, -h});
        p.push({w, h});
        p.push({-w, h});
    } else if (r.shape == HurtShape::Polygon) {
        for (auto v : r.vertices)
            p.push(v);
    } else
        for (int i = 0; i < 32; ++i) {
            float a = 2 * std::numbers::pi_v<float> * i / 32;
            Vec2 v{std::cos(a) * w, std::sin(a) * h};
            if (r.shape == HurtShape::Circle)
                v = {std::cos(a) * r.radius, std::sin(a) * r.radius};
            if (r.shape == HurtShape::Capsule) {
                float radius = std::min(w, h);
                v = {std::cos(a) * radius, std::sin(a) * radius};
                if (h >= w)
                    v.y += (i < 16 ? 1.f : -1.f) * (h - radius);
                else
                    v.x += (std::cos(a) >= 0 ? 1.f : -1.f) * (w - radius);
            }
            p.push(v);
        }
    auto center = hurtCenter(k, c, r);
    for (auto& v : p) {
        v = rotate(v, r.angle);
        if (c.mirror && k.facing < 0)
            v.x = -v.x;
        v.x += center.x;
        v.y += center.y;
    }
    return p;
}
Rect hurtBounds(const KinoState& k, const HurtConfig& c, const HurtRegion& r) {
    auto center = hurtCenter(k, c, r);
    if (r.shape == HurtShape::Circle)
        return {center.x - r.radius, center.y - r.radius, r.radius * 2, r.radius * 2};
    float cosine = std::abs(std::cos(r.angle)), sine = std::abs(std::sin(r.angle)), x = 0, y = 0;
    if (r.shape == HurtShape::Rectangle) {
        x = cosine * r.width * .5f + sine * r.height * .5f;
        y = sine * r.width * .5f + cosine * r.height * .5f;
    } else if (r.shape == HurtShape::Ellipse) {
        x = std::hypot(cosine * r.width * .5f, sine * r.height * .5f);
        y = std::hypot(sine * r.width * .5f, cosine * r.height * .5f);
    } else if (r.shape == HurtShape::Capsule) {
        float radius = std::min(r.width, r.height) * .5f,
              length = std::max(r.width, r.height) * .5f - radius;
        x = radius + length * (r.width >= r.height ? cosine : sine);
        y = radius + length * (r.width >= r.height ? sine : cosine);
    } else {
        auto p = hurtPolygon(k, c, r);
        if (!p.count)
            return {};
        float left = p[0].x, top = p[0].y, right = left, bottom = top;
        for (auto v : p) {
            left = std::min(left, v.x);
            top = std::min(top, v.y);
            right = std::max(right, v.x);
            bottom = std::max(bottom, v.y);
        }
        return {left, top, right - left, bottom - top};
    }
    return {center.x - x, center.y - y, x * 2, y * 2};
}
bool playerHurtSweep(const WorldState& w, const KinoState& old, const HurtConfig& oldConfig, Vec2 from,
                     Vec2 to, float radius, bool box, std::uint8_t mask, float& entry,
                     std::uint32_t* candidates, bool proximity) {
    // The common one-circle query avoids copying the 16-vertex authoring payload per bullet.
    bool singleCircle = (w.hurt.source == HurtSource::Custom && w.hurt.count == 1) ||
                        (w.hurt.source == HurtSource::Animation && !w.kino.animation.hurt.enabled);
    if (singleCircle && w.hurt.regions[0].shape == HurtShape::Circle) {
        const auto& r = w.hurt.regions[0];
        entry = 2;
        if (!r.enabled || !(r.mask & mask) || (r.focusOnly && !w.kino.focusActive))
            return false;
        auto now = hurtCenter(w.kino, w.hurt, r);
        Vec2 before;
        if (oldConfig.source == HurtSource::Custom ||
            (oldConfig.source == HurtSource::Animation && !old.animation.hurt.enabled))
            before = hurtCenter(old, oldConfig, oldConfig.regions[0]);
        else
            before = hurtCenter(old, oldConfig, effectiveHurt(old, oldConfig, 0));
        Vec2 a{from.x - before.x, from.y - before.y}, b{to.x - now.x, to.y - now.y};
        if (w.hurt.method == HurtMethod::Overlap && !proximity)
            a = b;
        float combined = r.radius + radius;
        if (std::max(a.x, b.x) < -combined || std::min(a.x, b.x) > combined ||
            std::max(a.y, b.y) < -combined || std::min(a.y, b.y) > combined)
            return false;
        if (candidates)
            ++*candidates;
        if (!box)
            return circle(a, b, combined, proximity || w.hurt.touching, entry);
        return sweepRegion(w.kino, w.hurt, r, {a.x + now.x, a.y + now.y}, to, radius, true, entry,
                           proximity);
    }
    float best = 2;
    std::uint32_t count = w.hurt.source == HurtSource::Custom ? w.hurt.count : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto r = effectiveHurt(w, i);
        if (!r.enabled || !(r.mask & mask) || (r.focusOnly && !w.kino.focusActive))
            continue;
        // Use relative motion, including a changed animation offset. Current geometry governs this tick.
        auto now = hurtCenter(w.kino, w.hurt, r);
        auto oldRegion = i < oldConfig.count ? effectiveHurt(old, oldConfig, i) : r;
        auto before = hurtCenter(old, oldConfig, oldRegion);
        Vec2 a{from.x + now.x - before.x, from.y + now.y - before.y};
        if (w.hurt.method == HurtMethod::Overlap && !proximity)
            a = to;
        Rect bounds = hurtBounds(w.kino, w.hurt, r);
        float expand = radius;
        if (std::max(a.x, to.x) < bounds.x - expand ||
            std::min(a.x, to.x) > bounds.x + bounds.w + expand ||
            std::max(a.y, to.y) < bounds.y - expand ||
            std::min(a.y, to.y) > bounds.y + bounds.h + expand)
            continue;
        if (candidates)
            ++*candidates;
        float t;
        if (sweepRegion(w.kino, w.hurt, r, a, to, radius, box, t, proximity))
            best = std::min(best, t);
    }
    entry = best;
    return best <= 1;
}
bool playerHurtRect(const WorldState& w, Rect a, std::uint8_t mask) {
    // Axis-aligned attack rectangle Minkowski expansion: no corner false positives for circles.
    std::uint32_t count = w.hurt.source == HurtSource::Custom ? w.hurt.count : 1;
    Vec2 center{a.x + a.w * .5f, a.y + a.h * .5f};
    for (std::uint32_t i = 0; i < count; ++i) {
        auto r = effectiveHurt(w, i);
        if (!r.enabled || !(r.mask & mask) || (r.focusOnly && !w.kino.focusActive))
            continue;
        if (r.shape == HurtShape::Circle) {
            auto p = hurtCenter(w.kino, w.hurt, r);
            float dx = std::max(std::abs(p.x - center.x) - a.w * .5f, 0.f),
                  dy = std::max(std::abs(p.y - center.y) - a.h * .5f, 0.f), d = dx * dx + dy * dy;
            if (w.hurt.touching ? d <= r.radius * r.radius : d < r.radius * r.radius)
                return true;
            continue;
        }
        auto poly = hurtPolygon(w.kino, w.hurt, r);
        std::array<Vec2, 128> cloud{};
        std::size_t n = 0;
        for (auto v : poly)
            for (float x : {-a.w * .5f, a.w * .5f})
                for (float y : {-a.h * .5f, a.h * .5f})
                    cloud[n++] = {v.x + x, v.y + y};
        auto expanded = hull(cloud, n);
        float t;
        if (polygon(center, center, expanded, w.hurt.touching, t))
            return true;
    }
    return false;
}
} // namespace echo
