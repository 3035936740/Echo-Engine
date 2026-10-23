#pragma once
#include "world.hpp"
#include "platform_features.hpp"
#include "events.hpp"
#include "barrage.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <stdexcept>
namespace echo {
// Editing state becomes WorldState in Play. Runtime collision/layers/lights never
// hide in the editor, renderer, or closures.
class LevelEditor {
    struct UndoState {
        LevelState level;
        EventMap events, common;
        BarrageLibrary barrage;
    };
    std::vector<UndoState> undo_, redo_;
    std::size_t stateBytes(const UndoState& s) const {
        std::size_t bytes = sizeof(UndoState) + s.events.bytes() + s.common.bytes() +
                            s.barrage.patterns.capacity() * sizeof(BarragePattern) +
                            s.barrage.emitters.capacity() * sizeof(BarrageEmitter);
        for (const auto& emitter : s.barrage.emitters)
            bytes += emitter.waves.capacity() * sizeof(BarrageWave);
        return bytes;
    }
    void pushUndo(std::vector<UndoState>& list) {
        constexpr std::size_t budget = 64ull * 1024 * 1024;
        UndoState next{level, events, common, barrage};
        std::size_t bytes = list.capacity() * sizeof(UndoState) + stateBytes(next) - sizeof(UndoState);
        if (bytes > budget)
            throw std::runtime_error("Single edit exceeds undo memory budget");
        for (const auto& s : list)
            bytes += stateBytes(s) - sizeof(UndoState);
        while (!list.empty() && (list.size() >= 64 || bytes > budget)) {
            bytes -= stateBytes(list.front()) - sizeof(UndoState);
            list.erase(list.begin());
        }
        list.push_back(std::move(next));
    }
    bool stroke_{};
    int featureDrag_ = -1;
    bool featureCreating_{};
    Vec2 featureStart_{}, featureOffset_{};
    int sourceX_ = -1, sourceY_ = -1;
    Tile selectedTile_{};
    bool rectStroke_{}, selecting_{}, areaErase_{}, bucketStroke_{};
    int areaX_{}, areaY_{};
    std::vector<std::uint8_t> copiedCells_;
    std::vector<Tile> copiedCollision_;
    int copiedW_{}, copiedH_{};
    void writeCell(int x, int y, bool erase) {
        int i = y * mapWidth + x;
        level.layers[layer].cells[i] = erase ? 0 : std::uint8_t(std::clamp(tileIndex, 1, 16));
        if (layer == 1 && (autoCollision || (!erase && terrain != Tile::Solid)))
            level.tiles[i] = erase ? Tile::Empty : terrain;
        dirty = true;
    }
    void selectArea(int x, int y) {
        int left = std::min(x, areaX_), top = std::min(y, areaY_);
        selection = {left * tileSize, top * tileSize, (std::abs(x - areaX_) + 1) * tileSize,
                     (std::abs(y - areaY_) + 1) * tileSize};
        selectionValid = true;
    }
    Vec2 shapeStart_{};
    int activeCollider_ = -1;
    std::uint64_t draggedEmitter_{};
    Vec2 emitterDragOffset_{};
    BarrageEmitter* emitter(std::uint64_t id) {
        auto it = std::find_if(barrage.emitters.begin(), barrage.emitters.end(),
                               [&](const auto& e) { return e.id == id; });
        return it == barrage.emitters.end() ? nullptr : &*it;
    }
    void paintEmitter(float x, float y, bool erase) {
        if (erase) {
            end();
            emitterPlacementArmed = false;
            return;
        } // Right-click cancels positioning; deletion uses the checked form command.
        if (!draggedEmitter_) {
            if (emitterPlacementArmed) {
                draggedEmitter_ = selectedEmitter;
                emitterPlacementArmed = false;
                emitterDragOffset_ = {};
            } else {
                // Reverse order matches marker overlap selection.
                for (auto it = barrage.emitters.rbegin(); it != barrage.emitters.rend(); ++it) {
                    Vec2 point;
                    if (it->map == mapIndex && barrageEditorOrigin(level, *it, point) &&
                        std::abs(point.x - x) <= 24 && std::abs(point.y - y) <= 24) {
                        draggedEmitter_ = selectedEmitter = it->id;
                        emitterDragOffset_ = {point.x - x, point.y - y};
                        return; // Selection alone does not create an undo entry.
                    }
                }
            }
        }
        auto* e = emitter(draggedEmitter_);
        if (!e || e->map != mapIndex)
            return;
        Vec2 point;
        if (!barrageEditorOrigin(level, *e, point))
            return;
        auto anchor = Vec2{point.x - e->position.x, point.y - e->position.y};
        Vec2 position{x + emitterDragOffset_.x - anchor.x, y + emitterDragOffset_.y - anchor.y};
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || std::abs(position.x) > 8192 ||
            std::abs(position.y) > 8192)
            return;
        if (e->position.x == position.x && e->position.y == position.y)
            return;
        begin();
        e->position = position;
        dirty = true;
    }

