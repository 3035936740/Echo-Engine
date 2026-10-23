#include "echo/app.hpp"
#include "echo/control.hpp"
#include <commctrl.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace echo {
namespace {
std::wstring readWide(HWND h) {
    std::wstring s(GetWindowTextLengthW(h) + 1, 0);
    GetWindowTextW(h, s.data(), int(s.size()));
    s.resize(wcslen(s.c_str()));
    return s;
}
std::string readText(HWND h) {
    auto s = readWide(h);
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string text(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), text.data(), n, nullptr, nullptr);
    return text;
}
int readInt(HWND h) {
    auto s = readWide(h);
    std::size_t used = 0;
    long long value = std::stoll(s, &used);
    if (s.find_first_not_of(L" \t\r\n", used) != std::wstring::npos ||
        value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::runtime_error("Enter a signed 32-bit integer");
    return int(value);
}
struct Form {
    App& a;
    HWND h;
    void label(const wchar_t* s, int x, int y, int width = 400, int id = 0) {
        a.control(L"STATIC", s, id, x, y, width, 24, 0, h);
    }
    void edit(int id, int x, int y, int width, int height = 29, bool multiline = false) {
        a.control(L"EDIT", L"", id, x, y, width, height,
                  WS_BORDER | WS_TABSTOP |
                      (multiline ? WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN : 0),
                  h);
    }
    void button(int id, const wchar_t* s, int x, int y, int width = 180) {
        a.control(L"BUTTON", s, id, x, y, width, 31, WS_TABSTOP, h);
    }
    void box(int id, const std::vector<std::wstring>& items, int x, int y, int width = 400) {
        auto c = a.control(L"COMBOBOX", L"", id, x, y, width, 260,
                           CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, h);
        for (const auto& item : items)
            SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageW(c, CB_SETITEMHEIGHT, -1, 25);
        SendMessageW(c, CB_SETCURSEL, 0, 0);
    }
};
HWND popup(App& a, const wchar_t* title, int width, int height) {
    return CreateWindowW(L"EchoEngineWindow", title,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                             ((a.verifyMode || a.smoke) ? 0 : WS_VISIBLE),
                         100, 80, width, height, a.window, nullptr, a.instance, &a);
}
} // namespace
void App::showDatabaseEditor() {
    if (eventEditor && eventId >= 0)
        eventCommand(EventApply);
    command(Edit);
    if (databaseEditor && IsWindow(databaseEditor)) {
        SetForegroundWindow(databaseEditor);
        return;
    }
    databaseEditor = popup(*this, L"游戏数据库 · 开关 / 变量 / 物品 / 任务", 1040, 760);
    Form f{*this, databaseEditor};
    f.label(L"稳定 ID 引用 · 改名不影响事件，已删除 ID 不复用", 20, 18, 980);
    f.box(DataCategory,
          {L"全局开关 · 128项", L"整数变量 · 64项", L"物品 · 最多64种", L"任务 · 最多64项"}, 20, 54,
          280);
    control(L"LISTBOX", L"", DataList, 20, 98, 280, 394,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, databaseEditor);
    f.box(DataPreset, {L"普通物品（上限99）", L"唯一钥匙（上限1）"}, 20, 504, 280);
    f.button(DataNew, L"+ 新建", 20, 554, 134);
    f.button(DataDelete, L"删除", 166, 554, 134);
    f.button(DataUndo, L"数据库撤销", 20, 597, 134);
    f.button(DataRedo, L"重做", 166, 597, 134);
    f.button(DataOpenObjects, L"角色 / 敌人模板", 20, 640, 134);
    f.button(DataOpenAnimations, L"精灵 / 动画库", 166, 640, 134);
    f.label(L"名称（最多63个UTF-8字节）", 330, 54, 650);
    f.edit(DataName, 330, 84, 650);
    f.label(L"初始值", 330, 133, 650, DataInitialLabel);
    f.edit(DataInitial, 330, 163, 650);
    f.box(DataStatus, {L"未接受", L"进行中", L"已完成", L"失败"}, 330, 163, 650);
    f.label(L"上限 / 目标", 330, 212, 650, DataLimitLabel);
    f.edit(DataLimit, 330, 242, 650);
    f.button(DataAuto, L"达到目标自动完成任务", 330, 285, 650);
    f.label(L"说明 / 备注（最多255个UTF-8字节）", 330, 332, 650);
    f.edit(DataNote, 330, 362, 650, 120, true);
    f.label(L"", 330, 504, 650, DataRuntime);
    MoveWindow(controls[DataRuntime], 330, 504, 650, 72, TRUE);
    f.button(DataApply, L"应用定义 · 新试玩生效 / 重新开始历史", 330, 597, 650);
    f.label(L"物品 / 任务可在事件指令中按名称选择；复杂规则可用 Lua。", 330, 645, 650);
    f.label(L"Ctrl+S保存工程；数据库撤销最多16步，不改变已分配 ID。", 20, 693, 980);
    dataCategory = dataSlot = 0;
    databaseCommand(DataCategory);
}
void App::databaseForm() {
    bool collection = dataCategory >= 2, quest = dataCategory == 3;
    int count = dataCategory == 0   ? 128
                : dataCategory == 1 ? 64
                : quest             ? int(database.quests.size())
                                    : int(database.items.size());
    bool selected = dataSlot >= 0 && dataSlot < count;
    EnableWindow(controls[DataNew], collection && count < 64);
    EnableWindow(controls[DataDelete], collection && selected);
    EnableWindow(controls[DataUndo], !databaseUndo.empty());
    EnableWindow(controls[DataRedo], !databaseRedo.empty());
    for (int id : {DataName, DataNote, DataInitial, DataLimit, DataAuto, DataStatus, DataApply})
        EnableWindow(controls[id], selected);
    ShowWindow(controls[DataInitial], quest ? SW_HIDE : SW_SHOW);
    ShowWindow(controls[DataStatus], quest ? SW_SHOW : SW_HIDE);
    ShowWindow(controls[DataLimit], collection ? SW_SHOW : SW_HIDE);
    ShowWindow(controls[DataLimitLabel], collection ? SW_SHOW : SW_HIDE);
    ShowWindow(controls[DataAuto], quest ? SW_SHOW : SW_HIDE);
    ShowWindow(controls[DataPreset], collection ? SW_SHOW : SW_HIDE);
    SetWindowTextW(controls[DataInitialLabel], dataCategory == 0   ? L"初始值：0关 / 1开"
                                               : dataCategory == 1 ? L"初始整数值"
                                               : quest             ? L"新游戏任务状态"
                                                                   : L"新游戏持有数量");
    SetWindowTextW(controls[DataLimitLabel],
                   quest ? L"任务目标进度（1..1000000）" : L"持有上限（1..1000000；钥匙可设1）");
    if (!selected) {
        SetWindowTextW(controls[DataName], L"");
        SetWindowTextW(controls[DataNote], L"");
        setNumber(DataInitial, 0);
        setNumber(DataLimit, 1);
        SetWindowTextW(controls[DataRuntime], L"点击「新建」，然后在事件中选择这项定义。");
        return;
    }
    const char *name, *note;
    int initial = 0, limit = 1;
    std::wstring runtime;
    if (!collection) {
        const auto& e = dataCategory ? database.variables[dataSlot] : database.switches[dataSlot];
        name = e.name.c_str();
        note = e.note.c_str();
        initial = e.initial;
        runtime = L"当前试玩值：" + std::to_wstring(dataCategory
                                                        ? simulation->world().events.variables[dataSlot]
                                                        : simulation->world().events.switches[dataSlot]);
    } else if (quest) {
        const auto& e = database.quests[dataSlot];
        name = e.name.c_str();
        note = e.note.c_str();
        limit = e.target;
        SendMessageW(controls[DataStatus], CB_SETCURSEL, int(e.initial), 0);
        SendMessageW(controls[DataAuto], BM_SETCHECK, e.autoComplete, 0);
        if (auto* q = findQuest(simulation->world(), e.id))
            runtime =
                L"当前试玩：" +
                std::wstring(std::array{L"未接受", L"进行中", L"已完成", L"失败"}[int(q->status)]) +
                L" · 进度 " + std::to_wstring(q->progress) + L" / " + std::to_wstring(q->target);
    } else {
        const auto& e = database.items[dataSlot];
        name = e.name.c_str();
        note = e.note.c_str();
        initial = e.initial;
        limit = e.limit;
        if (auto* i = findItem(simulation->world(), e.id))
            runtime = L"当前试玩持有：" + std::to_wstring(i->count) + L" / " + std::to_wstring(i->limit);
    }
    if (collection)
        runtime += L"\r\n最近操作结果：" +
                   std::wstring(
                       std::array{L"成功", L"定义不存在", L"达到持有上限", L"数量不足",
                                  L"任务不在进行中"}[int(simulation->world().progression.lastResult)]) +
                   L" · 拒绝次数：" + std::to_wstring(simulation->world().progression.rejected);
    SetWindowTextW(controls[DataName], wide(name).c_str());
    SetWindowTextW(controls[DataNote], wide(note).c_str());
    setNumber(DataInitial, float(initial));
    setNumber(DataLimit, float(limit));
    SetWindowTextW(controls[DataRuntime], runtime.c_str());
}
void App::databaseCommit(Database next, bool history) {
    validateDatabase(next);
    auto references = [&](const EventMap& map, const std::string& location) {
        for (const auto& event : map.events)
            for (std::size_t p = 0; p < event.pages.size(); ++p)
                for (std::size_t c = 0; c < event.pages[p].commands.size(); ++c) {
                    const auto& cmd = event.pages[p].commands[c];
                    if (eventProgression(cmd.op) &&
                        !progressionReferenceValid(next, eventQuest(cmd.op), cmd.reference))
                        throw std::runtime_error(location + " event " + std::to_string(event.id) +
                                                 " page " + std::to_string(p + 1) + " command " +
                                                 std::to_string(c + 1) + " uses deleted item/quest #" +
                                                 std::to_string(cmd.reference));
                }
    };
    for (std::size_t m = 0; m < eventMaps.size(); ++m)
        references(int(m) == editMap ? editor.events : eventMaps[m], "map " + std::to_string(m + 1));
    references(editor.common, "common events");
    if (eventEditor && eventId >= 0)
        references(EventMap{{eventDraft}}, "open event draft");
    if (saveDatabase(next) == saveDatabase(database))
        return;
    if (history) {
        if (databaseUndo.size() == 16)
            databaseUndo.erase(databaseUndo.begin());
        databaseUndo.push_back(database);
        databaseRedo.clear();
    }
    database = std::move(next);
    applyDatabaseDefaults(*initial_, database);
    simulation->reset(*initial_, HistoryClearReason::DatabaseDefinitions);
    editor.dirty = true;
    refreshDataLabels();
    databaseCommand(DataCategory);
    refreshChrome();
    message = "Database applied; fresh timeline. Ctrl+S saves project";
}
void App::refreshDataLabels() {
    if (!eventEditor)
        return;
    for (auto pair : {std::pair{EventSwitchCondition, false}, std::pair{EventVariableCondition, true}}) {
        int chosen = selection(pair.first);
        auto h = controls[pair.first];
        SendMessageW(h, CB_RESETCONTENT, 0, 0);
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"不限制"));
        for (int i = 0; i < (pair.second ? 64 : 128); ++i) {
            auto text = wide(dataLabel(database, pair.second, i));
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(h, CB_SETCURSEL, chosen, 0);
    }
    if (eventId >= 0)
        eventPageForm();
}
void App::databaseCommand(int id) {
    if (id == DatabaseEditor) {
        showDatabaseEditor();
        return;
    }
    if (!databaseEditor)
        return;
    if (id == DataOpenObjects) {
        objectCommand(ObjectsEditor);
        return;
    }
    if (id == DataOpenAnimations) {
        animationCommand(AnimationsEditor);
        return;
    }
    if (id == DataCategory) {
        dataCategory = std::max(0, selection(DataCategory));
        int selectedPreset = std::max(0, selection(DataPreset));
        SendMessageW(controls[DataPreset], CB_RESETCONTENT, 0, 0);
        std::vector<std::wstring> presets =
            dataCategory == 3 ? std::vector<std::wstring>{L"默认任务（目标1）", L"收集任务（目标3）",
                                                          L"手动结算（目标3）"}
                              : std::vector<std::wstring>{L"普通物品（上限99）", L"唯一钥匙（上限1）"};
        for (const auto& p : presets)
            SendMessageW(controls[DataPreset], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.c_str()));
        SendMessageW(controls[DataPreset], CB_SETCURSEL,
                     std::min(selectedPreset, int(presets.size()) - 1), 0);
        int old = dataSlot;
        dataSlot = -1;
        SendMessageW(controls[DataList], LB_RESETCONTENT, 0, 0);
        auto add = [&](const std::string& text) {
            auto label = wide(text);
            SendMessageW(controls[DataList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        };
        if (dataCategory < 2)
            for (int i = 0; i < (dataCategory ? 64 : 128); ++i)
                add(dataLabel(database, dataCategory == 1, i));
        else if (dataCategory == 2)
            for (const auto& item : database.items)
                add(progressionLabel(database, false, item.id));
        else
            for (const auto& quest : database.quests)
                add(progressionLabel(database, true, quest.id));
        int count = int(SendMessageW(controls[DataList], LB_GETCOUNT, 0, 0));
        if (count)
            dataSlot = std::clamp(old, 0, count - 1);
        SendMessageW(controls[DataList], LB_SETCURSEL, dataSlot, 0);
        databaseForm();
        return;
    }
    if (id == DataList) {
        dataSlot = int(SendMessageW(controls[DataList], LB_GETCURSEL, 0, 0));
        databaseForm();
        return;
    }
    if (id == DataUndo || id == DataRedo) {
        auto& from = id == DataUndo ? databaseUndo : databaseRedo;
        auto& to = id == DataUndo ? databaseRedo : databaseUndo;
        if (from.empty())
            return;
        auto next = from.back();
        next.nextItemId = std::max(next.nextItemId, database.nextItemId);
        next.nextQuestId = std::max(next.nextQuestId, database.nextQuestId);
        auto previous = database;
        databaseCommit(std::move(next), false);
        from.pop_back();
        if (to.size() == 16)
            to.erase(to.begin());
        to.push_back(std::move(previous));
        databaseForm();
        return;
    }
    if (id == DataNew && dataCategory >= 2) {
        auto next = database;
        bool quest = dataCategory == 3;
        if ((quest ? next.quests.size() : next.items.size()) >= 64)
            throw std::runtime_error("Database capacity is 64");
        auto fresh = allocateDataId(next, quest);
        if (quest) {
            QuestDefinition q;
            q.id = fresh;
            q.name.set("新任务 " + std::to_string(fresh));
            if (selection(DataPreset) > 0)
                q.target = 3;
            q.autoComplete = selection(DataPreset) != 2;
            next.quests.push_back(q);
        } else {
            ItemDefinition item;
            item.id = fresh;
            item.name.set("新物品 " + std::to_string(fresh));
            if (selection(DataPreset) == 1)
                item.limit = 1;
            next.items.push_back(item);
        }
        dataSlot = int(quest ? next.quests.size() : next.items.size()) - 1;
        databaseCommit(std::move(next));
        return;
    }
    if (dataSlot < 0)
        return;
    if (id == DataDelete && dataCategory >= 2) {
        auto next = database;
        if (dataCategory == 3)
            next.quests.erase(next.quests.begin() + dataSlot);
        else
            next.items.erase(next.items.begin() + dataSlot);
        databaseCommit(std::move(next));
        return;
    }
    if (id != DataApply)
        return;
    auto name = readText(controls[DataName]), note = readText(controls[DataNote]);
    if (name.size() >= 64 || note.size() >= 256)
        throw std::runtime_error("Name / note too long");
    auto next = database;
    if (dataCategory < 2) {
        auto& v = dataCategory ? next.variables[dataSlot] : next.switches[dataSlot];
        v.name.set(name);
        v.note.set(note);
        v.initial = readInt(controls[DataInitial]);
    } else if (dataCategory == 2) {
        auto& v = next.items[dataSlot];
        v.name.set(name);
        v.note.set(note);
        v.initial = readInt(controls[DataInitial]);
        v.limit = readInt(controls[DataLimit]);
    } else {
        auto& q = next.quests[dataSlot];
        q.name.set(name);
        q.note.set(note);
        q.target = readInt(controls[DataLimit]);
        q.initial = QuestStatus(selection(DataStatus));
        q.autoComplete = SendMessageW(controls[DataAuto], BM_GETCHECK, 0, 0) != 0;
    }
    databaseCommit(std::move(next));
}

void App::showMapTools() {
    command(Edit);
    if (mapToolsEditor && IsWindow(mapToolsEditor)) {
        SetForegroundWindow(mapToolsEditor);
        return;
    }
    mapToolsEditor = popup(*this, L"地图绘制 / 选区工具", 780, 650);
    Form f{*this, mapToolsEditor};
    f.label(L"当前图层操作 · 选区仅包含图块与对应世界碰撞", 20, 18, 700);
    f.box(MapToolMode, {L"画笔", L"矩形填充", L"油漆桶", L"框选", L"吸管"}, 20, 56, 340);
    f.box(MapBrushType, {L"实心地形", L"单向平台", L"尖刺"}, 382, 56, 340);
    SendMessageW(controls[MapBrushType], CB_SETCURSEL,
                 editor.terrain == Tile::OneWay  ? 1
                 : editor.terrain == Tile::Spike ? 2
                                                 : 0,
                 0);
    f.label(L"矩形/框选：拖动地图；油漆桶/吸管：单击地图", 20, 108, 700);
    f.button(MapCopy, L"复制选区 · Ctrl+C", 20, 151, 340);
    f.button(MapPaste, L"粘贴到最后点击处 · Ctrl+V", 382, 151, 340);
    f.button(MapFlipH, L"选区水平翻转", 20, 199, 340);
    f.button(MapFlipV, L"选区垂直翻转", 382, 199, 340);
    f.button(MapClear, L"擦除选区 · Delete", 20, 247, 340);
    f.button(MapAutoTile, L"四邻接自动地形", 382, 247, 340);
    f.label(L"自动地形使用专用16格图集：北1、东2、南4、西8，索引=邻接值+1。", 20, 305, 710);
    f.label(L"有选区时只更新选区；无选区时处理当前图层全部非空图块。", 20, 339, 710);
    f.button(MapGrid, L"显示编辑网格", 20, 387, 340);
    f.button(MapCollisions, L"显示独立碰撞轮廓", 382, 387, 340);
    f.button(MapLock, L"锁定当前图层绘制", 20, 435, 340);
    f.button(MapHide, L"仅在编辑器隐藏当前图层", 382, 435, 340);
    SendMessageW(controls[MapGrid], BM_SETCHECK, editorGrid, 0);
    SendMessageW(controls[MapCollisions], BM_SETCHECK, editorCollisions, 0);
    SendMessageW(controls[MapLock], BM_SETCHECK, editor.layerLocked[editor.layer], 0);
    SendMessageW(controls[MapHide], BM_SETCHECK, editorHidden[editor.layer], 0);
    f.label(L"隐藏和锁定不修改游戏碰撞；切换图层后窗口会读取当前图层设置。", 20, 492, 710);
    f.label(L"绘制/粘贴/翻转/自动地形均可撤销；不复制NPC/事件或灯光。", 20, 526, 710);
}
void App::mapToolsCommand(int id) {
    if (id == MapToolsEditor) {
        showMapTools();
        return;
    }
    if (id == MapToolMode || id == MapBrushType) {
        int row = selection(MapToolMode);
        editor.terrain = selection(MapBrushType) == 1   ? Tile::OneWay
                         : selection(MapBrushType) == 2 ? Tile::Spike
                                                        : Tile::Solid;
        editor.end();
        editor.tool = row == 0 ? int(editor.terrain) : 17 + row;
        SetFocus(canvas);
        refreshChrome();
        return;
    }
    if (id == MapCopy) {
        if (!editor.copySelection())
            throw std::runtime_error("Select tiles with the area selection tool first");
        message = "Tile selection copied";
        return;
    }
    if (id == MapPaste) {
        if (!editor.pasteSelection(int(brushX / tileSize), int(brushY / tileSize)))
            throw std::runtime_error("No copied tiles, locked layer, or paste extends outside the map");
    }
    if (id == MapClear)
        editor.clearSelection();
    if (id == MapFlipH)
        editor.flipSelection(true);
    if (id == MapFlipV)
        editor.flipSelection(false);
    if (id == MapAutoTile)
        editor.retileSelection();
    if (id == MapGrid)
        editorGrid = SendMessageW(controls[id], BM_GETCHECK, 0, 0) != 0;
    if (id == MapCollisions)
        editorCollisions = SendMessageW(controls[id], BM_GETCHECK, 0, 0) != 0;
    if (id == MapGrid || id == MapCollisions)
        saveWorkspaceSettings();
    if (id == MapLock)
        editor.layerLocked[editor.layer] = SendMessageW(controls[id], BM_GETCHECK, 0, 0) != 0;
    if (id == MapHide)
        editorHidden[editor.layer] = SendMessageW(controls[id], BM_GETCHECK, 0, 0) != 0;
    refreshChrome();
}
void App::showDebugger() {
    if (editing)
        command(Play);
    if (debuggerEditor && IsWindow(debuggerEditor)) {
        SetForegroundWindow(debuggerEditor);
        return;
    }
    debuggerEditor = popup(*this, L"试玩调试 · 世界状态 / 事件 / 历史", 1060, 940);
    Form f{*this, debuggerEditor};
    f.label(L"调试修改作用于当前试玩；作品初始值在命名数据库设置。", 20, 18, 1000);
    f.button(DebugPause, L"暂停模拟", 20, 57, 225);
    f.button(DebugStep, L"单步 1/60秒", 260, 57, 225);
    f.label(L"状态", 20, 98, 995, DebugSummary);
    control(L"LISTBOX", L"", DebugEvents, 20, 140, 995, 278,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, debuggerEditor);
    f.label(L"历史定位 · 定位后截断未来，再继续模拟", 20, 439, 995);
    control(TRACKBAR_CLASSW, L"", DebugTimeline, 20, 476, 995, 37, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
            debuggerEditor);
    f.button(DebugSeek, L"定位到所选历史", 20, 529, 300);
    f.button(DebugOldest, L"定位到最远历史", 337, 529, 300);
    f.button(SnapshotCompareEditor, L"\u5feb\u7167\u6bd4\u5bf9", 654, 529, 361);
    std::vector<std::wstring> values;
    for (int i = 0; i < 128; ++i)
        values.push_back(L"开关 " + wide(dataLabel(database, false, i)));
    for (int i = 0; i < 64; ++i)
        values.push_back(L"变量 " + wide(dataLabel(database, true, i)));
    f.box(DebugData, values, 20, 590, 520);
    f.edit(DebugValue, 558, 590, 185);
    f.button(DebugWrite, L"写入当前快照", 760, 590, 255);
    f.label(L"开关只接受0/1；暂停后才可写入。关闭窗口恢复模拟。", 20, 650, 995);
    f.label(L"", 20, 690, 995, DebugHistoryInfo);
    f.label(L"", 20, 724, 995, DebugHistoryReason);
    f.label(L"", 20, 758, 995, DebugHistoryClear);
    f.label(L"", 20, 792, 995, DebugControlSummary);
    f.label(L"", 20, 826, 995, DebugControlClocks);
    debugPaused = true;
    debugMap = -1;
    SendMessageW(controls[DebugPause], BM_SETCHECK, 1, 0);
    refreshDebugger();
}
void App::refreshDebugger() {
    if (!debuggerEditor || !simulation)
        return;
    const auto& w = simulation->world();
    int size = simulation->history().size();
    int oldMax = int(SendMessageW(controls[DebugTimeline], TBM_GETRANGEMAX, 0, 0));
    int oldPos = int(SendMessageW(controls[DebugTimeline], TBM_GETPOS, 0, 0));
    SendMessageW(controls[DebugTimeline], TBM_SETRANGEMAX, TRUE, std::max(0, size - 1));
    if (oldPos == oldMax && !GetCapture())
        SendMessageW(controls[DebugTimeline], TBM_SETPOS, TRUE, std::max(0, size - 1));
    SendMessageW(controls[DebugPause], BM_SETCHECK, debugPaused, 0);
    std::wostringstream summary;
    summary << std::fixed << std::setprecision(2) << L"时间 " << w.clock << L"秒 / 地图 "
            << w.activeMap + 1 << L" / 弹幕 " << w.bullets.count << L" / 历史 " << size
            << L" 帧 / 事件错误 " << w.events.errors << L" / 预算让出 " << w.events.budgetYields
            << L" / 页条件延期 " << w.events.pageConditionDeferrals;
    SetWindowTextW(controls[DebugSummary], summary.str().c_str());
    refreshHistoryDiagnostics();
    auto policy = resolveControl(w);
    const wchar_t* reasons[] = {L"事件请求", L"前台事件", L"对话",     L"切图",
                                L"受击",     L"回溯",     L"游戏暂停", L"结束"};
    std::wostringstream control;
    control << L"控制者：";
    if (!policy.owners.count)
        control << L"玩家";
    int shown = 0;
    for (const auto& owner : policy.owners) {
        if (shown++ == 3) {
            control << L"等 " << policy.owners.count << L" 项（见事件列表）";
            break;
        }
        control << reasons[int(owner.reason)];
        if (owner.event >= 0)
            control << (owner.event < 32 ? L" 地图#" : L" 公共#")
                    << (owner.event < 32 ? owner.event + 1 : owner.event - 31);
        control << L"  ";
    }
    SetWindowTextW(controls[DebugControlSummary], control.str().c_str());
    std::wostringstream clocks;
    clocks << L"物理 " << (policy.physics ? L"推进" : L"冻结") << L" / 事件 "
           << (policy.events ? L"推进" : L"冻结") << L" / 音频 " << (policy.audio ? L"推进" : L"冻结")
           << L" / 历史 " << (policy.history ? L"记录" : L"冻结") << L" / 许可 " << policy.allowed
           << L" / 待松键 " << w.controlInput.heldBlocked
           << (debugPaused ? L" · 调试器暂停：全部冻结" : L"");
    SetWindowTextW(controls[DebugControlClocks], clocks.str().c_str());
    SendMessageW(controls[DebugEvents], LB_RESETCONTENT, 0, 0);
    for (int slot = 0; slot < 48; ++slot) {
        bool common = slot >= 32;
        int id = common ? slot - 31 : slot + 1;
        const EventMap* definitions = common ? &commonEvents
                                      : w.activeMap >= 0 && w.activeMap < int(eventMaps.size())
                                          ? &eventMaps[w.activeMap]
                                          : nullptr;
        const auto* definition = definitions ? findEvent(*definitions, id) : nullptr;
        if (!definition)
            continue;
        const auto& r = common ? w.events.common[slot - 32] : w.events.local[slot];
        std::wostringstream line;
        line << (common ? L"公共" : L"地图") << L" #" << id << L" " << wide(definition->name.c_str())
             << L"  页 " << r.page + 1 << L" / 指令 " << r.pc + 1 << L"  "
             << (r.running ? L"运行" : L"空闲");
        if (r.controlActive)
            line << L" · 输入许可 " << r.controlAllowed;
        if (r.caller >= 0)
            line << L" · 调用者 " << (r.caller < 32 ? L"地图#" : L"公共#")
                 << (r.caller < 32 ? r.caller + 1 : r.caller - 31);
        if (r.pageConditionFault >= 0)
            line << L" · 页条件错误 #" << r.pageConditionFault + 1 << L"（重开 / 重入可重试）";
        if (r.scriptFault)
            line << L" · Lua错误已停止（换页 / 重开可重试）";
        if (r.waitDialogue)
            line << L" · 等待对话";
        if (r.loopDepth > 0) {
            const auto& loop = r.loops[r.loopDepth - 1];
            line << L" · 循环" << r.loopDepth << L"层 / ";
            if (loop.remaining)
                line << L"剩余" << loop.remaining << L"次";
            else
                line << L"持续";
        }
        if (r.waitCommon >= 0)
            line << L" · 等待公共 #" << r.waitCommon + 1;
        if (r.wait > 0)
            line << L" · 等待 " << std::fixed << std::setprecision(2) << r.wait << L"秒";
        if (r.moveRemaining > 0)
            line << L" · 移动 " << r.moveRemaining << L"秒";
        if (r.moveBlocked)
            line << L" · 移动被阻挡";
        if (r.triggerPending)
            line << L" · 等待区域触发";
        auto text = line.str();
        SendMessageW(controls[DebugEvents], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    if (GetFocus() != controls[DebugValue]) {
        int row = std::max(0, selection(DebugData));
        int value = row < 128 ? w.events.switches[row] : w.events.variables[row - 128];
        SetWindowTextW(controls[DebugValue], std::to_wstring(value).c_str());
    }
    renderer.debugCollisions = editorCollisions;
}
void App::debuggerCommand(int id) {
    if (id == DebuggerEditor) {
        showDebugger();
        return;
    }
    if (!debuggerEditor)
        return;
    if (id != DebugPause && id != DebugStep && id != DebugSeek && id != DebugOldest &&
        id != DebugWrite && id != DebugData)
        return;
    if (id == DebugPause)
        debugPaused = SendMessageW(controls[id], BM_GETCHECK, 0, 0) != 0;
    if (id == DebugStep) {
        debugPaused = true;
        simulation->tick({});
    }
    if (id == DebugSeek || id == DebugOldest) {
        debugPaused = true;
        int cursor =
            id == DebugOldest ? 0 : int(SendMessageW(controls[DebugTimeline], TBM_GETPOS, 0, 0));
        simulation->seekHistory(cursor);
        camera.update(simulation->world(), 0, true);
    }
    if (id == DebugWrite) {
        if (!debugPaused)
            throw std::runtime_error("Pause the simulation before changing values");
        int row = selection(DebugData), value = readInt(controls[DebugValue]);
        if (row < 0 || row >= 192 || (row < 128 && value != 0 && value != 1))
            throw std::runtime_error("Invalid switch/variable value");
        auto& w = simulation->world();
        if (w.state == TimeState::Fast || w.state == TimeState::Trace)
            throw std::runtime_error("Finish rewind first");
        if (row < 128)
            w.events.switches[row] = value != 0;
        else
            w.events.variables[row - 128] = value;
        simulation->commitRuntimeEdit();
    }
    refreshDebugger();
}
} // namespace echo
