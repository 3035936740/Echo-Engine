#include "echo/collision.hpp"
#include "echo/platform_features.hpp"
#include <algorithm>
#include <cmath>
#include "echo/animations.hpp"
#include "echo/objects.hpp"
#include <sstream>
#include <stdexcept>
namespace echo {
int colliderVertices(const StaticCollider& c, std::array<Vec2, 16>& out) {
    auto r = c.bounds;
    if (c.kind == ColliderKind::Polygon) {
        out = c.points.values;
        return int(c.points.count);
    }
    if (c.kind == ColliderKind::Triangle) {
        out[0] = {r.x + r.w * .5f, r.y};
        out[1] = {r.x + r.w, r.y + r.h};
        out[2] = {r.x, r.y + r.h};
        return 3;
    }
    out[0] = {r.x, r.y};
    out[1] = {r.x + r.w, r.y};
    out[2] = {r.x + r.w, r.y + r.h};
    out[3] = {r.x, r.y + r.h};
    return 4;
}
static bool polygonSweep(Rect box, Vec2 delta, const Vec2* points, int count, float& entry) {
    Vec2 center{box.x + box.w * .5f, box.y + box.h * .5f};
    float near = 0, far = 1;
    auto axis = [&](Vec2 n) {
        float length = std::hypot(n.x, n.y);
        if (length < 1e-6f)
            return true;
        n.x /= length;
        n.y /= length;
        float min = points[0].x * n.x + points[0].y * n.y, max = min;
        for (int i = 1; i < count; ++i) {
            float p = points[i].x * n.x + points[i].y * n.y;
            min = std::min(min, p);
            max = std::max(max, p);
        }
        float middle = center.x * n.x + center.y * n.y,
              extent = std::abs(n.x) * box.w * .5f + std::abs(n.y) * box.h * .5f;
        float speed = delta.x * n.x + delta.y * n.y, lo = middle - extent, hi = middle + extent;
        if (std::abs(speed) < 1e-7f)
            return hi > min + 1e-4f && lo < max - 1e-4f;
        float a = (min - hi) / speed, b = (max - lo) / speed;
        if (a > b)
            std::swap(a, b);
        near = std::max(near, a);
        far = std::min(far, b);
        return near <= far && far > 1e-6f;
    };
    if (count < 3 || !axis({1, 0}) || !axis({0, 1}))
        return false;
    for (int i = 0; i < count; ++i) {
        auto a = points[i], b = points[(i + 1) % count];
        if (!axis({a.y - b.y, b.x - a.x}))
            return false;
    }
    if (near > 1 || far <= 1e-6f)
        return false;
    entry = std::max(0.0f, near);
    return true;
}
static bool roundedBoxSweep(Rect box, Vec2 d, Vec2 circle, float radius, float& entry) {
    // Minkowski sum of the player AABB and a true circle: two strips and four
    // round corners. This does not approximate the circle with polygon edges.
    Vec2 p{box.x + box.w * .5f, box.y + box.h * .5f};
    float hw = box.w * .5f, hh = box.h * .5f, best = 2;
    auto strip = [&](Rect r) {
        std::array<Vec2, 4> vertices{
            {{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}}};
        float t = 0;
        if (polygonSweep({p.x, p.y, 0, 0}, d, vertices.data(), 4, t))
            best = std::min(best, t);
    };
    strip({circle.x - hw - radius, circle.y - hh, 2 * (hw + radius), 2 * hh});
    strip({circle.x - hw, circle.y - hh - radius, 2 * hw, 2 * (hh + radius)});
    float a = d.x * d.x + d.y * d.y;
    if (a > 1e-12f)
        for (int x = -1; x <= 1; x += 2)
            for (int y = -1; y <= 1; y += 2) {
                Vec2 q{p.x - circle.x - x * hw, p.y - circle.y - y * hh};
                float b = 2 * (q.x * d.x + q.y * d.y), c = q.x * q.x + q.y * q.y - radius * radius,
                      disc = b * b - 4 * a * c;
                if (disc <= 1e-6f)
                    continue;
                float near = (-b - std::sqrt(disc)) / (2 * a), far = (-b + std::sqrt(disc)) / (2 * a);
                if (far > 1e-6f && near <= 1)
                    best = std::min(best, std::max(0.0f, near));
            }
    if (best > 1)
        return false;
    entry = best;
    return true;
}
bool sweepCollider(Rect box, Vec2 delta, const StaticCollider& c, float& entry) {
    Rect r = c.bounds;
    if (box.x + std::min(0.0f, delta.x) > r.x + r.w || box.x + box.w + std::max(0.0f, delta.x) < r.x ||
        box.y + std::min(0.0f, delta.y) > r.y + r.h || box.y + box.h + std::max(0.0f, delta.y) < r.y)
        return false;
    if (c.kind == ColliderKind::Circle)
        return roundedBoxSweep(box, delta, {r.x + r.w * .5f, r.y + r.h * .5f}, std::min(r.w, r.h) * .5f,
                               entry);
    std::array<Vec2, 16> points{};
    int n = colliderVertices(c, points);
    return polygonSweep(box, delta, points.data(), n, entry);
}
bool collisionAllowed(const WorldState& w, CollisionClass a, CollisionClass b) noexcept {
    auto x = unsigned(a), y = unsigned(b);
    return x < 8 && y < 8 && (w.collisions.masks[x] & (1u << y)) && (w.collisions.masks[y] & (1u << x));
}
void setCollisionPair(CollisionRules& r, CollisionClass a, CollisionClass b, bool enabled) noexcept {
    auto x = unsigned(a), y = unsigned(b);
    if (x >= 8 || y >= 8)
        return;
    if (enabled) {
        r.masks[x] |= 1u << y;
        r.masks[y] |= 1u << x;
    } else {
        r.masks[x] &= ~(1u << y);
        r.masks[y] &= ~(1u << x);
    }
}
CollisionClass objectCollisionClass(const ObjectState& o) noexcept {
    if (o.kind == ObjectKind::Player)
        return CollisionClass::Player;
    if (o.kind == ObjectKind::Enemy || o.kind == ObjectKind::Boss)
        return CollisionClass::Enemy;
    if (o.kind == ObjectKind::NPC)
        return CollisionClass::NPC;
    return CollisionClass::Prop;
}
std::string writeCollisionRules(const CollisionRules& r) {
    std::ostringstream out;
    out << "1";
    for (auto mask : r.masks)
        out << ' ' << unsigned(mask);
    // Reject asymmetric serialized data rather than silently changing an author's rule.
    (void)readCollisionRules(out.str());
    return out.str();
}
CollisionRules readCollisionRules(std::string_view text) {
    CollisionRules r;
    std::istringstream in{std::string(text)};
    int version;
    if (!(in >> version) || version != 1)
        throw std::runtime_error("Invalid collision rules version");
    for (auto& mask : r.masks) {
        int value;
        if (!(in >> value) || value < 0 || value > 255)
            throw std::runtime_error("Invalid collision mask");
        mask = std::uint8_t(value);
    }
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Trailing collision rules");
    for (int a = 0; a < 8; ++a)
        for (int b = 0; b < 8; ++b)
            if (((unsigned(r.masks[a]) >> b) & 1u) != ((unsigned(r.masks[b]) >> a) & 1u))
                throw std::runtime_error("Collision matrix must be symmetric: " + std::to_string(a) +
                                         "/" + std::to_string(b) + " masks=" +
                                         std::to_string(r.masks[a]) + "/" + std::to_string(r.masks[b]));
    return r;
}
Vec2 moveEventBody(WorldState& w, int eventId, Rect body, Vec2 delta, bool& blocked) {
    Vec2 travelled{};
    for (int axis = 0; axis < 2; ++axis) {
        float amount = axis ? delta.y : delta.x;
        if (std::abs(amount) < 1e-7f)
            continue;
        Vec2 motion{axis ? 0.f : amount, axis ? amount : 0.f};
        float hit = 1;
        auto test = [&](const StaticCollider& c) {
            float t;
            if (sweepCollider(body, motion, c, t))
                hit = std::min(hit, t);
        };
        auto box = [&](Rect r) {
            StaticCollider c;
            c.bounds = r;
            test(c);
        };
        if (collisionAllowed(w, CollisionClass::Trigger, CollisionClass::Terrain)) {
            int x0 = std::max(0, int(std::floor(std::min(body.x, body.x + motion.x) / tileSize)));
            int x1 =
                std::min(mapWidth - 1,
                         int(std::floor((std::max(body.x, body.x + motion.x) + body.w) / tileSize)));
            int y0 = std::max(0, int(std::floor(std::min(body.y, body.y + motion.y) / tileSize)));
            int y1 =
                std::min(mapHeight - 1,
                         int(std::floor((std::max(body.y, body.y + motion.y) + body.h) / tileSize)));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    auto t = w.level.at(x, y);
                    if (t == Tile::Solid || (axis && amount > 0 && t == Tile::OneWay &&
                                             body.y + body.h <= y * tileSize + .05f))
                        box({x * tileSize, y * tileSize, tileSize, tileSize});
                }
            for (const auto& c : w.level.colliders)
                test(c);
            if (axis && amount > 0)
                for (const auto& f : w.level.features)
                    if (featureSupports(f) && body.y + body.h <= f.bounds.y + .05f)
                        box(f.bounds);
            if (axis && amount > 0)
                for (const auto& p : w.level.platforms)
                    if (body.y + body.h <= p.pos.y + .05f)
                        box({p.pos.x, p.pos.y, p.base.w, p.base.h});
        }
        if (collisionAllowed(w, CollisionClass::Trigger, CollisionClass::Trigger))
            for (const auto& e : w.level.events)
                if (e.id != eventId && e.solid && !e.erased)
                    box(e.region);
        if (collisionAllowed(w, CollisionClass::Trigger, CollisionClass::Player))
            box(kinoBody(w));
        for (const auto& o : w.objects.slots)
            if (o.active && o.alive && o.map == w.activeMap && o.solid && o.kind != ObjectKind::Player &&
                collisionAllowed(w, CollisionClass::Trigger, objectCollisionClass(o)))
                box(objectBody(w, o));
        blocked |= hit < 1;
        if (axis) {
            travelled.y = amount * hit;
            body.y += travelled.y;
        } else {
            travelled.x = amount * hit;
            body.x += travelled.x;
        }
    }
    return travelled;
}
} // namespace echo
