#pragma once
#include "ui.hpp"
namespace echo {
// Main editor window built from plain state so it can be rendered without App (see tools/ui_preview).
enum class WorkspaceAction : int {
    None,
    Menu,
    New,
    Open,
    Save,
    Undo,
    Redo,
    Mode,
    Tool,
    Database,
    Settings,
    Theme,
    Run,
    Layer,
    Tile,
    Terrain,
    ImportTiles,
    Item,
    ItemEdit,
    MapRow,
    MapNew,
    MapSettings,
    Splitter,
    Property, // value = Property::id
    CloseInspector
};
// One row of the drawn scene-settings panel.
struct Property {
    enum Kind { Section, Choice, Toggle, Slider, Button, Hint } kind{};
    int id{};
    std::wstring label, tip;
    std::vector<std::wstring> options; // Choice
    int choice{};
    bool on{}, enabled = true;
    float value{}, minimum{}, maximum = 1, step = .1f; // Slider
    std::wstring unit;
};
struct WorkspaceModel {
    bool dark{}, editing = true, dirty{}, canUndo{}, canRedo{};
    int mode{};     // 0 terrain, 1 events, 2 objects, 3 barrage, 4 platform features
    int tool = -1;  // 0 pencil, 1 rectangle, 2 bucket, 3 selection, 4 eyedropper
    int brush{};    // 0 solid, 1 one-way, 2 spike, 3 erase
    int layer = 1;  // 0 back, 1 world, 2 front
    int tile{};     // 0-based atlas index
    int tileCount = 16;
    std::wstring project, map, status;
    struct Item {
        std::wstring name, note;
    };
    std::vector<Item> items;
    int selectedItem = -1;
    std::vector<std::wstring> maps;
    int editMap{}, startMap{};
    int openMenu = -1; // menu-bar entry whose popup is open
    bool inspector{};  // scene settings panel on the right
    std::vector<Property> properties;
};
struct WorkspaceLayout {
    ui::Box menubar, toolbar, dock, splitter, header, canvas, status, inspector;
};
constexpr int workspaceMenuCount = 7;
extern const wchar_t* const workspaceMenus[workspaceMenuCount];
WorkspaceLayout workspaceLayout(int width, int height, int dockWidth, float scale, bool inspector = false);
std::wstring propertyText(const Property&);
void buildWorkspace(ui::View&, ui::Painter&, const ui::Fonts&, const WorkspaceModel&, const WorkspaceLayout&);
struct TileAtlas {
    HBITMAP bitmap{};
    int width{}, height{}, frame = 16;
};
// Paints Custom nodes (tile thumbnails and the app mark).
void paintWorkspaceCustom(ui::Painter&, const ui::Node&, ui::Box, const ui::View&, const ui::Theme&,
                          const ui::Fonts&, const TileAtlas&);
} // namespace echo
