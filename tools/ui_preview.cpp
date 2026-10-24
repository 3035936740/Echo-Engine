// Renders the editor chrome offscreen with mock state, so the UI can be reviewed without the
// OpenGL editor (also works headless under Wine). Usage:
//   ui_preview <tiles.png> <game-view.png | -> <output-folder>
#include "echo/workspace_view.hpp"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers" // terse mock data
#include <wincodec.h>
#include <cstdio>
#include <string>
#include <vector>
using namespace echo;
using namespace echo::ui;
namespace {
HBITMAP loadImage(const std::wstring& path, int& width, int& height) {
    IWICImagingFactory* factory{};
    IWICBitmapDecoder* decoder{};
    IWICBitmapFrameDecode* frame{};
    IWICFormatConverter* converter{};
    HBITMAP result{};
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IWICImagingFactory, reinterpret_cast<void**>(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                                        0, WICBitmapPaletteTypeCustom))) {
        UINT w{}, h{};
        frame->GetSize(&w, &h);
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), LONG(w), -LONG(h), 1, 32, BI_RGB, 0, 0, 0, 0, 0};
        void* bits{};
        result = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (result && SUCCEEDED(converter->CopyPixels(nullptr, w * 4, w * h * 4, static_cast<BYTE*>(bits)))) {
            auto* p = static_cast<BYTE*>(bits);
            for (UINT i = 0; i < w * h; ++i) {
                int bg = 200;
                for (int k = 0; k < 3; ++k)
                    p[i * 4 + k] = BYTE((p[i * 4 + k] * p[i * 4 + 3] + bg * (255 - p[i * 4 + 3])) / 255);
            }
            width = int(w);
            height = int(h);
        }
    }
    for (IUnknown* u : std::initializer_list<IUnknown*>{converter, frame, decoder, factory})
        if (u)
            u->Release();
    return result;
}
void writeBmp(Painter& p, const std::wstring& path) {
    FILE* out = _wfopen(path.c_str(), L"wb");
    if (!out)
        return;
    int w = p.width(), h = p.height();
    BITMAPFILEHEADER file{0x4d42, DWORD(54 + w * h * 4), 0, 0, 54};
    BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    fwrite(&file, sizeof(file), 1, out);
    fwrite(&info, sizeof(info), 1, out);
    std::vector<std::uint32_t> opaque(p.pixels(), p.pixels() + size_t(w) * h);
    for (auto& px : opaque)
        px |= 0xFF000000u; // GDI text leaves the unused byte at 0; BMP readers may treat it as alpha.
    fwrite(opaque.data(), 4, opaque.size(), out);
    fclose(out);
}
struct Scene {
    const wchar_t* name;
    int width, height;
    float scale;
    WorkspaceModel model;
    Key hot{};
    int menu = -1, menuHot = -1;
    const wchar_t* tip{};
};
// Native-dialog control skins on a card and on the window background, light and dark.
void renderGallery(const std::wstring& folder) {
    HFONT dialogFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    Painter p;
    int W = 1160, H = 860;
    p.resize(W, H);
    for (int dark = 0; dark < 2; ++dark) {
        const auto& t = theme(dark != 0);
        Box half{dark * W / 2, 0, W / 2, H};
        p.fill(half, t.window);
        Box card{half.x + 296, 16, W / 2 - 312, H - 32};
        p.round(card, 10, t.panel, t.border);
        for (int column = 0; column < 2; ++column) {
            int x = column ? card.x + 16 : half.x + 16, y = 24;
            Color surface = column ? t.panel : t.window;
            auto row = [&](int h) {
                Box b{x, y, 248, h};
                y += h + 10;
                return b;
            };
            ControlState s;
            paintPushButton(p, row(32), L"应用模板", dialogFont, t, surface, s);
            s.hovered = true;
            paintPushButton(p, row(32), L"悬停", dialogFont, t, surface, s);
            s = {};
            s.pressed = true;
            s.hovered = true;
            paintPushButton(p, row(32), L"按下", dialogFont, t, surface, s);
            s = {};
            s.active = true;
            paintPushButton(p, row(32), L"当前工具", dialogFont, t, surface, s);
            s = {};
            s.primary = true;
            paintPushButton(p, row(32), L"应用并试玩", dialogFont, t, surface, s);
            s = {};
            {
                Theme danger = t; // mirrors editor_theme.cpp destructive buttons
                danger.text = t.stop;
                danger.hover = mix(t.raised, t.stop, t.dark ? .16f : .08f);
                paintPushButton(p, row(32), L"删除模板", dialogFont, danger, surface, s);
            }
            s.disabled = true;
            paintPushButton(p, row(32), L"不可用", dialogFont, t, surface, s);
            s = {};
            paintCheckBox(p, row(26), L"使用重力", dialogFont, t, surface, s);
            s.checked = true;
            paintCheckBox(p, row(26), L"随平台移动", dialogFont, t, surface, s);
            s.hovered = true;
            paintCheckBox(p, row(26), L"悬停（已勾选）", dialogFont, t, surface, s);
            s = {};
            s.disabled = true;
            paintCheckBox(p, row(26), L"不可用", dialogFont, t, surface, s);
            s = {};
            paintComboFace(p, row(30), L"巡逻敌人", dialogFont, t, surface, s);
            s.focused = true;
            paintComboFace(p, row(30), L"获得焦点", dialogFont, t, surface, s);
            s = {};
            Box field = row(30);
            paintFieldFrame(p, field, t, surface, s);
            p.text(field.inset(8, 0), L"1088", dialogFont, t.text);
            s.focused = true;
            field = row(30);
            paintFieldFrame(p, field, t, surface, s);
            p.text(field.inset(8, 0), L"输入中…", dialogFont, t.text);
            if (column) {
                s = {};
                Box list = {x, y, 248, 160};
                paintFieldFrame(p, list, t, surface, s, 3);
                const wchar_t* items[] = {L"1  巡逻示例", L"2  追击敌人", L"3  弹幕炮台", L"4  NPC / 对话角色",
                                          L"5  门 / 机关", L"6  拾取物"};
                for (int i = 0; i < 6; ++i) {
                    ControlState item;
                    item.active = i == 1;
                    paintListItem(p, {list.x + 2, list.y + 2 + i * 25, list.w - 4, 25}, items[i], dialogFont, t,
                                  item, fieldFill(t, surface));
                }
            }
        }
    }
    writeBmp(p, folder + L"\\controls.bmp");
    DeleteObject(dialogFont);
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc < 4)
        return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    TileAtlas atlas;
    atlas.bitmap = loadImage(argv[1], atlas.width, atlas.height);
    atlas.frame = 8;
    int gameW{}, gameH{};
    HBITMAP game = std::wstring(argv[2]) == L"-" ? nullptr : loadImage(argv[2], gameW, gameH);
    std::wstring folder = argv[3];
    renderGallery(folder);

    WorkspaceModel base;
    base.project = L"Echo of Kino";
    base.map = L"start";
    base.maps = {L"start", L"dawn"};
    base.tool = 0;
    base.tile = 5;
    base.status = L"地图 1 · 80 × 23 格   |   世界   |   画笔   |   图块 6";
    base.canUndo = true;
    auto events = base;
    events.mode = 1;
    events.items = {{L"村长对话", L"#1"}, {L"宝箱", L"#2"}, {L"传送门：黎明", L"#3"}, {L"Boss 战开始", L"#4"}};
    events.selectedItem = 1;
    events.dirty = true;
    events.status = L"地图 1 · 80 × 23 格   |   世界   |   地图事件   |   未保存";
    auto playing = base;
    playing.editing = false;
    playing.status = L"试玩   |   回溯 3.20/8s   |   历史 2.1 MiB / 预留 12.0 MiB   |   弹幕 112/2048";
    auto dark = base;
    dark.dark = true;
    auto darkEvents = events;
    darkEvents.dark = true;
    auto objects = base;
    objects.mode = 2;
    objects.items = {{L"巡逻示例", L""}, {L"追击敌人", L""}, {L"弹幕炮台", L""}, {L"NPC / 对话角色", L""},
                     {L"门 / 机关", L""}, {L"拾取物", L""}};
    objects.selectedItem = 0;
    auto inspector = base;
    inspector.inspector = true;
    inspector.properties = {
        {Property::Section, 0, L"手感与弹幕"},
        {Property::Choice, 1, L"跳跃手感", L"", {L"手感：基础平台跳跃"}, 0},
        {Property::Choice, 2, L"弹幕预设", L"", {L"弹幕：项目 / 自定义"}, 0},
        {Property::Toggle, 3, L"世界层自动生成碰撞", L"", {}, 0, true},
        {Property::Section, 0, L"环境与光照"},
        {Property::Choice, 4, L"环境", L"", {L"黄昏 · Dusk"}, 0},
        {Property::Choice, 5, L"天气", L"", {L"晴 / 无降水"}, 0},
        {Property::Slider, 6, L"雾浓度", L"", {}, 0, false, true, .04f, 0, 1, .01f},
        {Property::Slider, 7, L"风速", L"", {}, 0, false, true, .3f, -3, 3, .1f},
        {Property::Toggle, 8, L"太阳 / 月光与场景灯光", L"", {}, 0, true},
        {Property::Toggle, 9, L"流体背景层", L"", {}, 0, false},
        {Property::Section, 0, L"时间回溯"},
        {Property::Toggle, 10, L"启用回溯", L"", {}, 0, true},
        {Property::Slider, 11, L"可回溯时长", L"", {}, 0, false, true, 8, 1, 30, .5f, L" 秒"},
        {Property::Section, 0, L"移动平台"},
        {Property::Hint, 0, L"在地图上用「移动平台」工具放置或选中平台后，可在这里调整振幅与速度。"},
        {Property::Button, 12, L"放置 / 选择移动平台"}};
    auto inspectorDark = inspector;
    inspectorDark.dark = true;
    std::vector<Scene> scenes{
        {L"inspector-light", 1440, 900, 1, inspector, {int(WorkspaceAction::Property), 7}},
        {L"inspector-dark", 1440, 900, 1, inspectorDark, {int(WorkspaceAction::Property), 3}},
        {L"terrain-light", 1440, 900, 1, base, {int(WorkspaceAction::Tile), 2}},
        {L"terrain-dark", 1440, 900, 1, dark, {int(WorkspaceAction::Mode), 1}},
        {L"events-light", 1440, 900, 1, events, {int(WorkspaceAction::Item), 2}},
        {L"events-dark", 1440, 900, 1, darkEvents, {int(WorkspaceAction::MapRow), 1}},
        {L"objects-compact", 1200, 720, 1, objects, {}},
        {L"terrain-compact", 1200, 694, 1, base, {}}, // 1200 x 720 window minus the title bar
        {L"playing", 1440, 900, 1, playing, {int(WorkspaceAction::Run), 0}},
        {L"menu-light", 1440, 900, 1, base, {int(WorkspaceAction::Menu), 4}, 4, 2},
        {L"menu-dark", 1440, 900, 1, dark, {int(WorkspaceAction::Menu), 2}, 2, 3},
        {L"tooltip", 1440, 900, 1, base, {int(WorkspaceAction::Tool), 2}, -1, -1, L"油漆桶：填充相连的区域"},
        {L"terrain-150", 2160, 1350, 1.5f, base, {}},
        {L"narrow-150", 1650, 900, 1.5f, events, {}},
    };
    std::vector<MenuItem> menus[workspaceMenuCount];
    menus[2] = {{L"场景设置…", L"", 1},       {L"图层显示 / 锁定…", L"", 2}, {L"显示网格", L"", 3, true},
                {L"显示碰撞", L"", 4, true}, {L"", L"", 0, false, false, true}, {L"浅色主题", L"", 5, true},
                {L"深色主题", L"", 6},        {L"恢复默认工作区布局", L"", 7}};
    menus[4] = {{L"地图与卷轴…", L"", 1},
                {L"平台动作 / 机关预设…", L"", 2},
                {L"地图 / 全局事件…", L"", 3},
                {L"对象与模板…", L"", 4},
                {L"弹幕与波次…", L"", 5},
                {L"游戏数据库：物品 / 任务 / 开关 / 变量…", L"", 6},
                {L"", L"", 0, false, false, true},
                {L"资源管理…", L"", 7},
                {L"精灵与动画…", L"", 8},
                {L"BGM / SE…", L"", 9, false, true}};
    for (auto& scene : scenes) {
        Fonts fonts;
        fonts.create(scene.scale);
        Painter p;
        p.resize(scene.width, scene.height);
        View view;
        int dock = dp(272, scene.scale);
        auto layout = workspaceLayout(scene.width, scene.height, dock, scene.scale, scene.model.inspector);
        buildWorkspace(view, p, fonts, scene.model, layout);
        view.hot = scene.hot;
        const auto& t = theme(scene.model.dark);
        p.fill({0, 0, scene.width, scene.height}, t.window);
        view.paint(p, t, fonts, [&](Painter& painter, const Node& n, Box b) {
            paintWorkspaceCustom(painter, n, b, view, t, fonts, atlas);
        });
        // Canvas stand-in: the renderer clears to the backdrop and centers the 320 x 180 view.
        auto c = layout.canvas;
        p.fill(c, t.backdrop);
        if (game) {
            int k = std::max(1, std::min(c.w / 320, c.h / 180));
            Box view320{c.x + (c.w - 320 * k) / 2, c.y + (c.h - 180 * k) / 2, 320 * k, 180 * k};
            int sx = gameW > 1000 ? 368 : 0, sy = gameW > 1000 ? 196 : 0;
            p.image(game, sx, sy, std::min(960, gameW), std::min(540, gameH), view320);
        }
        if (scene.menu >= 0) {
            const Node* title = view.find(int(WorkspaceAction::Menu), scene.menu);
            Painter menu;
            Box size = menuSize(menu, menus[scene.menu], fonts);
            menu.resize(size.w, size.h);
            paintMenu(menu, menus[scene.menu], scene.menuHot, t, fonts);
            for (int i = 1; i <= 10; ++i)
                p.round({title->box.x - i, title->box.bottom() + 4 - i + 3, size.w + 2 * i, size.h + 2 * i},
                        float(dp(8, scene.scale) + i), alpha(hex(0), .012f));
            p.image(nullptr, 0, 0, 0, 0, {});
            BitBlt(p.dc(), title->box.x, title->box.bottom() + 4, size.w, size.h, menu.dc(), 0, 0, SRCCOPY);
            GdiFlush();
        }
        if (scene.tip) {
            const Node* node = view.find(scene.hot.action, scene.hot.value);
            Painter tip;
            tip.resize(1, 1);
            int w = tip.measure(scene.tip, fonts.small) + dp(20, scene.scale), h = dp(28, scene.scale);
            p.round({node->box.x, node->box.bottom() + dp(6, scene.scale), w, h}, float(dp(6, scene.scale)),
                    t.tooltip);
            p.text({node->box.x + dp(10, scene.scale), node->box.bottom() + dp(6, scene.scale), w, h}, scene.tip,
                   fonts.small, t.tooltipText);
        }
        writeBmp(p, folder + L"\\" + scene.name + L".bmp");
        std::wprintf(L"%ls\n", scene.name);
    }
    return 0;
}
