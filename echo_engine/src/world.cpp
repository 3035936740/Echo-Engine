#include "echo/world.hpp"
namespace echo {
WorldState makeWorld() {
    WorldState w;
    for (int x = 0; x < mapWidth; ++x)
        for (int y = 20; y < mapHeight; ++y)
            w.level.tiles[y * mapWidth + x] = Tile::Solid;
    for (int x = 6; x < 11; ++x)
        w.level.tiles[16 * mapWidth + x] = Tile::OneWay;
    for (int x = 17; x < 22; ++x)
        w.level.tiles[15 * mapWidth + x] = Tile::OneWay;
    for (int x = 25; x < 29; ++x)
        w.level.tiles[17 * mapWidth + x] = Tile::OneWay;
    w.level.platforms.push({{1120, 520, 128, 12}, {1120, 520}, {1120, 520}, 80, .8f, 0, 0});
    return w;
}
} // namespace echo
