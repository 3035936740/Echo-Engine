#include "echo/workspace_view.hpp"
#include <algorithm>
namespace echo {
using namespace ui;
const wchar_t* const workspaceMenus[workspaceMenuCount] = {L"文件", L"编辑", L"视图", L"绘图",
                                                           L"作品", L"工具", L"帮助"};
namespace {
int act(WorkspaceAction a) {
    return int(a);
}
} // namespace
std::wstring propertyText(const Property& p) {
    if (p.kind == Property::Choice)
        return p.choice >= 0 && p.choice < int(p.options.size()) ? p.options[p.choice] : L"";
    wchar_t text[48]{};
    int digits = p.step >= 1 ? 0 : p.step >= .1f ? 1 : 2;
    swprintf(text, 48, L"%.*f", digits, double(p.value));
    return text + p.unit;
}
WorkspaceLayout workspaceLayout(int width, int height, int dockWidth, float s, bool inspector) {
    WorkspaceLayout l;
    int menu = dp(30, s), tools = dp(48, s), status = dp(26, s), header = dp(38, s), split = dp(6, s);
    l.menubar = {0, 0, width, menu};
    l.toolbar = {0, menu, width, tools};
    int top = menu + tools, bottom = std::max(top + dp(120, s), height - status);
    l.status = {0, bottom, width, height - bottom};
    l.dock = {0, top, dockWidth, bottom - top};
    l.splitter = {dockWidth, top, split, bottom - top};
    int left = dockWidth + split, panel = inspector ? dp(300, s) : 0;
    l.inspector = {width - panel, top, panel, bottom - top};
    int right = width - panel;
    l.header = {left, top, std::max(0, right - left), header};
    l.canvas = {left, top + header, std::max(0, right - left - split), std::max(0, bottom - top - header - split)};
    return l;
}

void buildWorkspace(View& view, Painter& p, const Fonts& f, const WorkspaceModel& m, const WorkspaceLayout& l) {
    const auto& t = theme(m.dark);
    float s = f.scale;
    auto D = [&](float v) { return dp(v, s); };
    view.clear();
    auto fill = [&](Box b, Color c, int radius = 0, Color stroke = {0, 0, 0, 0}) {
        Node n;
        n.kind = Kind::Fill;
        n.box = b;
        n.color = c;
        n.radius = radius;
        n.stroke = stroke;
        view.add(n);
    };
    auto divider = [&](Box b, Color c) {
        Node n;
        n.kind = Kind::Divider;
        n.box = b;
        n.color = c;
        view.add(n);
    };
    auto label = [&](Box b, std::wstring text, std::uint32_t flags = 0, Icon icon = Icon::None) -> Node& {
        Node n;
        n.kind = Kind::Label;
        n.box = b;
        n.text = std::move(text);
        n.flags = flags;
        n.icon = icon;
        return view.add(n);
    };
    auto button = [&](Box b, WorkspaceAction a, int value, std::wstring text, Icon icon, std::wstring tip,
                      std::uint32_t flags = 0) -> Node& {
        Node n;
        n.kind = Kind::Button;
        n.box = b;
        n.action = act(a);
        n.value = value;
        n.text = std::move(text);
        n.icon = icon;
        n.tip = std::move(tip);
        n.flags = flags;
        return view.add(n);
    };
    auto badge = [&](Box b, std::wstring text, std::uint32_t flags = 0) {
        Node n;
        n.kind = Kind::Badge;
        n.box = b;
        n.text = std::move(text);
        n.flags = flags;
        view.add(n);
    };
    auto badgeWidth = [&](const std::wstring& text) { return p.measure(text, f.small) + D(16); };
    struct Choice {
        const wchar_t* text;
        Icon icon;
        const wchar_t* tip;
    };
    // Segmented control: a sunken track holding equally styled options.
    auto segments = [&](Box track, WorkspaceAction a, const std::vector<Choice>& choices, int selected,
                        bool equal, bool disabled = false) {
        fill(track, t.sunken, D(8));
        int pad = D(3), x = track.x + pad, inner = track.w - 2 * pad;
        std::vector<int> widths;
        int natural = 0;
        for (const auto& c : choices) {
            int w = D(24) + p.measure(c.text, f.strong) + (c.icon != Icon::None ? D(22) : 0);
            widths.push_back(w);
            natural += w;
        }
        for (int i = 0; i < int(choices.size()); ++i) {
            int w = equal ? (inner - (x - track.x - pad)) / (int(choices.size()) - i)
                          : widths[i] + (inner - natural) / int(choices.size());
            Node n;
            n.kind = Kind::Segment;
            n.box = {x, track.y + pad, w, track.h - 2 * pad};
            n.action = act(a);
            n.value = i;
            n.text = choices[i].text;
            n.icon = choices[i].icon;
            n.tip = choices[i].tip;
            n.flags = (i == selected ? Selected : 0) | (disabled ? Disabled : 0);
            n.radius = D(6);
            view.add(n);
            x += w;
        }
    };
    auto segmentsWidth = [&](const std::vector<Choice>& choices) {
        int w = D(6);
        for (const auto& c : choices)
            w += *c.text ? D(24) + p.measure(c.text, f.strong) + (c.icon != Icon::None ? D(22) : 0) : D(38);
        return w;
    };

    // Menu bar and title.
    fill(l.menubar, t.chrome);
    {
        Node mark;
        mark.kind = Kind::Custom;
        mark.box = {D(12), l.menubar.y + (l.menubar.h - D(20)) / 2, D(20), D(20)};
        mark.value = 1;
        view.add(mark);
        int x = mark.box.right() + D(8);
        for (int i = 0; i < workspaceMenuCount; ++i) {
            int w = p.measure(workspaceMenus[i], f.body) + D(20);
            Node n;
            n.kind = Kind::MenuTitle;
            n.box = {x, l.menubar.y + D(3), w, l.menubar.h - D(6)};
            n.action = act(WorkspaceAction::Menu);
            n.value = i;
            n.text = workspaceMenus[i];
            n.flags = i == m.openMenu ? Selected : 0;
            view.add(n);
            x += w;
        }
    }
    divider({0, l.menubar.bottom() - 1, l.menubar.w, 1}, t.divider);

    // Toolbar: file, history, edit mode, drawing tools | database, settings, theme, run.
    fill(l.toolbar, t.chrome);
    divider({0, l.toolbar.bottom() - 1, l.toolbar.w, 1}, t.border);
    int bh = D(32), by = l.toolbar.y + (l.toolbar.h - bh) / 2;
    int x = D(10);
    auto tool = [&](WorkspaceAction a, int value, Icon icon, std::wstring tip, std::uint32_t flags = 0) {
        button({x, by, bh, bh}, a, value, L"", icon, tip, Quiet | flags);
        x += bh + D(2);
    };
    auto separator = [&] {
        divider({x + D(6), by + D(7), 1, bh - D(14)}, t.border);
        x += D(14);
    };
    std::vector<Choice> modes{{L"地形", Icon::Terrain, L"地形：铺设图块与碰撞"},
                              {L"事件", Icon::Event, L"事件：单击部署，双击编辑指令"},
                              {L"对象", Icon::Object, L"对象：选择模板后在地图放置"},
                              {L"弹幕", Icon::Barrage, L"弹幕：拖动发射器，双击编辑波次"},
                              {L"机关", Icon::Feature, L"机关：梯子、水域、弹簧与升降平台"}};
    struct Wide {
        WorkspaceAction action;
        const wchar_t* text;
        Icon icon;
        const wchar_t* tip;
    };
    const Wide wide[] = {{WorkspaceAction::Settings, L"场景设置", Icon::Sliders, L"环境、天气、光照、回溯与平台参数"},
                         {WorkspaceAction::Database, L"数据库", Icon::Database, L"物品、任务、开关与变量"}};
    std::wstring runText = m.editing ? L"试玩" : L"停止";
    int runWidth = D(46) + p.measure(runText, f.strong);
    // Narrow windows (or high DPI) drop labels: first on the right-hand buttons, then on the modes.
    auto modeChoices = [&](bool labels) {
        auto list = modes;
        if (!labels)
            for (auto& c : list)
                c.text = L"";
        return list;
    };
    auto wideWidth = [&](bool labels) {
        int width = 0;
        for (const auto& w : wide)
            width += (labels ? D(40) + p.measure(w.text, f.body) : bh) + D(4);
        return width;
    };
    int fixedLeft = D(10) + 10 * (bh + D(2)) + 3 * D(14) + D(2);
    int fixedRight = D(10) + runWidth + D(8) + bh + D(4);
    bool modeLabels = true, wideLabels = true;
    if (fixedLeft + segmentsWidth(modeChoices(true)) + wideWidth(true) + fixedRight + D(12) > l.toolbar.w)
        wideLabels = false;
    if (fixedLeft + segmentsWidth(modeChoices(true)) + wideWidth(false) + fixedRight + D(12) > l.toolbar.w)
        modeLabels = false;
    tool(WorkspaceAction::New, 0, Icon::New, L"新建工程…");
    tool(WorkspaceAction::Open, 0, Icon::Open, L"打开工程…");
    tool(WorkspaceAction::Save, 0, Icon::Save, m.dirty ? L"保存工程（有未保存的修改）  Ctrl+S" : L"保存工程  Ctrl+S",
         m.dirty ? Mark : 0);
    separator();
    bool editable = m.editing;
    tool(WorkspaceAction::Undo, 0, Icon::Undo, L"撤销  Ctrl+Z", m.canUndo && editable ? 0 : Disabled);
    tool(WorkspaceAction::Redo, 0, Icon::Redo, L"重做  Ctrl+Y", m.canRedo && editable ? 0 : Disabled);
    separator();
    auto shownModes = modeChoices(modeLabels);
    int modesWidth = segmentsWidth(shownModes);
    segments({x, by - D(1), modesWidth, bh + D(2)}, WorkspaceAction::Mode, shownModes, m.mode, false);
    x += modesWidth + D(2);
    separator();
    const Choice tools[] = {{L"", Icon::Pencil, L"画笔：拖动绘制"},
                            {L"", Icon::Rectangle, L"矩形：拖动填充一块区域"},
                            {L"", Icon::Bucket, L"油漆桶：填充相连的区域"},
                            {L"", Icon::Select, L"框选：Ctrl+C / Ctrl+V 复制粘贴，Delete 擦除"},
                            {L"", Icon::Picker, L"吸管：点击读取图块"}};
    for (int i = 0; i < 5; ++i)
        tool(WorkspaceAction::Tool, i, tools[i].icon,
             m.mode != 0 ? std::wstring(tools[i].tip) + L"（切换到「地形」模式后可用）" : tools[i].tip,
             (m.mode != 0 ? Disabled : 0) | (m.mode == 0 && m.tool == i ? Selected : 0));
    // Right-aligned group.
    int right = l.toolbar.right() - D(10);
    button({right - runWidth, by, runWidth, bh}, WorkspaceAction::Run, 0, runText,
           m.editing ? Icon::Play : Icon::Stop, m.editing ? L"从当前地图开始试玩  F5" : L"结束试玩并返回编辑  F2",
           m.editing ? Positive : Negative);
    right -= runWidth + D(8);
    button({right - bh, by, bh, bh}, WorkspaceAction::Theme, 0, L"", m.dark ? Icon::Sun : Icon::Moon,
           m.dark ? L"切换到浅色主题" : L"切换到深色主题", Quiet);
    right -= bh + D(4);
    for (const auto& w : wide) {
        int width = wideLabels ? D(40) + p.measure(w.text, f.body) : bh;
        button({right - width, by, width, bh}, w.action, 0, wideLabels ? w.text : L"", w.icon, w.tip,
               Quiet | (w.action == WorkspaceAction::Settings && m.inspector ? Selected : 0));
        right -= width + D(4);
    }

    // Dock: mode panel above, project maps below.
    const auto& d = l.dock;
    fill(d, t.panel);
    divider({d.right() - 1, d.y, 1, d.h}, t.border);
    int pad = D(14), x0 = d.x + pad, w = d.w - 2 * pad - 1;
    int mapsHeight = std::clamp(d.h * 38 / 100, D(168), D(300));
    // The mode panel above has a minimum height (the terrain grid cannot shrink); on short windows
    // the map list gives way down to its header plus two rows.
    int gridCell = (w - 3 * D(6)) / 4;
    int modeNeeds = m.mode == 0 ? D(12 + 26 + 10 + 32 + 12) + 4 * gridCell + 3 * D(6) + D(14 + 22 + 32 + 12)
                                : D(12 + 26 + 26) + 2 * D(34) + D(52);
    int mapsTop = std::max(d.bottom() - mapsHeight, std::min(d.y + modeNeeds, d.bottom() - D(44 + 31 * 2 + 8)));
    int y = d.y + D(12);
    const wchar_t* titles[] = {L"图块", L"地图事件", L"对象模板", L"弹幕发射器", L"平台机关"};
    label({x0, y, w - D(90), D(26)}, titles[std::clamp(m.mode, 0, 4)], Strong);
    if (m.mode == 0) {
        int iw = D(30) + p.measure(L"导入", f.small);
        button({x0 + w - iw, y, iw, D(26)}, WorkspaceAction::ImportTiles, 0, L"导入", Icon::Import,
               L"导入图块图集（PNG，4 × 4 图块）", Quiet | Small);
    } else {
        auto count = std::to_wstring(m.items.size());
        int bw = std::max(D(26), badgeWidth(count));
        badge({x0 + w - bw, y + D(3), bw, D(20)}, count, Muted);
    }
    y += D(26);
    if (m.mode == 0) {
        y += D(10);
        segments({x0, y, w, D(32)}, WorkspaceAction::Layer,
                 {{L"后景", Icon::None, L"后景层：绘制在角色身后"},
                  {L"世界", Icon::None, L"世界层：可碰撞的主地形"},
                  {L"前景", Icon::None, L"前景层：遮挡在角色前方"}},
                 m.layer, true);
        y += D(32) + D(12);
        int gap = D(6), cell = (w - 3 * gap) / 4;
        for (int i = 0; i < 16; ++i) {
            Node n;
            n.kind = Kind::Custom;
            n.box = {x0 + i % 4 * (cell + gap), y + i / 4 * (cell + gap), cell, cell};
            n.action = act(WorkspaceAction::Tile);
            n.value = i;
            n.tip = L"图块 " + std::to_wstring(i + 1);
            n.flags = (i >= m.tileCount ? Disabled : 0) | (i == m.tile ? Selected : 0);
            view.add(n);
        }
        y += 4 * (cell + gap) - gap + D(14);
        label({x0, y, w, D(18)}, L"笔刷类型", Small | Muted);
        y += D(22);
        segments({x0, y, w, D(32)}, WorkspaceAction::Terrain,
                 {{L"实心", Icon::None, L"实心方块：可以站立的地面"},
                  {L"单向", Icon::None, L"单向平台：只能从上方落下"},
                  {L"尖刺", Icon::None, L"尖刺：接触即受伤"},
                  {L"擦除", Icon::None, L"擦除：清除图块与碰撞（也可右键）"}},
                 m.brush, true);
        y += D(32) + D(12);
        if (y + D(36) <= mapsTop)
            label({x0, y, w, D(36)}, L"左键拖动铺设，右键擦除；\n滚轮平移地图。", Small | Faint | Wrap);
    } else {
        const wchar_t* hints[] = {L"", L"单击空地部署 · 拖动移动 · 双击编辑", L"先选模板，再在地图上单击放置",
                                  L"拖动现有发射器 · 双击编辑波次", L"选择机关后拖动 · 双击编辑属性"};
        label({x0, y, w, D(18)}, hints[std::clamp(m.mode, 0, 4)], Small | Faint);
        y += D(26);
        int footer = D(52), listBottom = mapsTop - footer;
        Box listBox{d.x + D(6), y, d.w - D(13), std::max(0, listBottom - y)};
        int area = view.scrollArea(10 + m.mode, listBox, D(34) * 3);
        Icon icons[] = {Icon::Terrain, Icon::Event, Icon::Object, Icon::Barrage, Icon::Feature};
        int rowY = y;
        // Leave room for the scrollbar thumb when the list overflows.
        int rowWidth = listBox.w - (int(m.items.size()) * D(34) > listBox.h ? D(10) : D(4));
        for (int i = 0; i < int(m.items.size()); ++i) {
            if (i == m.selectedItem)
                view.reveal(area, i, rowY, D(32));
            Node n;
            n.kind = Kind::Row;
            n.box = {listBox.x, rowY, rowWidth, D(32)};
            n.action = act(WorkspaceAction::Item);
            n.value = i;
            n.text = m.items[i].name;
            n.note = m.items[i].note;
            n.icon = icons[std::clamp(m.mode, 0, 4)];
            n.flags = i == m.selectedItem ? Selected : 0;
            n.scroll = area;
            view.add(n);
            rowY += D(34);
        }
        view.finishArea(area, rowY);
        if (m.items.empty()) {
            const wchar_t* empty[][2] = {{L"", L""},
                                         {L"当前地图还没有事件", L"在地图上单击空地即可部署"},
                                         {L"还没有对象模板", L"从「作品 → 对象与模板」创建"},
                                         {L"当前地图没有发射器", L"从「作品 → 弹幕与波次」新建"},
                                         {L"当前地图没有机关", L"从「作品 → 平台动作 / 机关」放置"}};
            int cy = listBox.y + std::max(0, listBox.h / 2 - D(30));
            label({x0, cy, w, D(22)}, empty[m.mode][0], Muted | Center);
            label({x0, cy + D(22), w, D(20)}, empty[m.mode][1], Faint | Small | Center);
        }
        button({x0, listBottom + D(10), w, D(32)}, WorkspaceAction::ItemEdit, 0, L"编辑选中项", Icon::Edit,
               L"打开详细设置（也可以双击列表项）", m.selectedItem < 0 ? Disabled : 0);
    }
    divider({d.x, mapsTop, d.w - 1, 1}, t.border);
    y = mapsTop + D(10);
    label({x0, y, w - D(70), D(26)}, L"地图", Strong);
    button({x0 + w - D(26), y, D(26), D(26)}, WorkspaceAction::MapNew, 0, L"", Icon::Plus,
           m.maps.size() >= 8 ? L"已达到 8 张地图上限" : L"新建地图", Quiet | Small | (m.maps.size() >= 8 ? Disabled : 0));
    button({x0 + w - D(56), y, D(26), D(26)}, WorkspaceAction::MapSettings, 0, L"", Icon::Sliders,
           L"地图、卷轴与传送设置…", Quiet | Small);
    y += D(34);
    Box mapBox{d.x + D(6), y, d.w - D(13), std::max(0, d.bottom() - D(8) - y)};
    int area = view.scrollArea(2, mapBox, D(30) * 3);
    int mapWidth = mapBox.w - ((int(m.maps.size()) + 1) * D(31) > mapBox.h ? D(10) : D(4));
    {
        Node root;
        root.kind = Kind::Row;
        root.box = {mapBox.x, y, mapWidth, D(30)};
        root.text = m.project;
        root.icon = Icon::Folder;
        root.flags = Header;
        root.scroll = area;
        view.add(root);
        y += D(31);
    }
    for (int i = 0; i < int(m.maps.size()); ++i) {
        if (i == m.editMap)
            view.reveal(area, i, y, D(30));
        Node n;
        n.kind = Kind::Row;
        n.box = {mapBox.x, y, mapWidth, D(30)};
        n.action = act(WorkspaceAction::MapRow);
        n.value = i;
        n.text = m.maps[i];
        n.badge = i == m.startMap ? L"起始" : L"";
        n.icon = Icon::Map;
        n.indent = D(16);
        n.flags = i == m.editMap ? Selected : 0;
        n.tip = L"单击切换地图（自动保存）· 双击打开地图设置";
        n.scroll = area;
        view.add(n);
        y += D(31);
    }
    view.finishArea(area, y);
    {
        Node split;
        split.kind = Kind::Splitter;
        split.box = l.splitter;
        split.action = act(WorkspaceAction::Splitter);
        view.add(split);
    }

    if (m.inspector && l.inspector.w > 0) {
        const auto& r = l.inspector;
        fill(r, t.panel);
        divider({r.x, r.y, 1, r.h}, t.border);
        int px = r.x + D(16), pw = r.w - D(32);
        label({px, r.y + D(12), pw - D(30), D(26)}, L"场景设置", Strong);
        button({r.right() - D(16) - D(26), r.y + D(12), D(26), D(26)}, WorkspaceAction::CloseInspector, 0, L"",
               Icon::Close, L"关闭面板", Quiet | Small);
        int top = r.y + D(46);
        divider({r.x + 1, top, r.w - 1, 1}, t.divider);
        Box box{r.x + 1, top + 1, r.w - 1, r.bottom() - top - 1};
        int area = view.scrollArea(3, box, D(40) * 3);
        int py = top + D(8);
        bool first = true;
        for (const auto& prop : m.properties) {
            Node n;
            n.action = act(WorkspaceAction::Property);
            n.value = prop.id;
            n.text = prop.label;
            n.tip = prop.tip;
            n.scroll = area;
            n.flags = prop.enabled ? 0 : Disabled;
            switch (prop.kind) {
            case Property::Section:
                if (!first) {
                    divider({r.x + D(16), py + D(6), pw, 1}, t.divider);
                    view.nodes.back().scroll = area;
                    py += D(14);
                }
                n.kind = Kind::Section;
                n.action = 0;
                n.box = {px, py, pw, D(30)};
                py += D(32);
                break;
            case Property::Choice:
                n.kind = Kind::Dropdown;
                n.note = propertyText(prop);
                n.box = {px, py, pw, D(36)};
                py += D(38);
                break;
            case Property::Toggle:
                n.kind = Kind::Toggle;
                n.box = {px, py, pw, D(32)};
                n.flags |= prop.on ? Selected : 0;
                py += D(34);
                break;
            case Property::Slider:
                n.kind = Kind::Slider;
                n.note = propertyText(prop);
                n.number = prop.value;
                n.minimum = prop.minimum;
                n.maximum = prop.maximum;
                n.box = {px, py, pw, D(46)};
                py += D(50);
                break;
            case Property::Button:
                n.kind = Kind::Button;
                n.flags |= prop.on ? Selected : 0;
                n.box = {px, py + D(2), pw, D(32)};
                py += D(40);
                break;
            case Property::Hint:
                n.kind = Kind::Label;
                n.action = 0;
                n.flags = Small | Faint | Wrap;
                n.box = {px, py, pw, D(36)};
                py += D(38);
                break;
            }
            first = false;
            view.add(n);
        }
        view.finishArea(area, py + D(12));
    }

    // Canvas header: breadcrumb and state.
    const auto& h = l.header;
    fill(h, t.window);
    x = h.x + D(12);
    int pw = p.measure(m.project, f.body);
    label({x, h.y, pw + D(2), h.h}, m.project, Muted);
    x += pw + D(6);
    label({x, h.y + (h.h - D(14)) / 2, D(14), D(14)}, L"", Faint, Icon::ChevronRight);
    x += D(20);
    int mw = p.measure(m.map, f.strong);
    label({x, h.y, mw + D(2), h.h}, m.map, Strong);
    x += mw + D(12);
    std::wstring state = m.editing ? L"编辑中" : L"试玩中";
    int sw = badgeWidth(state);
    badge({x, h.y + (h.h - D(22)) / 2, sw, D(22)}, state, m.editing ? 0 : Positive);
    x += sw + D(6);
    if (m.dirty) {
        int uw = badgeWidth(L"未保存");
        badge({x, h.y + (h.h - D(22)) / 2, uw, D(22)}, L"未保存", Warn);
        x += uw + D(6);
    }
    label({x + D(12), h.y, std::max(0, h.right() - D(16) - x - D(12)), h.h},
          m.editing ? L"滚轮平移 · 右键擦除 · Ctrl+Z 撤销" : L"F2 返回编辑 · T 回溯 · R 快退", Small | Faint | Right);

    // Status bar.
    const auto& st = l.status;
    fill(st, t.chrome);
    divider({0, st.y, st.w, 1}, t.border);
    x = D(12);
    std::wstring_view rest = m.status;
    const std::wstring_view separatorText = L"   |   ";
    int hintWidth = p.measure(L"F5 试玩 · F2 编辑 · F6 重载", f.small) + D(16);
    bool first = true;
    while (!rest.empty() && x < st.right() - hintWidth) {
        auto cut = rest.find(separatorText);
        std::wstring part(rest.substr(0, cut));
        rest = cut == std::wstring_view::npos ? std::wstring_view{} : rest.substr(cut + separatorText.size());
        if (!first) {
            divider({x, st.y + D(8), 1, st.h - D(16)}, t.border);
            x += D(11);
        }
        int pw2 = std::min(p.measure(part, f.small) + D(2), st.right() - hintWidth - x);
        label({x, st.y + 1, pw2, st.h - 1}, part, Small | (first ? 0 : Muted));
        x += pw2 + D(10);
        first = false;
    }
    label({st.right() - hintWidth, st.y + 1, hintWidth - D(12), st.h - 1}, L"F5 试玩 · F2 编辑 · F6 重载",
          Small | Faint | Right);
}

void paintWorkspaceCustom(Painter& p, const Node& n, Box b, const View& view, const Theme& t, const Fonts& f,
                          const TileAtlas& atlas) {
    float s = f.scale;
    if (n.action == 0 && n.value == 1) {
        p.round(b, dp(5, s), t.accent);
        p.text(b, L"E", f.mark, t.onAccent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    if (n.action != int(WorkspaceAction::Tile))
        return;
    bool hovered = view.hot == Key{n.action, n.value} && !(n.flags & Disabled);
    bool selected = n.flags & Selected;
    float radius = dp(6, s);
    p.round(b, radius, t.sunken, hovered && !selected ? t.borderStrong : t.border);
    int inset = dp(4, s);
    Box image = b.inset(inset, inset);
    // Pixel art keeps whole-pixel steps: largest integer multiple of the frame, centred.
    if (atlas.frame > 0 && image.w >= atlas.frame) {
        int size = image.w / atlas.frame * atlas.frame;
        image = {b.x + (b.w - size) / 2, b.y + (b.h - size) / 2, size, size};
    }
    int columns = atlas.frame > 0 ? atlas.width / atlas.frame : 0;
    int rows = atlas.frame > 0 ? atlas.height / atlas.frame : 0;
    if (atlas.bitmap && columns > 0 && n.value < columns * rows)
        p.image(atlas.bitmap, n.value % columns * atlas.frame, n.value / columns * atlas.frame, atlas.frame,
                atlas.frame, image);
    else
        p.text(image, std::to_wstring(n.value + 1), f.small, t.faint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (n.flags & Disabled)
        p.round(b, radius, alpha(t.panel, .72f));
    if (selected) {
        p.round(b, radius, {0, 0, 0, 0}, t.accent, float(std::max(2, dp(2, s))));
        int size = dp(16, s);
        Box tick{b.right() - size + dp(3, s), b.y - dp(3, s), size, size};
        p.circle(tick.x + size / 2.0f, tick.y + size / 2.0f, size / 2.0f, t.accent);
        p.icon(Icon::Check, tick.inset(dp(3, s), dp(3, s)), t.onAccent);
    }
}
} // namespace echo