  public:
    int selectedFeature = -1;
    PlatformKind featureKind = PlatformKind::Ladder;
    bool featurePlacementArmed{};
    EventMap events, common;
    BarrageLibrary barrage;
    LevelState level{};
    int mapIndex{};
    std::uint64_t selectedEmitter{};
    bool emitterPlacementArmed{};
    int tool = 1, selectedPlatform = -1, selectedLight = -1, selectedDialogue = -1, selectedPortal = -1,
        selectedEvent = -1, layer = 1, tileIndex = 1, selectedObject = -1;
    std::uint64_t objectTemplate = 1;
    bool dirty{}, autoCollision = true;
    Tile terrain = Tile::Solid;
    std::array<bool, 3> layerLocked{};
    Rect selection{};
    bool selectionValid{};
    LevelEditor() {
        undo_.reserve(64);
        redo_.reserve(64);
    }
    void begin() {
        if (stroke_)
            return;
        pushUndo(undo_);
        redo_.clear();
        stroke_ = true;
    }
    void end() {
        if (rectStroke_) {
            for (int y = int(selection.y / tileSize); y < int((selection.y + selection.h) / tileSize);
                 ++y)
                for (int x = int(selection.x / tileSize);
                     x < int((selection.x + selection.w) / tileSize); ++x)
                    writeCell(x, y, areaErase_);
            rectStroke_ = false;
        }
        selecting_ = false;
        bucketStroke_ = false;
        if (activeCollider_ >= 0 && tool == 14) {
            auto& c = level.colliders[activeCollider_];
            // Monotone-chain hull: the drawing is a convex static collision outline.
            std::array<Vec2, 32> hull{};
            int n = int(c.points.count), k = 0;
            auto sorted = c.points.values;
            std::sort(sorted.begin(), sorted.begin() + n,
                      [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            auto cross = [](Vec2 a, Vec2 b, Vec2 c) {
                return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            };
            for (int i = 0; i < n; ++i) {
                while (k >= 2 && cross(hull[k - 2], hull[k - 1], sorted[i]) <= 0)
                    --k;
                hull[k++] = sorted[i];
            }
            for (int i = n - 2, t = k + 1; i >= 0; --i) {
                while (k >= t && cross(hull[k - 2], hull[k - 1], sorted[i]) <= 0)
                    --k;
                hull[k++] = sorted[i];
            }
            c.points.clear();
            for (int i = 0; i < k - 1; ++i)
                c.points.push(hull[i]);
            if (c.points.count < 3)
                level.colliders.erase(activeCollider_);
            else {
                float minX = c.points[0].x, maxX = minX, minY = c.points[0].y, maxY = minY;
                for (auto point : c.points) {
                    minX = std::min(minX, point.x);
                    maxX = std::max(maxX, point.x);
                    minY = std::min(minY, point.y);
                    maxY = std::max(maxY, point.y);
                }
                c.bounds = {minX, minY, maxX - minX, maxY - minY};
            }
        }
        stroke_ = false;
        featureDrag_ = -1;
        featureCreating_ = false;
        sourceX_ = sourceY_ = -1;
        activeCollider_ = -1;
        draggedEmitter_ = 0;
    }
    bool armEmitter(std::uint64_t id) {
        end();
        auto* e = emitter(id);
        Vec2 point;
        if (!e || e->map != mapIndex || !barrageEditorOrigin(level, *e, point))
            return false;
        selectedEmitter = id;
        emitterPlacementArmed = true;
        tool = 23;
        return true;
    }
    std::size_t undoCount() const {
        return undo_.size();
    }
    std::size_t redoCount() const {
        return redo_.size();
    }
    std::size_t undoBytes() const {
        std::size_t bytes = undo_.capacity() * sizeof(UndoState);
        for (const auto& s : undo_)
            bytes += stateBytes(s) - sizeof(UndoState);
        return bytes;
    }
    std::size_t redoBytes() const {
        std::size_t bytes = redo_.capacity() * sizeof(UndoState);
        for (const auto& s : redo_)
            bytes += stateBytes(s) - sizeof(UndoState);
        return bytes;
    }
    void set(LevelState l, EventMap definitions = {}, EventMap globals = {}, int map = 0) {
        end();
        level = l;
        mapIndex = map;
        selectedEmitter = 0;
        emitterPlacementArmed = false;
        events = std::move(definitions);
        common = std::move(globals);
        undo_.clear();
        redo_.clear();
        dirty = false;
        selectionValid = false;
        selectedPlatform = selectedLight = selectedDialogue = selectedPortal = selectedEvent =
            selectedObject = -1;
    }
    void undo() {
        end();
        if (undo_.empty())
            return;
        pushUndo(redo_);
        selectedFeature = -1;
        featurePlacementArmed = false;
        auto nextFeatureId = level.nextFeatureId;
        auto nextObjectId = level.nextObjectId;
        level = undo_.back().level;
        level.nextObjectId = std::max(level.nextObjectId, nextObjectId);
        level.nextFeatureId = std::max(level.nextFeatureId, nextFeatureId);
        events = undo_.back().events;
        common = undo_.back().common;
        auto nextBarrageId = barrage.nextId;
        barrage = undo_.back().barrage;
        barrage.nextId = std::max(barrage.nextId, nextBarrageId);
        selectedEmitter = 0;
        emitterPlacementArmed = false;
        undo_.pop_back();
        dirty = true;
        selectedPlatform = selectedLight = selectedDialogue = selectedPortal = selectedEvent =
            selectedObject = -1;
    }
    void redo() {
        end();
        if (redo_.empty())
            return;
        pushUndo(undo_);
        selectedFeature = -1;
        featurePlacementArmed = false;
        auto nextFeatureId = level.nextFeatureId;
        auto nextObjectId = level.nextObjectId;
        level = redo_.back().level;
        level.nextObjectId = std::max(level.nextObjectId, nextObjectId);
        level.nextFeatureId = std::max(level.nextFeatureId, nextFeatureId);
        events = redo_.back().events;
        common = redo_.back().common;
        auto nextBarrageId = barrage.nextId;
        barrage = redo_.back().barrage;
        barrage.nextId = std::max(barrage.nextId, nextBarrageId);
        selectedEmitter = 0;
        emitterPlacementArmed = false;
        redo_.pop_back();
        dirty = true;
        selectedPlatform = selectedLight = selectedDialogue = selectedPortal = selectedEvent =
            selectedObject = -1;
    }
    bool copySelection() {
        if (!selectionValid)
            return false;
        copiedW_ = int(selection.w / tileSize);
        copiedH_ = int(selection.h / tileSize);
        copiedCells_.clear();
        copiedCollision_.clear();
        copiedCells_.reserve(copiedW_ * copiedH_);
        copiedCollision_.reserve(copiedW_ * copiedH_);
        for (int y = 0; y < copiedH_; ++y)
            for (int x = 0; x < copiedW_; ++x) {
                int i = (int(selection.y / tileSize) + y) * mapWidth + int(selection.x / tileSize) + x;
                copiedCells_.push_back(level.layers[layer].cells[i]);
                copiedCollision_.push_back(layer == 1 ? level.tiles[i] : Tile::Empty);
            }
        return true;
    }
    bool pasteSelection(int x, int y) {
        if (layerLocked[layer] || copiedCells_.empty() || x < 0 || y < 0 || x + copiedW_ > mapWidth ||
            y + copiedH_ > mapHeight)
            return false;
        begin();
        for (int row = 0; row < copiedH_; ++row)
            for (int col = 0; col < copiedW_; ++col) {
                int i = (y + row) * mapWidth + x + col, j = row * copiedW_ + col;
                level.layers[layer].cells[i] = copiedCells_[j];
                if (layer == 1 && autoCollision)
                    level.tiles[i] = copiedCollision_[j];
            }
        selection = {x * tileSize, y * tileSize, copiedW_ * tileSize, copiedH_ * tileSize};
        selectionValid = dirty = true;
        end();
        return true;
    }
    void clearSelection() {
        if (!selectionValid || layerLocked[layer])
            return;
        begin();
        for (int y = int(selection.y / tileSize); y < int((selection.y + selection.h) / tileSize); ++y)
            for (int x = int(selection.x / tileSize); x < int((selection.x + selection.w) / tileSize);
                 ++x)
                writeCell(x, y, true);
        end();
    }
    void flipSelection(bool horizontal) {
        if (!selectionValid || layerLocked[layer])
            return;
        begin();
        int left = int(selection.x / tileSize), top = int(selection.y / tileSize);
        int width = int(selection.w / tileSize), height = int(selection.h / tileSize);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                int otherX = horizontal ? width - 1 - x : x, otherY = horizontal ? y : height - 1 - y;
                int a = (top + y) * mapWidth + left + x, b = (top + otherY) * mapWidth + left + otherX;
                if (a < b) {
                    std::swap(level.layers[layer].cells[a], level.layers[layer].cells[b]);
                    if (layer == 1 && autoCollision)
                        std::swap(level.tiles[a], level.tiles[b]);
                }
            }
        dirty = true;
        end();
    }
    void floodFill(int x, int y, bool erase = false) {
        if (layerLocked[layer] || x < 0 || y < 0 || x >= mapWidth || y >= mapHeight)
            return;
        int first = y * mapWidth + x;
        auto original = level.layers[layer].cells[first];
        auto collision = level.tiles[first];
        auto replacement = erase ? 0 : std::clamp(tileIndex, 1, 16);
        if (original == replacement &&
            (layer != 1 || !autoCollision || collision == (erase ? Tile::Empty : terrain)))
            return;
        begin();
        std::array<int, mapWidth * mapHeight> queue{};
        std::array<bool, mapWidth * mapHeight> visited{};
        int head = 0, tail = 1;
        queue[0] = first;
        visited[first] = true;
        while (head < tail) {
            int i = queue[head++], cx = i % mapWidth, cy = i / mapWidth;
            if (level.layers[layer].cells[i] != original ||
                (layer == 1 && autoCollision && level.tiles[i] != collision))
                continue;
            writeCell(cx, cy, erase);
            for (auto d : std::array<Vec2, 4>{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}}) {
                int nx = cx + int(d.x), ny = cy + int(d.y), n = ny * mapWidth + nx;
                if (nx >= 0 && nx < mapWidth && ny >= 0 && ny < mapHeight && !visited[n]) {
                    visited[n] = true;
                    queue[tail++] = n;
                }
            }
        }
    }
    void retileSelection() {
        if (layerLocked[layer])
            return;
        begin();
        auto& cells = level.layers[layer].cells;
        int left = selectionValid ? int(selection.x / tileSize) : 0,
            top = selectionValid ? int(selection.y / tileSize) : 0;
        int right = selectionValid ? int((selection.x + selection.w) / tileSize) : mapWidth;
        int bottom = selectionValid ? int((selection.y + selection.h) / tileSize) : mapHeight;
        auto occupied = [&](int x, int y) {
            return x >= 0 && x < mapWidth && y >= 0 && y < mapHeight && cells[y * mapWidth + x] != 0;
        };
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                if (occupied(x, y)) {
                    int mask = occupied(x, y - 1) * 1 + occupied(x + 1, y) * 2 + occupied(x, y + 1) * 4 +
                               occupied(x - 1, y) * 8;
                    cells[y * mapWidth + x] = std::uint8_t(mask + 1);
                }
        dirty = true;
        end();
    }
    void paint(float x, float y, bool erase = false) {
        if (!std::isfinite(x) || !std::isfinite(y))
            return;
        int tx = int(x / tileSize), ty = int(y / tileSize);
        if (x < 0 || y < 0 || tx >= mapWidth || ty >= mapHeight)
            return;
        if (tool == 24) {
            float px = tx * tileSize, py = ty * tileSize;
            if (erase) {
                for (std::size_t i = 0; i < level.features.count; ++i) {
                    auto r = level.features[i].base;
                    if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
                        begin();
                        level.features.erase(i);
                        selectedFeature = -1;
                        dirty = true;
                        break;
                    }
                }
                featurePlacementArmed = false;
                return;
            }
            if (featureDrag_ < 0) {
                if (featurePlacementArmed) {
                    if (level.features.count >= 64 || level.nextFeatureId > 1000000)
                        throw std::runtime_error("Platform feature capacity / ID exhausted");
                    begin();
                    auto f =
                        platformPreset(featureKind, {px, py, tileSize, tileSize}, level.nextFeatureId++);
                    level.features.push(f);
                    selectedFeature = featureDrag_ = int(level.features.count) - 1;
                    featureStart_ = {px, py};
                    featureCreating_ = true;
                    featurePlacementArmed = false;
                    dirty = true;
                } else {
                    for (int i = int(level.features.count) - 1; i >= 0; --i) {
                        auto r = level.features[i].base;
                        if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
                            selectedFeature = featureDrag_ = i;
                            featureOffset_ = {r.x - px, r.y - py};
                            return;
                        }
                    }
                    return;
                }
            }
            auto& f = level.features[featureDrag_];
            Rect next = f.base;
            if (featureCreating_)
                next = {std::min(px, featureStart_.x), std::min(py, featureStart_.y),
                        std::abs(px - featureStart_.x) + tileSize,
                        std::abs(py - featureStart_.y) + tileSize};
            else {
                next.x = px + featureOffset_.x;
                next.y = py + featureOffset_.y;
            }
            if (next.x != f.base.x || next.y != f.base.y || next.w != f.base.w || next.h != f.base.h) {
                begin();
                f.base = f.bounds = next;
                f.previous = {next.x, next.y};
                dirty = true;
            }
            return;
        }
        if (tool == 23) {
            paintEmitter(x, y, erase);
            return;
        }
        if (tool == 20) {
            if (!selecting_) {
                areaX_ = tx;
                areaY_ = ty;
                selecting_ = true;
            }
            selectArea(tx, ty);
            return;
        }
        if (tool == 21) {
            tileIndex = level.layers[layer].cells[ty * mapWidth + tx];
            terrain = level.at(tx, ty);
            if (terrain == Tile::Empty || terrain == Tile::Moving)
                terrain = Tile::Solid;
            if (tileIndex == 0)
                tileIndex = 1;
            return;
        }
        if (layerLocked[layer] && (tool <= 4 || tool == 8 || tool == 18 || tool == 19))
            return;
        if (tool == 18) {
            if (!rectStroke_) {
                begin();
                rectStroke_ = true;
                areaX_ = tx;
                areaY_ = ty;
            }
            areaErase_ = erase;
            selectArea(tx, ty);
            return;
        }
        if (tool == 19) {
            if (!bucketStroke_) {
                bucketStroke_ = true;
                floodFill(tx, ty, erase);
            }
            return;
        }
        if (tool >= 1 && tool <= 3)
            terrain = tool == 1 ? Tile::Solid : tool == 2 ? Tile::OneWay : Tile::Spike;
        bool first = !stroke_;
        begin();
        dirty = true;
        if (tool == 22) {
            if (!first && !erase && selectedObject >= 0 && selectedObject < int(level.objects.count)) {
                level.objects[selectedObject].x = tx * tileSize;
                level.objects[selectedObject].y = ty * tileSize;
                return;
            }
            for (std::size_t i = 0; i < level.objects.count; ++i) {
                auto& p = level.objects[i];
                if (x >= p.x && x < p.x + p.width && y >= p.y && y < p.y + p.height) {
                    if (erase) {
                        level.objects.erase(i);
                        selectedObject = -1;
                    } else
                        selectedObject = int(i);
                    return;
                }
            }
            if (!erase && objectTemplate && level.objects.count < maxObjectPlacements) {
                ObjectPlacement p;
                p.templateId = objectTemplate;
                p.x = tx * tileSize;
                p.y = ty * tileSize;
                for (const auto& prior : level.objects)
                    level.nextObjectId = std::max(level.nextObjectId, prior.id + 1);
                if (!level.nextObjectId || level.nextObjectId >= UINT64_MAX - 1)
                    return;
                p.id = level.nextObjectId++;
                level.objects.push(p);
                selectedObject = int(level.objects.count) - 1;
            }
            return;
        }
        if (tool == 17) {
            if (!first && !erase && selectedEvent >= 0 && selectedEvent < int(level.events.count)) {
                level.events[selectedEvent].region.x = tx * tileSize;
                level.events[selectedEvent].region.y = ty * tileSize;
                return;
            }
            for (std::size_t i = 0; i < level.events.count; ++i) {
                auto& e = level.events[i];
                if (x >= e.region.x && x < e.region.x + e.region.w && y >= e.region.y &&
                    y < e.region.y + e.region.h) {
                    if (erase) {
                        int id = e.id;
                        level.events.erase(i);
                        std::erase_if(events.events, [id](const auto& d) { return d.id == id; });
                        selectedEvent = -1;
                    } else
                        selectedEvent = int(i);
                    return;
                }
            }
            if (first && !erase && level.events.count < 32 && events.events.size() < 32) {
                int id = 1;
                while (findEvent(events, id) && id <= 32)
                    ++id;
                EventPlacement e;
                e.id = id;
                e.region = {tx * tileSize, ty * tileSize, tileSize, tileSize};
                level.events.push(e);
                events.events.push_back(eventPreset(id, 0));
                selectedEvent = int(level.events.count) - 1;
            }
            return;
        }
        if (tool >= 10 && tool <= 14) {
            if (erase) {
                for (std::size_t i = 0; i < level.colliders.count; ++i) {
                    auto& r = level.colliders[i].bounds;
                    if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
                        level.colliders.erase(i);
                        break;
                    }
                }
                return;
            }
            if (first) {
                StaticCollider c;
                c.kind = ColliderKind(tool - 10);
                c.bounds = {x, y, 1, 1};
                shapeStart_ = {x, y};
                if (!level.colliders.push(c))
                    return;
                activeCollider_ = int(level.colliders.count) - 1;
            }
            if (activeCollider_ < 0)
                return;
            auto& c = level.colliders[activeCollider_];
            c.bounds = {std::min(x, shapeStart_.x), std::min(y, shapeStart_.y),
                        std::max(1.0f, std::abs(x - shapeStart_.x)),
                        std::max(1.0f, std::abs(y - shapeStart_.y))};
            if (tool == 10 || tool == 12)
                c.bounds.w = c.bounds.h = std::max(c.bounds.w, c.bounds.h);
            if (tool == 14 &&
                (c.points.count == 0 || std::hypot(x - c.points[c.points.count - 1].x,
                                                   y - c.points[c.points.count - 1].y) >= 8))
                c.points.push({x, y});
            return;
        }
        if (tool == 16) {
            if (erase) {
                for (std::size_t i = 0; i < level.portals.count; ++i) {
                    auto r = level.portals[i].region;
                    if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
                        level.portals.erase(i);
                        selectedPortal = -1;
                        break;
                    }
                }
                return;
            }
            if (first) {
                for (std::size_t i = 0; i < level.portals.count; ++i) {
                    auto r = level.portals[i].region;
                    if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
                        selectedPortal = int(i);
                        activeCollider_ = -1;
                        return;
                    }
                }
                MapPortal p;
                p.region = {x, y, 32, 32};
                if (!level.portals.push(p))
                    return;
                selectedPortal = int(level.portals.count) - 1;
                activeCollider_ = selectedPortal;
                shapeStart_ = {x, y};
            }
            if (activeCollider_ >= 0)
                level.portals[activeCollider_].region = {std::min(x, shapeStart_.x),
                                                         std::min(y, shapeStart_.y),
                                                         std::max(1.0f, std::abs(x - shapeStart_.x)),
                                                         std::max(1.0f, std::abs(y - shapeStart_.y))};
            return;
        }
        if (tool == 15) {
            if (erase) {
                for (std::size_t i = 0; i < level.dialogues.count; ++i) {
                    auto r = level.dialogues[i].region;
                    if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
                        level.dialogues.erase(i);
                        selectedDialogue = -1;
                        break;
                    }
                }
                return;
            }
            if (first) {
                for (std::size_t i = 0; i < level.dialogues.count; ++i) {
                    auto r = level.dialogues[i].region;
                    if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
                        selectedDialogue = int(i);
                        activeCollider_ = -1;
                        return;
                    }
                }
                DialogueDefinition d;
                d.speaker.set("Character");
                d.text.set("Write dialogue in the Dialogue editor.");
                d.region = {x, y, 32, 32};
                if (!level.dialogues.push(d))
                    return;
                selectedDialogue = int(level.dialogues.count) - 1;
                activeCollider_ = selectedDialogue;
                shapeStart_ = {x, y};
            }
            if (activeCollider_ >= 0)
                level.dialogues[activeCollider_].region = {std::min(x, shapeStart_.x),
                                                           std::min(y, shapeStart_.y),
                                                           std::max(1.0f, std::abs(x - shapeStart_.x)),
                                                           std::max(1.0f, std::abs(y - shapeStart_.y))};
            return;
        }
        auto& cells = level.layers[layer].cells;
        if (tool == 9) {
            for (std::size_t i = 0; i < level.lights.count; ++i) {
                auto& lamp = level.lights[i];
                if (std::hypot(x - lamp.pos.x, y - lamp.pos.y) < 24) {
                    if (erase)
                        level.lights.erase(i);
                    else
                        selectedLight = int(i);
                    return;
                }
            }
            if (!erase && first) {
                SceneLight lamp;
                lamp.pos = {x, y};
                if (level.lights.push(lamp))
                    selectedLight = int(level.lights.count) - 1;
            }
            return;
        }
        if (erase || tool == 4) {
            cells[ty * mapWidth + tx] = 0;
            if (autoCollision && layer == 1)
                level.tiles[ty * mapWidth + tx] = Tile::Empty;
            return;
        }
        if (tool == 5) {
            level.spawn = {tx * tileSize, ty * tileSize};
            return;
        }
        if (tool == 6) {
            level.bossSpawn = {tx * tileSize, ty * tileSize};
            return;
        }
        if (tool == 7) {
            for (std::size_t i = 0; i < level.platforms.count; ++i) {
                auto& p = level.platforms[i];
                if (x >= p.base.x && x < p.base.x + p.base.w && y >= p.base.y && y < p.base.y + 32) {
                    selectedPlatform = int(i);
                    return;
                }
            }
            if (first) {
                MovingPlatform p;
                p.base = {tx * tileSize, ty * tileSize, 128, 12};
                p.pos = p.previous = {p.base.x, p.base.y};
                p.id = level.platforms.count;
                if (level.platforms.push(p))
                    selectedPlatform = int(level.platforms.count) - 1;
            }
            return;
        }
        if (tool == 8) {
            if (sourceX_ < 0) {
                sourceX_ = tx;
                sourceY_ = ty;
                selectedTile_ = level.at(tx, ty);
                tileIndex = cells[ty * mapWidth + tx];
            } else if (tx != sourceX_ || ty != sourceY_) {
                cells[sourceY_ * mapWidth + sourceX_] = 0;
                cells[ty * mapWidth + tx] = std::uint8_t(tileIndex);
                if (layer == 1 && autoCollision) {
                    level.tiles[sourceY_ * mapWidth + sourceX_] = Tile::Empty;
                    level.tiles[ty * mapWidth + tx] = selectedTile_;
                }
                sourceX_ = tx;
                sourceY_ = ty;
            }
            return;
        }
        cells[ty * mapWidth + tx] = std::uint8_t(std::clamp(tileIndex, 1, 16));
        if (layer == 1 && (autoCollision || tool == 2 || tool == 3))
            level.tiles[ty * mapWidth + tx] = tool == 1   ? Tile::Solid
                                              : tool == 2 ? Tile::OneWay
                                                          : Tile::Spike;
    }
};
} // namespace echo
