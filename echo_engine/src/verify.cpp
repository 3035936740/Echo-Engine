#include "echo/app.hpp"
#include "echo/audio_state.hpp"
#include "echo/collision.hpp"
#include <GL/gl.h>
#include <commctrl.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace echo {
void App::captureUi(const std::filesystem::path& path, HWND target) {
    if (!target)
        target = window;
    RECT rect;
    GetClientRect(target, &rect);
    int width = rect.right, height = rect.bottom;
    HDC source = GetDC(target), dc = CreateCompatibleDC(source);
    HBITMAP bitmap = CreateCompatibleBitmap(source, width, height);
    auto old = SelectObject(dc, bitmap);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    printUi(dc, target);
    if (target == window) {
        auto canvasPath = path;
        canvasPath.replace_extension(".ppm");
        renderer.captureDisplay(canvasPath);
        std::ifstream f(canvasPath, std::ios::binary);
        std::string magic;
        int w, h, max;
        f >> magic >> w >> h >> max;
        f.get();
        std::vector<unsigned char> rgb(w * h * 3);
        f.read(reinterpret_cast<char*>(rgb.data()), rgb.size());
        std::vector<unsigned char> bgra(w * h * 4);
        for (int i = 0; i < w * h; ++i) {
            bgra[i * 4] = rgb[i * 3 + 2];
            bgra[i * 4 + 1] = rgb[i * 3 + 1];
            bgra[i * 4 + 2] = rgb[i * 3];
            bgra[i * 4 + 3] = 255;
        }
        BITMAPINFO image{};
        image.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        image.bmiHeader.biWidth = w;
        image.bmiHeader.biHeight = -h;
        image.bmiHeader.biPlanes = 1;
        image.bmiHeader.biBitCount = 32;
        image.bmiHeader.biCompression = BI_RGB;
        RECT canvasRect;
        GetWindowRect(canvas, &canvasRect);
        POINT origin{canvasRect.left, canvasRect.top};
        ScreenToClient(window, &origin);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, origin.x + renderer.offsetX, origin.y + renderer.offsetY, w, h, 0, 0, w, h,
                      bgra.data(), &image, DIB_RGB_COLORS, SRCCOPY);
        if (dialogWindow && (GetWindowLongPtrW(dialogWindow, GWL_STYLE) & WS_VISIBLE)) {
            RECT overlay;
            GetWindowRect(dialogWindow, &overlay);
            POINT p{overlay.left, overlay.top};
            ScreenToClient(window, &p);
            int saved = SaveDC(dc);
            SetViewportOrgEx(dc, p.x, p.y, nullptr);
            IntersectClipRect(dc, 0, 0, overlay.right - overlay.left, overlay.bottom - overlay.top);
            paintDialogue(dialogWindow, dc);
            RestoreDC(dc, saved);
        }
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned char> pixels(width * height * 4);
    SelectObject(dc, old);
    GetDIBits(dc, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + pixels.size();
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<char*>(&header), sizeof(header));
    out.write(reinterpret_cast<char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(reinterpret_cast<char*>(pixels.data()), pixels.size());
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(target, source);
}
namespace {
void verifyProgression(App& a, const std::filesystem::path& output) {
    auto check = [](bool v, const char* message) {
        if (!v)
            throw std::runtime_error(message);
    };
    a.showDatabaseEditor();
    auto saved = std::make_unique<Database>(a.database);
    auto initial = std::make_unique<WorldState>(*a.initial_);
    auto local = a.editor.events, common = a.editor.common;
    auto category = [&](int c) {
        SendMessageW(a.controls[DataCategory], CB_SETCURSEL, c, 0);
        a.databaseCommand(DataCategory);
    };
    category(2);
    SendMessageW(a.controls[DataPreset], CB_SETCURSEL, 1, 0);
    a.databaseCommand(DataNew);
    auto itemId = a.database.items[a.dataSlot].id;
    check(a.database.items[a.dataSlot].limit == 1, "Native unique key preset");
    SetWindowTextW(a.controls[DataName], L"回声钥匙");
    a.setNumber(DataInitial, 0);
    a.setNumber(DataLimit, 1);
    a.databaseCommand(DataApply);
    check(a.database.items[a.dataSlot].limit == 1, "Native item limit/name");
    auto before = saveDatabase(a.database);
    a.setNumber(DataLimit, 0);
    bool invalid = false;
    try {
        a.databaseCommand(DataApply);
    } catch (...) {
        invalid = true;
    }
    check(invalid && before == saveDatabase(a.database),
          "Invalid database leaves definitions unchanged");
    a.databaseForm();
    a.databaseCommand(DataUndo);
    a.databaseCommand(DataRedo);
    check(a.database.items[a.dataSlot].id == itemId && a.database.items[a.dataSlot].limit == 1,
          "Database undo/redo retains stable identity");
    a.captureUi(output / "inventory-light.bmp", a.databaseEditor);
    a.setTheme(true);
    a.captureUi(output / "inventory-dark.bmp", a.databaseEditor);
    a.setTheme(false);
    category(3);
    SendMessageW(a.controls[DataPreset], CB_SETCURSEL, 1, 0);
    a.databaseCommand(DataNew);
    auto questId = a.database.quests[a.dataSlot].id;
    check(a.database.quests[a.dataSlot].target == 3, "Native collection quest preset");
    SetWindowTextW(a.controls[DataName], L"寻找三枚回声碎片");
    a.setNumber(DataLimit, 3);
    SendMessageW(a.controls[DataStatus], CB_SETCURSEL, 1, 0);
    SendMessageW(a.controls[DataAuto], WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    SendMessageW(a.controls[DataAuto], WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
    a.databaseCommand(DataApply);
    check(a.database.quests[a.dataSlot].target == 3 && !a.database.quests[a.dataSlot].autoComplete &&
              findQuest(a.simulation->world(), questId)->status == QuestStatus::Active,
          "Native quest target/status and actual auto checkbox mouse input");
    a.captureUi(output / "quests-light.bmp", a.databaseEditor);
    a.setTheme(true);
    a.captureUi(output / "quests-dark.bmp", a.databaseEditor);
    a.setTheme(false);
    // Exercise all six form types, then save a real condition block using the selected ID.
    a.showEventEditor();
    SendMessageW(a.controls[EventScope], CB_SETCURSEL, 0, 0);
    a.eventCommand(EventScope);
    int fixtureEventId = a.eventId;
    a.eventCommon = false;
    a.eventPage = 0;
    a.eventDraft = {};
    a.eventDraft.id = fixtureEventId;
    a.eventDraft.name.set("Inventory condition");
    a.eventDraft.pages.emplace_back();
    SetWindowTextW(a.controls[EventName], L"物品条件测试");
    a.eventPageForm();
    auto selectOp = [&](EventOp op) {
        int count = int(SendMessageW(a.controls[EventCommandType], CB_GETCOUNT, 0, 0));
        for (int i = 0; i < count; ++i)
            if (SendMessageW(a.controls[EventCommandType], CB_GETITEMDATA, i, 0) == int(op))
                SendMessageW(a.controls[EventCommandType], CB_SETCURSEL, i, 0);
        a.eventCommand(EventCommandType);
        auto id = eventQuest(op) ? questId : itemId;
        int n = int(SendMessageW(a.controls[EventTarget], CB_GETCOUNT, 0, 0));
        for (int i = 0; i < n; ++i)
            if (SendMessageW(a.controls[EventTarget], CB_GETITEMDATA, i, 0) == id)
                SendMessageW(a.controls[EventTarget], CB_SETCURSEL, i, 0);
        a.setNumber(EventValue, 1);
        a.setNumber(EventX, 0);
    };
    for (auto op : {EventOp::ChangeItem, EventOp::IfItem, EventOp::QuestStatus, EventOp::QuestProgress,
                    EventOp::IfQuestStatus, EventOp::IfQuestProgress}) {
        selectOp(op);
        auto command = a.eventReadCommand();
        check(command.op == op && command.reference == (eventQuest(op) ? questId : itemId),
              "Native item/quest name selectors retain ID");
    }
    selectOp(EventOp::ChangeItem);
    SendMessageW(a.controls[EventFlag], CB_SETCURSEL, 2, 0);
    check(a.eventReadCommand().c == 2, "Native result uses named variable selector");
    a.eventCommand(EventInsert);
    selectOp(EventOp::IfItem);
    a.eventCommand(EventInsert);
    a.eventCommand(EventApply);
    check(findEvent(a.editor.events, fixtureEventId) &&
              findEvent(a.editor.events, fixtureEventId)->pages[0].commands.size() == 4,
          "Native conditional block insertion");
    a.captureUi(output / "inventory-event-light.bmp", a.eventEditor);
    category(2);
    for (std::size_t i = 0; i < a.database.items.size(); ++i)
        if (a.database.items[i].id == itemId)
            a.dataSlot = int(i);
    a.databaseForm();
    before = saveDatabase(a.database);
    bool referenced = false;
    try {
        a.databaseCommand(DataDelete);
    } catch (const std::exception& e) {
        referenced = std::string(e.what()).find("uses deleted") != std::string::npos;
    }
    check(referenced && saveDatabase(a.database) == before,
          "Database deletion checks formal event references");
    a.save();
    auto reloaded = loadDatabase(a.resources.text("database.echo"));
    check(progressionReferenceValid(reloaded, false, itemId) &&
              progressionReferenceValid(reloaded, true, questId),
          "Native database saves through real project transaction");
    SendMessageW(a.eventEditor, WM_CLOSE, 0, 0);
    a.editor.events = std::move(local);
    a.editor.common = std::move(common);
    a.database = *saved;
    *a.initial_ = *initial;
    a.databaseUndo.clear();
    a.databaseRedo.clear();
    SendMessageW(a.databaseEditor, WM_CLOSE, 0, 0);
    a.simulation->reset(*a.initial_, HistoryClearReason::DatabaseDefinitions);
    a.save();
    std::ofstream(output / "verification.txt", std::ios::app)
        << "PASS inventory/quest database / mouse / limits / stable-ID undo-redo / six command forms / "
           "condition block / delete reference guard / transaction save / light-dark\n";
}

void verifyEventEditing(App& a, const std::filesystem::path& output) {
    auto check = [](bool v, const char* text) {
        if (!v)
            throw std::runtime_error(text);
    };
    auto local = a.editor.events, common = a.editor.common;
    a.showEventEditor();
    auto world = std::make_unique<WorldState>(a.simulation->world());
    auto initial = std::make_unique<WorldState>(*a.initial_);
    auto history = a.simulation->history().diagnostics(a.simulation->world());
    int historyCursor = a.simulation->world().timelineCursor;
    auto op = [](EventOp type, float value = 0) {
        EventCommand c;
        c.op = type;
        c.value = value;
        return c;
    };
    a.eventDraft.pages.assign(1, EventPage{});
    a.eventPage = 0;
    auto branch = op(EventOp::IfSwitch);
    branch.b = 1;
    auto text = op(EventOp::Text);
    text.text.set("选择正确分支");
    a.eventDraft.pages[0].commands = {
        branch,           text, op(EventOp::Else), text, op(EventOp::EndIf), op(EventOp::Wait, .2f),
        op(EventOp::Exit)};
    a.eventPageForm();
    check((GetWindowLongPtrW(a.controls[EventCommandList], GWL_STYLE) & LBS_EXTENDEDSEL) != 0,
          "Native event list is genuinely multiselect");
    SendMessageW(a.controls[EventCatalogCategory], CB_SETCURSEL, int(EventCategory::Advanced), 0);
    a.command(EventCatalogCategory, CBN_SELCHANGE);
    SetWindowTextW(a.controls[EventCatalogSearch], L"LuA 条件");
    check(SendMessageW(a.controls[EventCommandType], CB_GETCOUNT, 0, 0) == 1 &&
              SendMessageW(a.controls[EventCommandType], CB_GETITEMDATA, 0, 0) == int(EventOp::IfScript),
          "Native categorized Unicode/English live search");
    SetWindowTextW(a.controls[EventCatalogSearch], L"no_such_command");
    check(SendMessageW(a.controls[EventCommandType], CB_GETCOUNT, 0, 0) == 0 &&
              !IsWindowEnabled(a.controls[EventInsert]),
          "Native empty search cannot insert stale opcode");
    a.eventSetSelection({5});
    check(a.eventSelectedSource() == 5 &&
              SendMessageW(a.controls[EventCommandType], CB_GETITEMDATA, a.selection(EventCommandType),
                           0) == int(EventOp::Wait) &&
              a.selection(EventCatalogCategory) == 0,
          "Existing command reveals its own type through filtered catalog");
    a.eventSetSelection({1, 5});
    check(SendMessageW(a.controls[EventCommandList], LB_GETSELCOUNT, 0, 0) == 2 &&
              !IsWindowEnabled(a.controls[EventUpdate]),
          "Native multi-selection preserves indices and disables ambiguous parameter edits");
    a.eventCommand(EventCopy);
    check(a.eventClipboard.size() == 2 && a.eventClipboard[0].op == EventOp::Text &&
              a.eventClipboard[1].op == EventOp::Wait,
          "Native disjoint copy follows source order");
    a.eventSetSelection({0});
    SendMessageW(a.eventEditor, WM_COMMAND, MAKEWPARAM(EventCommandList, LBN_DBLCLK),
                 reinterpret_cast<LPARAM>(a.controls[EventCommandList]));
    check(a.eventRows.size() == 3 && a.eventRows[1].source == 5 && a.eventRows[0].hidden == 4,
          "Native double-click fold preserves source PC");
    auto folded = a.eventDraft.pages[0].commands.size();
    ShowWindow(a.eventEditor, SW_SHOWNOACTIVATE);
    SetFocus(a.controls[EventCommandList]);
    check(a.eventShortcut('C', true) && a.eventClipboard.size() == 5,
          "Focused copy of folded block includes invisible body");
    a.captureUi(output / "event-editing-light.bmp", a.eventEditor);
    a.setTheme(true);
    a.captureUi(output / "event-editing-dark.bmp", a.eventEditor);
    a.setTheme(false);
    a.eventSetSelection({5});
    a.eventCommand(EventRemove);
    check(a.eventDraft.pages[0].commands.size() == folded - 1 &&
              a.eventDraft.pages[0].commands.back().op == EventOp::Exit,
          "Deleting after folded block addresses actual command");
    a.eventCommand(EventDraftUndo);
    check(a.eventDraft.pages[0].commands.size() == folded, "Draft undo restores hidden commands");
    a.eventCommand(EventDraftRedo);
    check(a.eventDraft.pages[0].commands.size() == folded - 1, "Draft redo restores deletion");
    a.eventCommand(EventPageNew);
    check(a.eventDraft.pages.size() == 2 && a.eventClipboard.size() == 5,
          "Clipboard survives page changes");
    a.eventCommand(EventPaste);
    check(a.eventDraft.pages[1].commands.size() == 5 &&
              a.eventDraft.pages[1].commands[2].op == EventOp::Else,
          "Cross-page paste preserves complete condition");
    a.eventCommand(EventDraftUndo);
    check(a.eventDraft.pages[1].commands.empty(), "Paste is one draft undo");
    a.eventCommand(EventDraftRedo);
    check(a.eventDraft.pages[1].commands.size() == 5, "Paste redo preserves whole branch");
    auto before = saveEvents(local);
    auto count = a.editor.undoCount();
    a.eventCommand(EventApply);
    auto applied = saveEvents(a.editor.events);
    check(a.editor.undoCount() == count + 1,
          "Draft commands commit one shared authoring undo transaction");
    a.command(Undo);
    check(saveEvents(a.editor.events) == before, "Shared authoring undo restores original definitions");
    a.command(Redo);
    check(saveEvents(a.editor.events) == applied, "Shared redo retains pasted page");
    a.eventDraft.pages.assign(1, EventPage{});
    a.eventPage = 0;
    a.eventDraft.pages[0].commands.assign(32, op(EventOp::Wait, .1f));
    a.eventPageForm();
    bool full = false;
    try {
        a.eventCommand(EventPaste);
    } catch (...) {
        full = true;
    }
    check(full && a.eventDraft.pages[0].commands.size() == 32 && a.eventClipboard.size() == 5 &&
              a.eventDraftUndo.empty(),
          "Rejected full-page paste preserves draft/clipboard/history");
    a.eventDraft.pages[0].commands = {op(EventOp::Wait, .1f), op(EventOp::Exit)};
    a.eventPageForm();
    a.eventClipboard = {op(EventOp::Script)};
    a.eventClipboard[0].text.set("local =");
    bool syntax = false;
    try {
        a.eventCommand(EventPaste);
    } catch (const std::exception& e) {
        syntax = std::string(e.what()).find("command") != std::string::npos;
    }
    check(syntax && a.eventDraft.pages[0].commands.size() == 2 && a.eventDraftUndo.empty(),
          "Invalid advanced clipboard Lua fails before draft changes");
    for (int i = 0; i < 24; ++i) {
        a.eventSetSelection({0});
        a.setNumber(EventValue, .1f + i * .01f);
        a.eventCommand(EventUpdate);
    }
    check(a.eventDraftUndo.size() == 16, "Draft history has explicit step bound");
    std::size_t budget = 0;
    for (const auto& e : a.eventDraftUndo)
        budget += eventDraftBytes(e);
    check(budget < 8ull * 1024 * 1024, "Draft deep history within byte budget");
    a.eventCatalog(int(EventOp::Script), true);
    SetFocus(a.controls[EventContent]);
    check(!a.eventShortcut('C', true), "Text editing keeps normal Ctrl+C");
    check(std::memcmp(world.get(), &a.simulation->world(), sizeof(WorldState)) == 0 &&
              std::memcmp(initial.get(), a.initial_.get(), sizeof(WorldState)) == 0,
          "All event authoring leaves gameplay snapshots unchanged");
    auto after = a.simulation->history().diagnostics(a.simulation->world());
    check(history.frames == after.frames && historyCursor == a.simulation->world().timelineCursor,
          "Folds/search/drafts leave actual history untouched");
    SendMessageW(a.eventEditor, WM_CLOSE, 0, 0);
    a.editor.events = std::move(local);
    a.editor.common = std::move(common);
    a.eventClipboard.clear();
    a.eventDraftUndo.clear();
    a.eventDraftRedo.clear();
    a.save();
    std::ofstream(output / "verification.txt", std::ios::app)
        << "PASS categorized live search / real multi-selection / folded source PC / keyboard focus / "
           "cross-page clipboard / bounded draft undo-redo / common shared undo / invalid Lua-capacity "
           "rollback / unchanged gameplay / light-dark\n";
}

} // namespace
void App::verify(const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    auto check = [](bool value, const char* text) {
        if (!value)
            throw std::runtime_error(text);
    };
    command(Edit);
    auto old = editor.level.at(12, 12);
    command(1);
    editor.paint(384, 384);
    editor.end();
    check(editor.level.at(12, 12) == Tile::Solid, "Editor paint");
    SendMessageW(window, WM_COMMAND, Undo, 0);
    check(editor.level.at(12, 12) == old, "Native undo button");
    SendMessageW(window, WM_COMMAND, Redo, 0);
    check(editor.level.at(12, 12) == Tile::Solid, "Native redo button");
    command(8);
    editor.paint(384, 384);
    editor.paint(416, 384);
    editor.end();
    check(editor.level.at(12, 12) == Tile::Empty && editor.level.at(13, 12) == Tile::Solid,
          "Editor drag");
    SendMessageW(controls[Environment], CB_SETCURSEL, 2, 0);
    SendMessageW(side, WM_COMMAND, MAKEWPARAM(Environment, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(controls[Environment]));
    check(renderer.environment == 2, "Sidebar combo command routed");
    setNumber(WindowSeconds, 12);
    SendMessageW(controls[RecordHz], CB_SETCURSEL, 0, 0);
    command(Apply);
    check(initial_->rewind.history_seconds == 12 && initial_->rewind.record_hz == 30, "Native settings");
    SendMessageW(controls[Snap], CB_SETCURSEL, 1, 0);
    command(Snap, CBN_SELCHANGE);
    save();
    check(resources.config().cameraSnap == "smooth", "camera mode persisted");
    SendMessageW(controls[Snap], CB_SETCURSEL, 0, 0);
    command(Snap, CBN_SELCHANGE);
    save();
    check(loadLevel(resources.text(config.level)).at(13, 12) == Tile::Solid, "Editor save roundtrip");
    command(Play);
    check(!editing && simulation->world().level.at(13, 12) == Tile::Solid,
          "Instant play uses authored level");
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    Input right;
    right.move = 1;
    for (int i = 0; i < 780; ++i)
        simulation->tick(right);
    Input trace;
    trace.trace = true;
    for (int i = 0; i < 780; ++i)
        simulation->tick(trace);
    simulation->tick({});
    simulation->tick(right);
    check(simulation->world().state == TimeState::Free, "Editor project farthest rewind then play");
    command(Edit);
    camera.update(*initial_, 0, true);
    renderer.fluid = false;
    renderer.raymarch = false;
    SendMessageW(controls[ThemeMode], CB_SETCURSEL, 0, 0);
    command(ThemeMode, CBN_SELCHANGE);
    render();
    captureUi(output / "editor-light.bmp");
    SendMessageW(controls[ThemeMode], CB_SETCURSEL, 1, 0);
    command(ThemeMode, CBN_SELCHANGE);
    render();
    captureUi(output / "editor-dark.bmp");
    check(darkTheme, "native theme selection");
    SendMessageW(controls[ThemeMode], CB_SETCURSEL, 0, 0);
    command(ThemeMode, CBN_SELCHANGE);
    RECT original;
    GetWindowRect(window, &original);
    SetWindowPos(window, nullptr, 0, 0, 1280, 760, SWP_NOMOVE | SWP_NOZORDER);
    layout();
    RECT client;
    GetClientRect(window, &client);
    check(workspace && mapTree && tileAtlas, "persistent workspace and atlas");
    check((GetWindowLongPtrW(side, GWL_STYLE) & WS_VISIBLE) == 0, "advanced inspector initially hidden");
    captureUi(output / "scene-settings.bmp", side);
    SendMessageW(side, WM_CLOSE, 0, 0);
    check(IsWindow(side), "closing settings retains reusable controls");
    check(GetParent(controls[Layer]) == workspace && GetParent(controls[1]) == window,
          "common tools and layer controls have dedicated locations");
    for (int id : std::vector<int>{ThemeMode, 1, 17, WorkspaceObjects, WorkspaceBarrage, WorkspacePencil,
                                   WorkspacePick, WorkspaceInspector, Edit, Play}) {
        RECT r{};
        GetWindowRect(controls[id], &r);
        POINT p{r.right, r.bottom};
        ScreenToClient(window, &p);
        check(p.x <= client.right && p.y < canvasTop, "compact single toolbar fits window");
    }
    RECT dock{}, view{};
    GetWindowRect(workspace, &dock);
    GetWindowRect(canvas, &view);
    check(dock.right <= view.left && view.right <= original.right,
          "left dock and central canvas do not overlap");
    command(WorkspaceRectangle);
    check(editor.tool == 18, "toolbar rectangle tool");
    command(WorkspaceTileFirst + 5);
    check(editor.tileIndex == 6 && editor.tool == 18, "dock tile preserves rectangle mode");
    auto previousTile = editor.level.layers[editor.layer].cells[10 * mapWidth + 14];
    auto toCanvas = [&](float x, float y) {
        int px =
            renderer.offsetX + int(std::round((x - camera.renderX) * pixelsPerUnit * renderer.scale));
        int py =
            renderer.offsetY + int(std::round((y - camera.renderY) * pixelsPerUnit * renderer.scale));
        return MAKELPARAM(px, py);
    };
    SendMessageW(canvas, WM_LBUTTONDOWN, MK_LBUTTON, toCanvas(14 * tileSize + 4, 10 * tileSize + 4));
    SendMessageW(canvas, WM_MOUSEMOVE, MK_LBUTTON, toCanvas(15 * tileSize + 4, 11 * tileSize + 4));
    SendMessageW(canvas, WM_LBUTTONUP, 0, toCanvas(15 * tileSize + 4, 11 * tileSize + 4));
    check(editor.level.layers[editor.layer].cells[10 * mapWidth + 14] == 6 &&
              editor.level.layers[editor.layer].cells[11 * mapWidth + 15] == 6,
          "compact viewport mouse rectangle maps to correct world tiles");
    command(Undo);
    check(editor.level.layers[editor.layer].cells[10 * mapWidth + 14] == previousTile,
          "mouse rectangle is one undo entry");

    command(WorkspaceBucket);
    check(editor.tool == 19, "toolbar bucket");
    command(WorkspaceSelection);
    check(editor.tool == 20, "toolbar selection");
    command(WorkspacePick);
    check(editor.tool == 21, "toolbar eyedropper");
    command(WorkspacePencil);
    check(editor.tool == int(editor.terrain), "toolbar pencil");
    command(WorkspaceObjects);
    check(editor.tool == 22 &&
              SendMessageW(workspaceItems, LB_GETCOUNT, 0, 0) == LRESULT(objectLibrary.templates.size()),
          "object templates in dock");
    if (!objectLibrary.templates.empty()) {
        SendMessageW(workspaceItems, LB_SETCURSEL, 0, 0);
        SendMessageW(workspace, WM_COMMAND, MAKEWPARAM(WorkspaceItems, LBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(workspaceItems));
        check(editor.objectTemplate == objectLibrary.templates.front().id,
              "dock selects object template");
    }
    command(1);
    auto beforeWorkspace = std::make_unique<WorldState>(simulation->world());
    bool wasDirty = editor.dirty;
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(workspaceWidth + 2, 150));
    SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(332, 150));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(332, 150));
    check(workspaceWidth == 332 && !splittingWorkspace, "workspace splitter drag");
    command(WorkspaceDarkTheme);
    workspaceWidth = 272;
    editorGrid = false;
    loadWorkspaceSettings();
    check(workspaceWidth == 332 && darkTheme && editorGrid, "workspace preferences restore");
    check(editor.dirty == wasDirty &&
              std::memcmp(beforeWorkspace.get(), &simulation->world(), sizeof(WorldState)) == 0,
          "workspace preferences do not change gameplay or dirty state");
    writeTextAtomically(resources.root / ".echo/workspace.echo", "ECHO_WORKSPACE_999 332 1 1 1");
    loadWorkspaceSettings();
    check(workspaceWidth == 272 && !darkTheme && editorGrid, "invalid workspace version defaults");
    command(WorkspaceReset);
    check(debugMuteMusic, "Debug BGM muted by default");
    updateAudioOutputPolicy();
    check(audio.musicMuted(), "Debug mute reaches audio output");
    command(DebugMuteMusic);
    check(!debugMuteMusic && audio.musicMuted(),
          "Automated verification stays quiet even when preference is off");
    bool savedVerify = verifyMode, savedSmoke = smoke, savedPublished = resources.published;
    verifyMode = smoke = false;
    updateAudioOutputPolicy();
    check(!audio.musicMuted(), "Interactive debug preference permits explicit unmute");
    debugMuteMusic = true;
    resources.published = true;
    updateAudioOutputPolicy();
    check(!audio.musicMuted(), "Ordinary published game keeps its music");
    smoke = true;
    updateAudioOutputPolicy();
    check(audio.musicMuted(), "Published smoke test mutes BGM");
    verifyMode = savedVerify;
    smoke = savedSmoke;
    resources.published = savedPublished;
    debugMuteMusic = false;
    updateAudioOutputPolicy();
    command(DebugMuteMusic);
    debugMuteMusic = false;
    loadWorkspaceSettings();
    check(debugMuteMusic && audio.musicMuted(), "Debug BGM mute preference restores");
    auto gameplayMusic = std::make_unique<WorldState>(simulation->world());
    bool projectDirty = editor.dirty;
    command(DebugMuteMusic);
    command(DebugMuteMusic);
    check(editor.dirty == projectDirty &&
              std::memcmp(gameplayMusic.get(), &simulation->world(), sizeof(WorldState)) == 0,
          "Output mute never changes gameplay snapshots or project dirty state");
    writeTextAtomically(resources.root / ".echo/workspace.echo", "ECHO_WORKSPACE_1 300 0 1 1");
    loadWorkspaceSettings();
    check(workspaceWidth == 300 && debugMuteMusic, "Legacy workspace migrates to default BGM mute");
    command(WorkspaceReset);
    command(WorkspaceGrid);
    check(!editorGrid, "workspace grid off");
    command(WorkspaceGrid);
    check(editorGrid, "workspace grid on");
    command(WorkspaceDarkTheme);
    check(darkTheme, "menu theme action");
    command(WorkspaceLightTheme);
    check(!darkTheme, "menu light theme action");
    render();
    captureUi(output / "editor-compact.bmp");
    SetWindowPos(window, nullptr, 0, 0, original.right - original.left, original.bottom - original.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    layout();
    showAudioEditor();
    check(audioEditor && IsWindow(audioEditor), "audio settings window");
    SendMessageW(window, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(audioEditor));
    check(focused, "owned audio settings retain app focus");
    SendMessageW(audioEditor, WM_ACTIVATE, WA_INACTIVE, 0);
    check(!focused, "switching away from audio settings pauses app");
    focused = true;
    command(AudioApply);
    command(MusicPreview);
    check(previewAudio && audioPreview->audio.musicLength > 0, "BGM preview decoded");
    command(MusicFadeOut);
    stepAudioState(*audioPreview, .5f);
    check(audioPreview->audio.fadeElapsed > 0, "BGM fade button");
    command(SoundPreview);
    check(audioPreview->audio.voices.count == 1, "SE preview button");
    SendMessageW(audioEditor, WM_CLOSE, 0, 0);
    check(!previewAudio, "closing audio ends preview");
    showMapEditor();
    check(mapEditor && IsWindow(mapEditor), "map settings window");
    if (maps.size() > 1) {
        auto root = TreeView_GetRoot(mapTree);
        auto first = TreeView_GetChild(mapTree, root);
        auto second = TreeView_GetNextSibling(mapTree, first);
        check(second != nullptr, "map tree has second map");
        TreeView_SelectItem(mapTree, second);
        check(editMap == 1 && initial_->activeMap == 1, "map tree switch");
        check(selection(MapSelect) == 1, "map dialog follows tree selection");
        SendMessageW(controls[MapSelect], CB_SETCURSEL, 0, 0);
        command(MapSelect, CBN_SELCHANGE);
    }
    SendMessageW(mapEditor, WM_CLOSE, 0, 0);
    command(2);
    showTilePalette();
    check(tileAtlas && tilePalette, "tile thumbnail palette loaded");
    command(405);
    check(editor.tileIndex == 6 && editor.tool == 2,
          "tile thumbnail chooses brush and keeps collision mode");
    captureUi(output / "tile-palette.bmp", tilePalette);
    SendMessageW(tilePalette, WM_CLOSE, 0, 0);
    check(tileAtlas != nullptr, "closing floating palette preserves dock atlas");
    command(17);
    editor.paint(352, 592);
    editor.end();
    check(editor.selectedEvent >= 0, "native map event placement");
    refreshWorkspace();
    check(SendMessageW(workspaceItems, LB_GETCOUNT, 0, 0) == editor.level.events.count,
          "dock lists deployed map events");
    render();
    captureUi(output / "workspace-events.bmp");
    showEventEditor();
    check(eventEditor && IsWindow(eventEditor), "event command window");
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::IfSwitch), 0);
    eventCommand(EventCommandType, CBN_SELCHANGE);
    eventCommand(EventInsert);
    check(eventDraft.pages[0].commands.size() == 4 &&
              eventDraft.pages[0].commands[2].op == EventOp::Else &&
              eventDraft.pages[0].commands[3].op == EventOp::EndIf,
          "condition insertion builds a complete block");
    eventCommand(EventRemove);
    check(eventDraft.pages[0].commands.size() == 1, "condition removal deletes its whole block");
    auto beforeLoopPreset = eventDraft;
    SendMessageW(controls[EventPreset], CB_SETCURSEL, 7, 0);
    eventCommand(EventPresetApply);
    check(eventDraft.pages[0].commands.size() == 4 &&
              eventDraft.pages[0].commands[0].op == EventOp::Loop &&
              eventDraft.pages[0].commands[0].a == 3,
          "Native timed-loop preset");
    eventDraft = beforeLoopPreset;
    eventPageForm();
    // Exercise structural editing through the same native controls users operate.
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::Loop), 0);
    eventCommandForm();
    SetWindowTextW(controls[EventValue], L"3");
    eventCommand(EventInsert);
    check(eventDraft.pages[0].commands.size() == 3 &&
              eventDraft.pages[0].commands[1].op == EventOp::Loop &&
              eventDraft.pages[0].commands[1].a == 3 &&
              eventDraft.pages[0].commands[2].op == EventOp::EndLoop,
          "Native loop insert/count/pair");
    SetWindowTextW(controls[EventValue], L"1.5");
    bool invalidLoopCount = false;
    try {
        eventCommand(EventUpdate);
    } catch (...) {
        invalidLoopCount = true;
    }
    check(invalidLoopCount && eventDraft.pages[0].commands[1].a == 3,
          "Native fractional loop count mutated draft");
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::Wait), 0);
    eventCommandForm();
    SetWindowTextW(controls[EventValue], L".1");
    eventCommand(EventInsert);
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::BreakLoop), 0);
    eventCommandForm();
    eventCommand(EventInsert);
    eventSetSelection({1});
    eventCommand(EventCommandList, LBN_SELCHANGE);
    auto loopBefore = saveEvents(editor.events);
    eventCommand(EventApply);
    auto loopDefinitions = saveEvents(editor.events);
    command(Undo);
    check(saveEvents(editor.events) == loopBefore, "Native loop definition undo");
    command(Redo);
    check(saveEvents(editor.events) == loopDefinitions, "Native loop definition redo");
    eventSetSelection({1});
    eventCommand(EventCommandList, LBN_SELCHANGE);
    captureUi(output / "event-loop-light.bmp", eventEditor);
    setTheme(true);
    captureUi(output / "event-loop-dark.bmp", eventEditor);
    setTheme(false);
    eventCommand(EventUp);
    check(eventDraft.pages[0].commands[0].op == EventOp::Loop &&
              eventDraft.pages[0].commands[3].op == EventOp::EndLoop,
          "Native loop move carries entire block");
    eventCommand(EventDown);
    check(eventDraft.pages[0].commands[1].op == EventOp::Loop,
          "Native loop move down carries entire block");
    eventCommand(EventRemove);
    check(eventDraft.pages[0].commands.size() == 1 &&
              eventDraft.pages[0].commands[0].op == EventOp::Text,
          "Native loop delete carries body and end");
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::BreakLoop), 0);
    eventCommandForm();
    bool orphanBreak = false;
    try {
        eventCommand(EventInsert);
    } catch (...) {
        orphanBreak = true;
    }
    check(orphanBreak && eventDraft.pages[0].commands.size() == 1, "Native orphan break mutated draft");
    SendMessageW(controls[EventPreset], CB_SETCURSEL, 1, 0);
    eventCommand(EventPresetApply);
    SetWindowTextW(controls[EventName], L"一次性宝箱");
    eventCommand(EventApply);
    check(findEvent(editor.events, eventId)->pages.size() == 2, "native event conditional pages");
    if (editor.level.events.count > 1) {
        int selected = editor.selectedEvent;
        editor.selectedEvent = 0;
        showEventEditor(true);
        check(eventId == editor.level.events[0].id, "double click switches the existing event window");
        editor.selectedEvent = selected;
        showEventEditor(true);
    }
    captureUi(output / "event-map.bmp", eventEditor);
    SendMessageW(controls[EventScope], CB_SETCURSEL, 1, 0);
    eventCommand(EventScope, CBN_SELCHANGE);
    check(eventCommon && !editor.common.events.empty(), "native global common event library");
    auto beforeCommonLoopPreset = eventDraft;
    SendMessageW(controls[EventPreset], CB_SETCURSEL, 4, 0);
    eventCommand(EventPresetApply);
    check(eventDraft.pages[0].commands[0].op == EventOp::Loop && eventDraft.pages[0].commands[0].a == 3,
          "Native common loop preset stable ID mapping");
    eventDraft = beforeCommonLoopPreset;
    eventPageForm();

    eventSetSelection({0});
    eventCommand(EventCommandList, LBN_SELCHANGE);
    setNumber(EventValue, .05f);
    eventCommand(EventUpdate);
    SetWindowTextW(controls[EventName], L"全局并行计时");
    eventCommand(EventApply);
    captureUi(output / "event-global.bmp", eventEditor);
    SendMessageW(eventEditor, WM_CLOSE, 0, 0);
    save();
    check(resources.contains("events/common.echo") &&
              !loadEvents(resources.text("events/common.echo"), true).events.empty(),
          "native event save");
    command(Play);
    simulation->world().events.switches[0] = 1;
    auto countBefore = simulation->world().events.variables[0];
    for (int i = 0; i < 12; ++i)
        simulation->tick({});
    check(simulation->world().events.variables[0] > countBefore,
          "native authored global parallel executes");
    command(Edit);
    // Keep the existing dense benchmark scene isolated from newly authored event art.
    editor.level.events.clear();
    editor.events = {};
    editor.common = {};
    command(Play);
    auto& game = simulation->world();
    DialogueDefinition d;
    d.speaker.set("Kino");
    d.portrait.set("assets/assetKino.png");
    d.text.set("The past leaves an echo.\nHold T to retrace your steps.");
    game.level.dialogues.clear();
    game.level.dialogues.push(d);
    game.dialogue.active = 0;
    game.dialogue.revealed = 100;
    game.gameplayClockPaused = true;
    render();
    captureUi(output / "dialogue.bmp");
    check(!portraits.empty() && std::get<0>(portraits.begin()->second) != nullptr,
          "portrait decoded for dialogue");
    RECT canvasSize{};
    GetClientRect(canvas, &canvasSize);
    for (int frame = 0; frame < 8; ++frame) {
        render(0);
        glReadBuffer(GL_FRONT);
        unsigned char color[4]{};
        glReadPixels(renderer.offsetX + 12 * renderer.scale,
                     canvasSize.bottom - renderer.offsetY - 130 * renderer.scale, 1, 1, GL_RGB,
                     GL_UNSIGNED_BYTE, color);
        glReadBuffer(GL_BACK);
        check(color[0] == 43 && color[1] == 48 && color[2] == 64,
              "dialogue is composited into every presented frame");
    }
    check(!IsWindowVisible(dialogWindow), "no GDI dialogue child can flicker over OpenGL");
    auto oldKeys = config.keys;
    inputCommand(InputEditor);
    SendMessageW(controls[InputKeyFirst + 4], CB_SETCURSEL, 'K' - 8, 0);
    inputCommand(InputApply);
    check(config.keys.keys[4] == 'K', "native custom confirm binding");
    auto previousProjectText = resources.text("project.echo");
    writeTextAtomically(resources.root / "project.echo", projectText());
    check(resources.config().keys.keys[4] == 'K', "native keyboard save / project reload");
    writeTextAtomically(resources.root / "project.echo", previousProjectText);
    SendMessageW(controls[InputArrows], WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 12));
    SendMessageW(controls[InputArrows], WM_LBUTTONUP, 0, MAKELPARAM(12, 12));
    inputCommand(InputApply);
    check(!config.keys.arrowMovement, "native arrow movement toggle click");

    SendMessageW(controls[InputKeyFirst + 4], CB_SETCURSEL, 'R' - 8, 0);
    bool keyConflict = false;
    try {
        inputCommand(InputApply);
    } catch (...) {
        keyConflict = true;
    }
    check(keyConflict && config.keys.keys[4] == 'K', "key conflict preserves valid mapping");
    inputCommand(InputReset);
    inputCommand(InputApply);
    check(config.keys.keys[4] == 'E', "native restore default keys");
    captureUi(output / "keyboard-light.bmp", inputEditor);
    setTheme(true);
    captureUi(output / "keyboard-dark.bmp", inputEditor);
    setTheme(false);
    SendMessageW(inputEditor, WM_CLOSE, 0, 0);
    config.keys = oldKeys;
    auto priorRules = initial_->rewind;
    rewindRulesCommand(RewindRulesEditor);
    check(number(RewindMargin) == 2 &&
              SendMessageW(controls[RewindNearEnabled], BM_GETCHECK, 0, 0) == BST_CHECKED,
          "native default fast proximity +2 rule");
    setNumber(RewindMargin, 7);
    SendMessageW(controls[RewindNearEnabled], WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 12));
    SendMessageW(controls[RewindNearEnabled], WM_LBUTTONUP, 0, MAKELPARAM(12, 12));
    rewindRulesCommand(RewindRulesApply);
    check(!initial_->rewind.fast_stop_near_bullets && initial_->rewind.fast_bullet_margin == 7,
          "native fast rules switch and margin apply");
    setNumber(RewindMargin, 65);
    bool invalidMargin = false;
    try {
        rewindRulesCommand(RewindRulesApply);
    } catch (...) {
        invalidMargin = true;
    }
    check(invalidMargin && initial_->rewind.fast_bullet_margin == 7,
          "invalid fast margin preserves prior config");
    setNumber(RewindMargin, 7);
    auto serializedRules = projectText();
    check(serializedRules.find("rewind_fast_stop_near_bullets=0") != std::string::npos &&
              serializedRules.find("rewind_fast_bullet_margin=7.") != std::string::npos,
          "project manifest carries fast proximity configuration");
    captureUi(output / "fast-rewind-light.bmp", rewindRulesEditor);
    setTheme(true);
    captureUi(output / "fast-rewind-dark.bmp", rewindRulesEditor);
    setTheme(false);
    SendMessageW(rewindRulesEditor, WM_CLOSE, 0, 0);
    initial_->rewind = priorRules;
    simulation->configureRewind(priorRules);
    std::array<bool, 256> keys{};
    keys['E'] = true;
    auto confirm = keyboardInput(config.keys, keys, game.focus.key, game.hurt.showKey);
    simulation->tick(confirm);
    check(game.dialogue.page == 1 && game.state != TimeState::Fast && game.state != TimeState::Trace,
          "native E continues dialogue without rewind");
    command(Edit);
    bool shaderRejected = false;
    try {
        renderer.setBackgroundSource("not valid GLSL");
    } catch (...) {
        shaderRejected = true;
    }
    check(shaderRejected, "custom shader syntax error is reported");
    render();
    check(glGetError() == GL_NO_ERROR, "shader failure keeps prior renderer");

    // Every environment / weather grade and both optional shader layers are compiled
    // and rendered through the ACTUAL OpenGL driver, not a source-only shader check.
    visual->aster.enabled = false;
    double maxFrame = 0;
    for (int environment = 0; environment < 7; ++environment) {
        renderer.environment = environment;
        renderer.weather = 0;
        renderer.fluid = false;
        renderer.raymarch = false;
        visual->clock = 3.2;
        auto begin = std::chrono::steady_clock::now();
        renderer.draw(*visual, camera);
        maxFrame = std::max(
            maxFrame,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        renderer.capture(output / ("environment_" + std::to_string(environment) + ".ppm"));
        check(glGetError() == GL_NO_ERROR, "Environment GL error");
    }
    for (int weather = 1; weather <= 2; ++weather)
        for (int grade = 1; grade <= 5; ++grade) {
            renderer.environment = weather == 1 ? 3 : 1;
            renderer.weather = weather;
            renderer.weatherStrength = grade / 5.0f;
            visual->clock = 4;
            renderer.draw(*visual, camera);
            renderer.capture(
                output / ("weather_" + std::to_string(weather) + "_" + std::to_string(grade) + ".ppm"));
            check(glGetError() == GL_NO_ERROR, "Weather GL error");
        }
    renderer.weather = 0;
    renderer.fluid = true;
    renderer.raymarch = true;
    for (int i = 0; i < 60; ++i) {
        visual->clock += fixedDt;
        visual->kino.vx = 100;
        renderer.draw(*visual, camera);
    }
    renderer.capture(output / "fluid_sdf.ppm");
    check(glGetError() == GL_NO_ERROR, "Fluid/SDF GL error");
    command(Edit);
    editor.layer = 0;
    showLayerEditor();
    SetWindowTextW(controls[LayerImagePath], L"assets/tiles.png");
    setNumber(LayerImageW, 64);
    setNumber(LayerImageH, 64);
    setNumber(LayerParallax, .5f);
    command(LayerImageApply);
    check(!editor.level.layers[0].image.empty() && editor.level.layers[0].parallax == .5f,
          "image layer authoring");
    SendMessageW(layerEditor, WM_CLOSE, 0, 0);
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    simulation->world().level.dialogues.clear();
    renderer.fluid = false;
    renderer.raymarch = false;
    renderer.weather = 0;
    for (int i = 0; i < maxBullets; ++i) {
        Bullet bullet;
        bullet.x = 30 + (i % 64) * 18;
        bullet.y = 30 + (i / 64) * 15;
        bullet.vx = 1;
        bullet.expireAge = 100;
        spawnBullet(simulation->world(), bullet);
    }
    double totalMs = 0;
    std::array<double, 120> denseFrameTimes{};
    int denseDraws = 0;
    for (int frame = 0; frame < 120; ++frame) {
        auto begin = std::chrono::steady_clock::now();
        simulation->tick({});
        render();
        glFinish();
        denseFrameTimes[frame] =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        totalMs += denseFrameTimes[frame];
        denseDraws = renderer.drawCalls;
    }
    std::sort(denseFrameTimes.begin(), denseFrameTimes.end());
    renderer.capture(output / "bullets_2048.ppm");
    check(simulation->world().bullets.count == maxBullets, "GPU dense scene retains all bullets");
    check(denseDraws < 20 && glGetError() == GL_NO_ERROR,
          "2048 bullets batched into bounded draw calls");
    Input oldest;
    oldest.trace = true;
    for (int i = 0; i < 240; ++i) {
        simulation->tick(oldest);
        if (simulation->world().state != TimeState::Trace)
            break;
    }
    simulation->tick({});
    simulation->tick({});
    check(!simulation->world().level.layers[0].image.empty(), "layer image farthest rewind continues");
    command(Edit);
    if (editor.level.portals.count == 0) {
        MapPortal p;
        p.region = {500, 540, 40, 100};
        p.targetMap = 0;
        editor.level.portals.push(p);
    }
    editor.selectedPortal = 0;
    showTransitionEditor();
    SendMessageW(controls[TransitionPreset], CB_SETCURSEL, 3, 0);
    setNumber(TransitionClose, .25f);
    transitionCommand(TransitionApply);
    check(initial_->mapTransition.kind == TransitionKind::Diamond, "native transition preset");
    captureUi(output / "transition-settings.bmp", transitionEditor);
    SendMessageW(controls[TransitionScope], CB_SETCURSEL, 1, 0);
    transitionCommand(TransitionScope);
    SendMessageW(controls[TransitionPreset], CB_SETCURSEL, 2, 0);
    transitionCommand(TransitionApply);
    check(initial_->deathTransition.kind == TransitionKind::Iris, "native restart preset");
    SendMessageW(controls[TransitionScope], CB_SETCURSEL, 2, 0);
    transitionCommand(TransitionScope);
    SendMessageW(controls[TransitionOverride], BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(controls[TransitionPreset], CB_SETCURSEL, 0, 0);
    transitionCommand(TransitionApply);
    check(editor.level.portals[0].transitionOverride, "native per portal override");
    editor.undo();
    check(!editor.level.portals[0].transitionOverride, "portal transition undo");
    editor.redo();
    bool rejectedTransition = false;
    renderer.setTransitionSource(resources.text("shaders/transition_custom.glsl"));
    try {
        renderer.setTransitionSource("invalid GLSL");
    } catch (...) {
        rejectedTransition = true;
    }
    check(rejectedTransition, "custom transition compiler error retains working shader");
    config.transitionShader = "shaders/transition_custom.glsl";
    save();
    check(resources.config().mapTransition.kind == TransitionKind::Diamond &&
              resources.config().deathTransition.kind == TransitionKind::Iris,
          "transition project persistence");
    SendMessageW(transitionEditor, WM_CLOSE, 0, 0);
    *visual = *initial_;
    visual->transition = {};
    visual->transition.phase = TransitionPhase::Closing;
    visual->transition.config.closeSeconds = 1;
    visual->transition.config.playerCenter = false;
    visual->transition.elapsed = .5f;
    for (int kind = 1; kind <= 7; ++kind) {
        visual->transition.config.kind = TransitionKind(kind);
        renderer.draw(*visual, camera);
        renderer.capture(output / ("transition_" + std::to_string(kind) + ".ppm"));
        check(glGetError() == GL_NO_ERROR, "preset/custom transition real GL rendering");
    }
    auto imageBytes = [&](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::string magic;
        int width, height, max;
        input >> magic >> width >> height >> max;
        input.get();
        std::vector<unsigned char> data(width * height * 3);
        input.read(reinterpret_cast<char*>(data.data()), data.size());
        check(bool(input) && magic == "P6", "transition capture readable");
        return data;
    };
    // Even an author mask returning zero cannot reveal a destination during commit.
    renderer.setTransitionSource("float echo_transition(vec2 uv,float cover){return 0.0;}");
    visual->transition.config.kind = TransitionKind::Custom;
    visual->transition.config.color = {0, 0, 0};
    visual->transition.phase = TransitionPhase::Covered;
    renderer.draw(*visual, camera);
    renderer.capture(output / "transition-covered.ppm");
    for (auto byte : imageBytes(output / "transition-covered.ppm"))
        check(byte == 0, "commit framebuffer fully covered");
    visual->transition = {};
    renderer.draw(*visual, camera);
    renderer.capture(output / "transition-baseline.ppm");
    auto baseline = imageBytes(output / "transition-baseline.ppm");
    visual->transition.phase = TransitionPhase::Opening;
    visual->transition.config.kind = TransitionKind::Custom;
    visual->transition.elapsed = visual->transition.config.openSeconds;
    renderer.draw(*visual, camera);
    renderer.capture(output / "transition-visible.ppm");
    check(imageBytes(output / "transition-visible.ppm") == baseline,
          "reveal endpoint exactly preserves scene");
    renderer.setTransitionSource(resources.text("shaders/transition_custom.glsl"));
    visual->transition = {};
    command(Edit);
    showDatabaseEditor();
    SendMessageW(controls[DataCategory], CB_SETCURSEL, 1, 0);
    databaseCommand(DataCategory);
    SendMessageW(controls[DataList], LB_SETCURSEL, 1, 0);
    databaseCommand(DataList);
    SetWindowTextW(controls[DataName], L"测试奖励次数");
    SetWindowTextW(controls[DataInitial], L"7");
    databaseCommand(DataApply);
    check(database.variables[1].initial == 7 && initial_->events.variables[1] == 7,
          "Native database default");
    captureUi(output / "database.bmp", databaseEditor);
    SendMessageW(databaseEditor, WM_CLOSE, 0, 0);
    showMapTools();
    captureUi(output / "map-tools.bmp", mapToolsEditor);
    SendMessageW(controls[MapGrid], BM_SETCHECK, 0, 0);
    mapToolsCommand(MapGrid);
    check(!editorGrid, "Map grid toggle");
    editorGrid = true;
    loadWorkspaceSettings();
    check(!editorGrid, "map tools visibility preference persists");
    command(WorkspaceReset);
    check(editorGrid && SendMessageW(controls[MapGrid], BM_GETCHECK, 0, 0) == BST_CHECKED,
          "workspace reset synchronizes open map tools");
    SendMessageW(controls[MapGrid], BM_SETCHECK, 1, 0);
    mapToolsCommand(MapGrid);
    SendMessageW(mapToolsEditor, WM_CLOSE, 0, 0);
    if (!findEvent(editor.events, 32)) {
        auto debugEvent = eventPreset(32, 2);
        debugEvent.name.set("Debug parallel timer");
        debugEvent.pages[0].switchId = 127;
        EventCommand debugControl;
        debugControl.op = EventOp::PlayerControl;
        debugControl.a = 2;
        debugEvent.pages[0].commands.insert(debugEvent.pages[0].commands.begin(), debugControl);
        editor.events.events.push_back(debugEvent);
        EventPlacement p;
        p.id = 32;
        p.region = {600, 500, 32, 48};
        editor.level.events.push(p);
    }
    showDebugger();
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    auto debugClock = simulation->world().clock;
    simulation->world().events.switches[127] = 1;
    debuggerCommand(DebugStep);
    check(debugPaused && simulation->world().clock > debugClock, "Native debugger single step");
    SendMessageW(controls[DebugData], CB_SETCURSEL, 129, 0);
    SetWindowTextW(controls[DebugValue], L"99");
    debuggerCommand(DebugWrite);
    check(simulation->world().events.variables[1] == 99, "Native debugger snapshot write");
    auto comparisonWorld = std::make_unique<WorldState>(simulation->world());
    int comparisonFrames = simulation->history().size();
    auto comparisonLast = std::make_unique<WorldState>(simulation->history().at(comparisonFrames - 1));
    snapshotCompareCommand(SnapshotCompareEditor);
    SetWindowTextW(controls[SnapshotFilter], L"events.variables[1]");
    snapshotCompareCommand(SnapshotCompare);
    check(snapshotReport->matching == 1 && snapshotReport->entries.count == 1 &&
              std::string_view(snapshotReport->entries[0].before.c_str()) == "7" &&
              std::string_view(snapshotReport->entries[0].after.c_str()) == "99",
          "Native pinned oldest / current field comparison and filtering");
    exportSnapshotReport(output / "snapshot-diff.tsv");
    std::ifstream comparisonFile(output / "snapshot-diff.tsv", std::ios::binary);
    std::string comparisonText((std::istreambuf_iterator<char>(comparisonFile)), {});
    check(comparisonText.find("events.variables[1]\t7\t99") != std::string::npos,
          "Native bounded TSV snapshot export");
    captureUi(output / "snapshot-compare-light.bmp", snapshotCompareEditor);
    setTheme(true);
    captureUi(output / "snapshot-compare-dark.bmp", snapshotCompareEditor);
    setTheme(false);
    check(debugPaused && simulation->history().size() == comparisonFrames &&
              std::memcmp(comparisonWorld.get(), &simulation->world(), sizeof(WorldState)) == 0 &&
              std::memcmp(comparisonLast.get(), &simulation->history().at(comparisonFrames - 1),
                          sizeof(WorldState)) == 0,
          "Native comparison/export do not alter world, history or pause");
    debuggerCommand(DebugOldest);
    check(simulation->world().events.variables[1] == 7 && simulation->history().size() == 1,
          "Native debugger oldest branch");
    snapshotCompareCommand(SnapshotCompare);
    check(snapshotReport->matching == 0 && snapshotA->events.variables[1] == 7,
          "Pinned baseline survives history truncation and re-use");
    SendMessageW(controls[SnapshotSourceA], CB_SETCURSEL, 0, 0);
    snapshotCompareCommand(SnapshotPinA);
    check(snapshotReport->equal(), "Native current / current comparison is exact");
    simulation->world().events.switches[127] = 1;
    debuggerCommand(DebugStep);
    refreshDebugger();
    check(SendMessageW(controls[DebugEvents], LB_GETCOUNT, 0, 0) > 0,
          "Debugger shows runtime event slots");
    wchar_t controlSummary[512]{};
    GetWindowTextW(controls[DebugControlSummary], controlSummary, 512);
    check(std::wstring_view(controlSummary).find(L"地图#32") != std::wstring_view::npos,
          "Native debugger explains event control owner");
    captureUi(output / "debugger.bmp", debuggerEditor);
    SendMessageW(debuggerEditor, WM_CLOSE, 0, 0);
    check(!debugPaused && !renderer.debugCollisions, "Debugger close releases pause/overlays");
    check(!snapshotCompareEditor && !snapshotA && !snapshotB && !snapshotReport,
          "Debugger close releases comparison buffers");
    command(Edit);
    showProjectHealth();
    editor.level.spawn = {900, 200};
    healthCommand(RecoverySave);
    auto recoveryCell = editor.level.layers[1].cells[0];
    editor.level.layers[1].cells[0] = recoveryCell == 16 ? 1 : 16;
    editor.dirty = true;
    healthCommand(RecoveryRestore);
    check(initial_->kino.x == 900 && camera.logicalX > 0,
          "Recovery synchronizes spawn and editor camera");
    check(editor.dirty && editor.level.layers[1].cells[0] == recoveryCell,
          "Native recovery loads unsaved editor state");
    check(std::filesystem::exists(resources.root / ".echo/recovery-before-restore.echo"),
          "Recovery preserves pre-restore editor");
    showProjectHealth();
    captureUi(output / "project-health.bmp", healthEditor);
    SendMessageW(healthEditor, WM_CLOSE, 0, 0);
    save();
    check(resources.text("database.echo") == saveDatabase(database), "Recovered database commit");
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    for (int i = 0; i < 420; ++i)
        simulation->tick({});
    Input recoveredTrace;
    recoveredTrace.trace = true;
    for (int i = 0; i < 600; ++i) {
        simulation->tick(recoveredTrace);
        if (simulation->world().state != TimeState::Trace)
            break;
    }
    check(simulation->history().size() == 1, "Recovered project reaches oldest history");
    simulation->tick({});
    simulation->tick({});
    check(simulation->world().state == TimeState::Free, "Recovered project oldest rewind/resume");
    command(Edit);
    showObjectsEditor();
    check(SendMessageW(controls[ObjectList], LB_GETCOUNT, 0, 0) == int(objectLibrary.templates.size()),
          "Native object templates listed");
    objectTemplateIndex = 0;
    objectForm();
    SetWindowTextW(controls[ObjectName], L"巡逻示例");
    SetWindowTextW(controls[ObjectSpeed], L"44");
    SetWindowTextW(controls[ObjectDamage], L"0");
    auto clickRiderOption = [&](int id) {
        SendMessageW(controls[id], WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
        SendMessageW(controls[id], WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
    };
    clickRiderOption(ObjectRide);
    clickRiderOption(ObjectCrush);
    check(SendMessageW(controls[ObjectRide], BM_GETCHECK, 0, 0) == FALSE &&
              SendMessageW(controls[ObjectCrush], BM_GETCHECK, 0, 0) == FALSE,
          "Native mouse toggles object rider checkboxes");
    objectCommand(ObjectApply);
    check(objectLibrary.templates[0].speed == 44 && !objectLibrary.templates[0].ridesPlatforms &&
              !objectLibrary.templates[0].crushKills,
          "Native template rider / crush form applies");
    auto riderTemplates = loadObjects(saveObjects(objectLibrary));
    check(!riderTemplates.templates[0].ridesPlatforms && !riderTemplates.templates[0].crushKills,
          "Native object rider policy saves");
    clickRiderOption(ObjectRide);
    SendMessageW(controls[ObjectKindSelect], CB_SETCURSEL, int(ObjectKind::Player), 0);
    objectCommand(ObjectKindSelect, CBN_SELCHANGE);
    check(!IsWindowEnabled(controls[ObjectRide]) && !IsWindowEnabled(controls[ObjectCrush]),
          "Native special player uses dedicated movement policy");
    SendMessageW(controls[ObjectKindSelect], CB_SETCURSEL, int(ObjectKind::Enemy), 0);
    objectCommand(ObjectKindSelect, CBN_SELCHANGE);
    check(IsWindowEnabled(controls[ObjectRide]), "Ordinary actor restores rider controls");
    objectCommand(ObjectApply);
    objectCommand(ObjectPlace);
    auto beforeObjects = editor.level.objects.count;
    editor.paint(1100, 400);
    editor.end();
    objectInstanceIndex = editor.selectedObject;
    objectForm();
    SetWindowTextW(controls[ObjectInstanceSpeed], L"11");
    SetWindowTextW(controls[ObjectInstanceHP], L"1000");
    SendMessageW(controls[ObjectOverrideSpeed], BM_SETCHECK, TRUE, 0);
    SendMessageW(controls[ObjectOverrideHP], BM_SETCHECK, TRUE, 0);
    objectCommand(ObjectInstanceApply);
    check(editor.level.objects.count == beforeObjects + 1 &&
              editor.level.objects[objectInstanceIndex].speed == 11,
          "Native instance deploy and override");
    captureUi(output / "objects-light.bmp", objectsEditor);
    setTheme(true);
    captureUi(output / "objects-dark.bmp", objectsEditor);
    setTheme(false);
    save();
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    for (int i = 0; i < 420; ++i)
        simulation->tick({});
    simulation->seekHistory(0);
    check(simulation->world().objects.slots[0].alive && simulation->world().objects.slots[0].speed == 11,
          "Native object oldest snapshot");
    simulation->tick({});
    check(simulation->world().state == TimeState::Free, "Native object continue after oldest");
    command(Edit);
    SendMessageW(objectsEditor, WM_CLOSE, 0, 0);
    showAssetsEditor();
    auto playerAsset = findAssetPath(assetLibrary, config.player.path);
    check(playerAsset && playerAsset->kind == AssetKind::Image, "Native image asset metadata");
    auto playerAssetId = playerAsset->id;
    selectedAsset = playerAssetId;
    assetForm();
    check(assetThumbnail != nullptr, "Native RGBA thumbnail");
    SetWindowTextW(controls[AssetSearch], L"tiles");
    check(!visibleAssets.empty() && visibleAssets.size() < assetLibrary.entries.size(),
          "Native resource search");
    SetWindowTextW(controls[AssetSearch], L"");
    selectedAsset = playerAssetId;
    assetForm();
    SetWindowTextW(controls[AssetPath], L"assets/moved/native-player.rgba");
    assetCommand(AssetMove);
    check(config.player.path == assetReference(playerAssetId) && resources.contains(config.player.path),
          "Native resource rename retains stable player reference");
    check(assetThumbnail != nullptr, "Native thumbnail after move/reload");
    render();
    captureUi(output / "assets-light.bmp", assetsEditor);
    setTheme(true);
    captureUi(output / "assets-dark.bmp", assetsEditor);
    setTheme(false);
    SendMessageW(assetsEditor, WM_CLOSE, 0, 0);
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    for (int i = 0; i < 420; ++i)
        simulation->tick({});
    simulation->seekHistory(0);
    simulation->tick({});
    check(simulation->world().state == TimeState::Free, "Stable assets continue after farthest rewind");
    command(Edit);
    showAnimationsEditor();
    animationCommand(AnimationNew);
    SetWindowTextW(controls[AnimationName], L"奔跑与脚步示例");
    animationCommand(AnimationApply);
    setNumber(AnimationSplitCount, 4);
    setNumber(AnimationSplitFPS, 10);
    animationCommand(AnimationSplit);
    check(animationLibrary.clips.back().frames.size() == 4, "Native sprite sheet split");
    SendMessageW(controls[AnimationVariable], CB_SETCURSEL, 3, 0);
    setNumber(AnimationDelta, 3);
    SendMessageW(controls[AnimationCollision], BM_SETCHECK, TRUE, 0);
    setNumber(AnimationCollisionX, 0);
    setNumber(AnimationCollisionY, 0);
    setNumber(AnimationCollisionW, playerWidth);
    setNumber(AnimationCollisionH, playerHeight);
    animationCommand(AnimationFrameApply);
    auto animationId = animationLibrary.clips.back().id;
    for (int state = 0; state < 4; ++state) {
        SendMessageW(controls[AnimationBindingState], CB_SETCURSEL, state, 0);
        animationCommand(AnimationBind);
    }
    captureUi(output / "animations-light.bmp", animationsEditor);
    setTheme(true);
    captureUi(output / "animations-dark.bmp", animationsEditor);
    setTheme(false);
    save();
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    for (int i = 0; i < 420; ++i)
        simulation->tick({});
    check(simulation->world().events.variables[2] > 3,
          "Native loop checkbox preserves repeated markers");
    simulation->seekHistory(0);
    check(simulation->world().kino.animation.clip == animationId, "Native animation oldest snapshot");
    auto markerBefore = simulation->world().events.variables[2];
    simulation->tick({});
    simulation->tick({});
    check(simulation->world().state == TimeState::Free &&
              simulation->world().events.variables[2] >= markerBefore,
          "Native animation resumes its marker cursor");
    command(Edit);
    SendMessageW(animationsEditor, WM_CLOSE, 0, 0);
    showBarrageEditor();
    barrageCommand(BarrageNew);
    SetWindowTextW(controls[BarrageName], L"组合环形示例");
    setNumber(BarrageDamage, 0);
    setNumber(BarrageCount, 16);
    SendMessageW(controls[BarrageAimSelect], CB_SETCURSEL, int(BarrageAim::AtWarning), 0);
    SendMessageW(controls[BarrageTerrain], CB_SETCURSEL, 1, 0);
    barrageCommand(BarragePatternApply);
    barrageCommand(BarrageEmitterNew);
    SetWindowTextW(controls[BarrageEmitterName], L"地图发射器示例");
    SetWindowTextW(controls[BarrageSeed], L"4294967295");
    setNumber(BarrageX, 640);
    setNumber(BarrageY, 240);
    SendMessageW(controls[BarragePersistent], BM_SETCHECK, TRUE, 0);
    barrageCommand(BarrageEmitterApply);
    setNumber(BarrageWaveRepeat, 3);
    setNumber(BarrageInterval, .2f);
    barrageCommand(BarrageWaveApply);
    auto emitterId = editor.barrage.emitters.back().id;
    auto propertiesUndo = editor.undoCount();
    barrageCommand(BarrageUndo);
    check(editor.barrage.emitters.back().waves[0].repeat == 1 &&
              editor.undoCount() + 1 == propertiesUndo,
          "Native wave property undo");
    barrageCommand(BarrageRedo);
    check(editor.barrage.emitters.back().waves[0].repeat == 3 &&
              editor.barrage.emitters.back().waves[0].interval == .2f,
          "Native wave property redo");
    auto noOpUndo = editor.undoCount();
    barrageCommand(BarragePatternApply);
    check(editor.undoCount() == noOpUndo, "Native no-op property apply added undo");
    setNumber(BarrageCount, 513);
    bool invalidBarrage = false;
    try {
        barrageCommand(BarragePatternApply);
    } catch (...) {
        invalidBarrage = true;
    }
    check(invalidBarrage && editor.undoCount() == noOpUndo && editor.barrage.patterns.back().count == 16,
          "Native invalid property apply mutated definition or history");
    barrageForm();
    showBarrageTargetEditor();
    SendMessageW(controls[BarrageTargetSelect], CB_SETCURSEL, int(BarrageTarget::Point), 0);
    barrageCommand(BarrageTargetSelect, CBN_SELCHANGE);
    check(IsWindowEnabled(controls[BarrageTargetX]) && !IsWindowEnabled(controls[BarrageTargetObject]),
          "Native point target conditional form");
    SendMessageW(controls[BarrageTargetSelect], CB_SETCURSEL, int(BarrageTarget::Object), 0);
    SendMessageW(controls[BarrageTargetFilter], CB_SETCURSEL, int(BarrageFactionFilter::Only), 0);
    SendMessageW(controls[BarrageTargetObject], CB_SETCURSEL, 1, 0);
    setNumber(BarrageTargetFaction, objectLibrary.templates[0].faction);
    setNumber(BarrageTargetRange, 1372);
    barrageCommand(BarrageTargetSelect, CBN_SELCHANGE);
    check(IsWindowEnabled(controls[BarrageTargetObject]) &&
              IsWindowEnabled(controls[BarrageTargetFaction]) &&
              !IsWindowEnabled(controls[BarrageTargetX]),
          "Native object target conditional form");
    auto selectedTarget =
        std::uint64_t(SendMessageW(controls[BarrageTargetObject], CB_GETITEMDATA, 1, 0));
    check(selectedTarget != 0, "Native target choices contain authored stable IDs");
    captureUi(output / "barrage-target-light.bmp", barrageTargetEditor);
    setTheme(true);
    captureUi(output / "barrage-target-dark.bmp", barrageTargetEditor);
    setTheme(false);
    barrageCommand(BarrageTargetApply);
    check(!barrageTargetEditor && editor.barrage.emitters.back().targetObject == selectedTarget &&
              editor.barrage.emitters.back().targetRange == 1372 &&
              editor.barrage.emitters.back().factionFilter == BarrageFactionFilter::Only,
          "Native target apply preserves object identity and filter");
    showBarrageTargetEditor();
    check(selection(BarrageTargetSelect) == int(BarrageTarget::Object) &&
              std::uint64_t(SendMessageW(controls[BarrageTargetObject], CB_GETITEMDATA,
                                         selection(BarrageTargetObject), 0)) == selectedTarget,
          "Native target popup reopen preserved reference");
    SendMessageW(barrageTargetEditor, WM_CLOSE, 0, 0);
    check(editor.barrage.emitters.back().seed == UINT32_MAX && editor.barrage.emitters.back().persistent,
          "Native exact seed and persistent checkbox");
    // Exercise actual Win32 map mouse messages, including capture cancellation and double-click.
    barrageCommand(BarrageMapPlace);
    render(0);
    Vec2 mapPosition{camera.renderX + 480, camera.renderY + 240};
    auto screenPoint = [&](Vec2 point) {
        return POINT{
            LONG(renderer.offsetX + (point.x - camera.renderX) * pixelsPerUnit * renderer.scale),
            LONG(renderer.offsetY + (point.y - camera.renderY) * pixelsPerUnit * renderer.scale)};
    };
    auto mouse = [&](UINT msg, Vec2 point, WPARAM buttons = MK_LBUTTON) {
        auto p = screenPoint(point);
        SendMessageW(canvas, msg, buttons, MAKELPARAM(p.x, p.y));
    };
    auto beforePosition = editor.undoCount();
    mouse(WM_LBUTTONDOWN, mapPosition);
    mouse(WM_LBUTTONUP, mapPosition, 0);
    check(editor.barrage.emitters.back().position.x == mapPosition.x &&
              editor.barrage.emitters.back().position.y == mapPosition.y &&
              editor.undoCount() == beforePosition + 1,
          "Native map positioning / one undo record");
    mouse(WM_LBUTTONDOWN, {mapPosition.x + 8, mapPosition.y + 4});
    mouse(WM_MOUSEMOVE, {mapPosition.x + 88, mapPosition.y + 44});
    mouse(WM_LBUTTONUP, {mapPosition.x + 88, mapPosition.y + 44}, 0);
    Vec2 mapDragged{mapPosition.x + 80, mapPosition.y + 40};
    check(editor.barrage.emitters.back().position.x == mapDragged.x &&
              editor.barrage.emitters.back().position.y == mapDragged.y &&
              editor.barrage.emitters.back().targetObject == selectedTarget &&
              editor.undoCount() == beforePosition + 2,
          "Native marker drag retained grab offset / target / one record");
    command(Undo);
    check(editor.barrage.emitters.back().position.x == mapPosition.x, "Native drag undo");
    command(Redo);
    check(editor.barrage.emitters.back().position.x == mapDragged.x, "Native drag redo");
    mouse(WM_LBUTTONDOWN, mapDragged);
    mouse(WM_MOUSEMOVE, {mapDragged.x + 24, mapDragged.y});
    SendMessageW(canvas, WM_CANCELMODE, 0, 0);
    check(!dragging && GetCapture() != canvas &&
              editor.barrage.emitters.back().position.x == mapDragged.x + 24,
          "Native lost capture did not close drag transaction");
    command(Undo);
    check(editor.barrage.emitters.back().position.x == mapDragged.x,
          "Native cancelled capture stroke undo");
    mouse(WM_LBUTTONDBLCLK, mapDragged);
    check(barrageEditor && editor.tool == 23 && barrageEmitterIndex >= 0,
          "Native marker double-click opens definition form");
    render(0);
    renderer.capture(output / "barrage-map.bmp");
    auto beforeCancel = saveBarrage(editor.barrage);
    auto beforeCancelUndo = editor.undoCount();
    barrageCommand(BarrageMapPlace);
    mouse(WM_RBUTTONDOWN, mapDragged, MK_RBUTTON);
    mouse(WM_RBUTTONUP, mapDragged, 0);
    check(!editor.emitterPlacementArmed && editor.undoCount() == beforeCancelUndo &&
              saveBarrage(editor.barrage) == beforeCancel,
          "Native right-click positioning cancellation");
    showBarrageEditor();
    // The non-active-map graph participates in deletion checks.
    auto beforeReference = eventMaps;
    if (eventMaps.size() > 1) {
        auto reference = eventPreset(1, 0);
        EventCommand emitterReference;
        emitterReference.op = EventOp::EmitterControl;
        emitterReference.reference = emitterId;
        reference.pages[0].commands.push_back(emitterReference);
        eventMaps[(editMap + 1) % eventMaps.size()].events.push_back(reference);
        bool referenceRejected = false;
        try {
            barrageCommand(BarrageEmitterDelete);
        } catch (...) {
            referenceRejected = true;
        }
        check(referenceRejected && saveBarrage(editor.barrage) == beforeCancel &&
                  editor.undoCount() == beforeCancelUndo,
              "Native emitter deletion broke another map");
    }
    eventMaps = std::move(beforeReference);
    barragePreviewPlaying = true;
    for (int i = 0; i < 40; ++i)
        tickBarragePreview();
    barragePreviewPlaying = false;
    check(barragePreviewWorld->barrage.volleys > 0 && barragePreviewWorld->bullets.count > 0 &&
              barragePreviewWorld->barrage.emitters[0].targetId != 0,
          "Native isolated barrage preview runs real wave logic");
    captureUi(output / "barrage-light.bmp", barrageEditor);
    setTheme(true);
    captureUi(output / "barrage-dark.bmp", barrageEditor);
    setTheme(false);
    showEventEditor();
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::EmitterControl), 0);
    eventCommandForm();
    auto emitterCommand = eventReadCommand();
    check(emitterCommand.op == EventOp::EmitterControl && emitterCommand.reference == emitterId,
          "Native named emitter event selector stores stable ID");
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::PlayerControl), 0);
    eventCommandForm();
    SendMessageW(controls[EventTarget], CB_SETCURSEL, 5, 0);
    eventCommand(EventTarget, CBN_SELCHANGE);
    for (int i = 0; i < 9; ++i)
        SendMessageW(controls[EventControlFirst + i], BM_SETCHECK, i == 6 || i == 8, 0);
    auto playerControl = eventReadCommand();
    check(playerControl.op == EventOp::PlayerControl && playerControl.a == 5 &&
              playerControl.b == (64 | 256),
          "Native custom control permissions use visible checkboxes");
    captureUi(output / "event-control-light.bmp", eventEditor);
    setTheme(true);
    captureUi(output / "event-control-dark.bmp", eventEditor);
    setTheme(false);
    auto beforeScriptEvents = editor.events;
    int beforeScriptId = eventId;
    eventDraft.pages.resize(1);
    eventPage = 0;
    eventDraft.pages[0] = {};
    eventDraft.pages[0].trigger = EventTrigger::Parallel;
    eventPageForm();
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::Script), 0);
    eventCommandForm();
    SetWindowTextW(controls[EventContent], L"-- 地图事件中的高级 Lua\r\n"
                                           L"engine.set_variable(64, engine.get_variable(64) + 3)\r\n"
                                           L"engine.state[63] = engine.state[63] + 1");
    auto nativeScript = eventReadCommand();
    check(nativeScript.op == EventOp::Script &&
              std::string(nativeScript.text.c_str()).find("engine.state[63]") != std::string::npos &&
              (GetWindowLongPtrW(controls[EventContent], GWL_STYLE) & WS_VISIBLE) &&
              !(GetWindowLongPtrW(controls[EventSpeaker], GWL_STYLE) & WS_VISIBLE),
          "Native event script type / multiline editor / hidden dialogue fields");
    eventCommand(EventInsert);
    auto scriptUndoCount = editor.undoCount();
    eventCommand(EventApply);
    check(editor.undoCount() == scriptUndoCount + 1 &&
              findEvent(editor.events, eventId)->pages[0].commands[0].op == EventOp::Script,
          "Native script apply not in shared authoring history");
    auto scriptDefinitions = saveEvents(editor.events);
    command(Undo);
    check(saveEvents(editor.events) == saveEvents(beforeScriptEvents), "Native script undo");
    command(Redo);
    check(saveEvents(editor.events) == scriptDefinitions, "Native script redo");
    eventSetSelection({0});
    eventCommand(EventCommandList, LBN_SELCHANGE);
    captureUi(output / "event-script-light.bmp", eventEditor);
    setTheme(true);
    captureUi(output / "event-script-dark.bmp", eventEditor);
    setTheme(false);
    auto validScriptDraft = eventDraft;
    SetWindowTextW(controls[EventContent], L"local =");
    bool invalidEventScript = false;
    try {
        eventCommand(EventUpdate);
    } catch (const std::exception& e) {
        invalidEventScript = std::string(e.what()).find("page 1 command 1") != std::string::npos;
    }
    check(invalidEventScript && saveEvents(editor.events) == scriptDefinitions,
          "Native invalid Lua committed definitions / lost location");
    eventDraft = validScriptDraft;
    eventPageForm();
    save();
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 10000;
    auto scriptVariable = simulation->world().events.variables[63];
    simulation->tick({});
    check(simulation->world().events.variables[63] == scriptVariable + 3 &&
              simulation->world().script.persistent[62] > 0 &&
              !simulation->world().events.local[beforeScriptId - 1].scriptFault,
          "Native inline Lua didn't execute in Play");
    command(Edit);
    auto beforeConditionDefinitions = saveEvents(editor.events);
    eventDraft.pages.resize(1);
    eventPage = 0;
    eventDraft.pages[0] = {};
    eventDraft.pages[0].trigger = EventTrigger::Parallel;
    eventPageForm();
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::IfScript), 0);
    eventCommandForm();
    SetWindowTextW(controls[EventContent], L"-- 只读条件；普通开关 / 变量仍可使用表单\r\n"
                                           L"return player.hp > 0 and engine.get_variable(64) >= 1");
    eventCommand(EventInsert);
    check(eventDraft.pages[0].commands.size() == 3 &&
              eventDraft.pages[0].commands[1].op == EventOp::Else &&
              eventDraft.pages[0].commands[2].op == EventOp::EndIf,
          "Native Lua condition auto block");
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::AddVariable), 0);
    eventCommandForm();
    SendMessageW(controls[EventTarget], CB_SETCURSEL, 63, 0);
    setNumber(EventValue, 5);
    eventCommand(EventInsert);
    eventSetSelection({2});
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::AddVariable), 0);
    eventCommandForm();
    SendMessageW(controls[EventTarget], CB_SETCURSEL, 63, 0);
    setNumber(EventValue, 7);
    eventCommand(EventInsert);
    eventSetSelection({4});
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::Wait), 0);
    eventCommandForm();
    setNumber(EventValue, 1);
    eventCommand(EventInsert);
    eventCommand(EventApply);
    auto conditionDefinitions = saveEvents(editor.events);
    command(Undo);
    check(saveEvents(editor.events) == beforeConditionDefinitions, "Native Lua condition undo");
    command(Redo);
    check(saveEvents(editor.events) == conditionDefinitions, "Native Lua condition redo");
    eventSetSelection({0});
    eventCommand(EventCommandList, LBN_SELCHANGE);
    check((GetWindowLongPtrW(controls[EventContent], GWL_STYLE) & WS_VISIBLE) &&
              !(GetWindowLongPtrW(controls[EventSpeaker], GWL_STYLE) & WS_VISIBLE),
          "Native Lua condition multiline form");
    captureUi(output / "event-condition-light.bmp", eventEditor);
    setTheme(true);
    captureUi(output / "event-condition-dark.bmp", eventEditor);
    setTheme(false);
    auto validConditionDraft = eventDraft;
    SetWindowTextW(controls[EventContent], L"return (");
    bool invalidConditionSyntax = false;
    try {
        eventCommand(EventUpdate);
    } catch (const std::exception& e) {
        invalidConditionSyntax = std::string(e.what()).find("page 1 command 1") != std::string::npos;
    }
    check(invalidConditionSyntax && saveEvents(editor.events) == conditionDefinitions,
          "Native invalid Lua condition changed definitions");
    eventDraft = validConditionDraft;
    eventPageForm();
    eventSetSelection({0});
    eventCommand(EventRemove);
    check(eventDraft.pages[0].commands.size() == 1 &&
              eventDraft.pages[0].commands[0].op == EventOp::Wait,
          "Native condition removal carries complete body/else/end");
    eventDraft = validConditionDraft;
    eventPageForm();
    save();
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 10000;
    auto conditionValue = simulation->world().events.variables[63];
    bool conditionResult = simulation->world().kino.hp > 0 && conditionValue >= 1;
    simulation->tick({});
    check(simulation->world().events.variables[63] == conditionValue + (conditionResult ? 5 : 7) &&
              !simulation->world().events.local[beforeScriptId - 1].scriptFault,
          "Native conditional Lua branch Play");
    command(Edit);
    auto beforePageCondition = saveEvents(editor.events);
    eventLoad(beforeScriptId);
    check(!IsWindowEnabled(controls[EventPageScriptSource]), "Native page Lua default disabled");
    SendMessageW(controls[EventPageScriptEnabled], BM_SETCHECK, BST_CHECKED, 0);
    command(EventPageScriptEnabled);
    check(IsWindowEnabled(controls[EventPageScriptSource]), "Native page Lua toggle routing");
    SetWindowTextW(controls[EventPageScriptSource],
                   L"-- 只读页条件：常用开关和变量仍用表单\r\nreturn engine.get_variable(1) == 7");
    eventCommand(EventApply);
    auto pageDefinitions = saveEvents(editor.events);
    check(pageDefinitions != beforePageCondition &&
              findEvent(editor.events, beforeScriptId)->pages[0].scriptConditionEnabled,
          "Native page condition Apply");
    command(Undo);
    check(saveEvents(editor.events) == beforePageCondition, "Native page condition undo");
    command(Redo);
    check(saveEvents(editor.events) == pageDefinitions, "Native page condition redo");
    eventLoad(beforeScriptId);
    captureUi(output / "event-page-condition-light.bmp", eventEditor);
    setTheme(true);
    captureUi(output / "event-page-condition-dark.bmp", eventEditor);
    setTheme(false);
    SetWindowTextW(controls[EventPageScriptSource], L"return (");
    bool invalidPageCondition = false;
    try {
        eventCommand(EventApply);
    } catch (const std::exception& e) {
        invalidPageCondition = std::string(e.what()).find("page 1 page condition") != std::string::npos;
    }
    check(invalidPageCondition && saveEvents(editor.events) == pageDefinitions,
          "Native page syntax failure must preserve definitions");
    eventLoad(beforeScriptId);
    save();
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 10000;
    simulation->world().events.variables[0] = 0;
    simulation->tick({});
    check(simulation->world().events.local[beforeScriptId - 1].page == -1,
          "Native page condition false Play");
    simulation->world().events.variables[0] = 7;
    simulation->tick({});
    check(simulation->world().events.local[beforeScriptId - 1].page == 0 &&
              simulation->world().events.local[beforeScriptId - 1].pageConditionFault == -1,
          "Native page condition true Play");
    command(Edit);
    editor.events = beforeScriptEvents;
    eventLoad(beforeScriptId);
    SendMessageW(eventEditor, WM_CLOSE, 0, 0);
    save();
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    for (int i = 0; i < 420; ++i)
        simulation->tick({});
    check(simulation->world().barrage.volleys > 0, "Native barrage authored emitter plays");
    auto authoringUndoInPlay = editor.undoCount();
    auto playedTick = simulation->world().tick;
    command(Undo);
    check(!editing && editor.undoCount() == authoringUndoInPlay &&
              simulation->world().tick == playedTick,
          "Native authoring undo modified active Play");
    simulation->seekHistory(0);
    check(simulation->world().barrage.emitters[0].id == emitterId,
          "Native barrage oldest restores emitter identity");
    simulation->tick({});
    command(Edit);
    SendMessageW(barrageEditor, WM_CLOSE, 0, 0);
    showFocusEditor();
    SendMessageW(controls[HurtPreset], CB_SETCURSEL, 3, 0);
    focusCommand(HurtPresetApply);
    SendMessageW(controls[FocusActivation], CB_SETCURSEL, 1, 0);
    setNumber(FocusRatio, .3f);
    SendMessageW(controls[HurtShowControl], CB_SETCURSEL, 1, 0);
    focusCommand(HurtRegionCopy);
    check(hurtDraft.count == 2, "Native multi-region copy");
    setNumber(HurtOffsetX, 8);
    focusCommand(HurtRegionApply);
    SendMessageW(controls[RenderTexture], CB_SETCURSEL, 2, 0);
    SendMessageW(controls[RenderUpscale], CB_SETCURSEL, 3, 0);
    SendMessageW(controls[RenderAA], CB_SETCURSEL, 2, 0);
    focusCommand(FocusApply);
    check(initial_->focus.enabled && initial_->focus.mode == FocusMode::Toggle &&
              initial_->hurt.count == 2,
          "Native focus/hurt committed");
    captureUi(output / "focus-light.bmp", focusEditor);
    setTheme(true);
    captureUi(output / "focus-dark.bmp", focusEditor);
    setTheme(false);
    save();
    auto reloaded = resources.config();
    check(reloaded.focus.enabled && reloaded.hurt.count == 2 && reloaded.textureFilter == 2 &&
              reloaded.antiAliasing == 2,
          "Native focus sampling saved");
    command(Play);
    simulation->world().aster.enabled = false;
    simulation->world().kino.invulnTimer = 1000;
    Input slow;
    slow.focus = true;
    slow.move = 1;
    simulation->tick(slow);
    check(simulation->world().kino.focusActive, "Native authored focus movement");
    slow.focus = false;
    for (int i = 0; i < 420; ++i)
        simulation->tick(slow);
    simulation->seekHistory(0);
    check(simulation->world().hurt.count == 2, "Native oldest restores hurt regions");
    simulation->tick({});
    camera.update(simulation->world(), 0, true);
    auto beforeSampling = std::make_unique<WorldState>(simulation->world());
    for (int texture = 0; texture < 3; ++texture)
        for (int upscale = 0; upscale < 6; ++upscale)
            for (int aa = 0; aa < 5; ++aa) {
                renderer.setSampling(texture, upscale, aa, .4f);
                renderer.draw(simulation->world(), camera, false);
                glFinish();
                check(glGetError() == GL_NO_ERROR, "All sampling/AA modes GPU clean");
                if (texture == 0 && aa == 0)
                    renderer.captureDisplay(output / ("upscale-" + std::to_string(upscale) + ".ppm"));
                if (texture == 0 && upscale == 0)
                    renderer.captureDisplay(output / ("display-aa-" + std::to_string(aa) + ".ppm"));
                if (aa == 0 && upscale == 0)
                    renderer.captureDisplay(output / ("texture-" + std::to_string(texture) + ".ppm"));
                if (texture == 0 && upscale == 0)
                    renderer.capture(output / ("aa-" + std::to_string(aa) + ".ppm"));
            }
    check(std::memcmp(beforeSampling.get(), &simulation->world(), sizeof(WorldState)) == 0,
          "Rendering and AA preserve all gameplay/snapshot fields");
    renderer.setSampling(config.textureFilter, config.upscaleFilter, config.antiAliasing,
                         config.renderSharpness);
    command(Edit);
    SendMessageW(focusEditor, WM_CLOSE, 0, 0);
    collisionCommand(CollisionEditor);
    collisionCommand(CollisionSafe);
    collisionCommand(CollisionApply);
    check(!collisionAllowed(*initial_, CollisionClass::Player, CollisionClass::Projectile),
          "Native collision immunity preset");
    check(simulation->history().diagnostics(simulation->world()).lastClear ==
              HistoryClearReason::CollisionSettings,
          "Native matrix reset reason");
    captureUi(output / "collision-rules-light.bmp", collisionEditor);
    setTheme(true);
    captureUi(output / "collision-rules-dark.bmp", collisionEditor);
    setTheme(false);
    save();
    check(!bool(resources.config().collisions.masks[1] & (1u << 5)), "Native pair project persistence");
    collisionCommand(CollisionDefault);
    collisionCommand(CollisionApply);
    int cell = CollisionCell + 1 * 8 + 5;
    SendMessageW(controls[cell], BM_SETCHECK, FALSE, 0);
    collisionCommand(cell);
    check(SendMessageW(controls[CollisionCell + 5 * 8 + 1], BM_GETCHECK, 0, 0) == BST_UNCHECKED,
          "Native matrix mirror");
    collisionCommand(CollisionDefault);
    collisionCommand(CollisionApply);
    SendMessageW(collisionEditor, WM_CLOSE, 0, 0);
    showEventEditor(true);
    SendMessageW(controls[EventTriggerMode], CB_SETCURSEL, 5, 0);
    eventCommand(EventApply);
    check(eventDraft.pages[eventPage].trigger == EventTrigger::Leave, "Native leave trigger form");
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::Move), 0);
    eventCommand(EventCommandType);
    SendMessageW(controls[EventFlag], CB_SETCURSEL, 1, 0);
    setNumber(EventValue, .5f);
    setNumber(EventX, 64);
    setNumber(EventY, 0);
    auto route = eventReadCommand();
    check(route.op == EventOp::Move && route.b == 1, "Native blocked route form");
    captureUi(output / "event-collision-route.bmp", eventEditor);
    SendMessageW(eventEditor, WM_CLOSE, 0, 0);

    showEventEditor(true);
    auto originalChoiceEvents = editor.events;
    int originalChoiceId = eventId;
    SendMessageW(controls[EventCommandType], CB_SETCURSEL, int(EventOp::Choices), 0);
    eventCommand(EventCommandType);
    choiceCommand(EventChoiceOpen);
    check(choiceEditor && eventChoiceDraft.size() == 2, "Native choice editor opens");
    SetWindowTextW(controls[ChoiceLabel], L"接受委托");
    choiceCommand(ChoiceApply);
    SendMessageW(controls[ChoiceList], LB_SETCURSEL, 1, 0);
    choiceCommand(ChoiceList, LBN_SELCHANGE);
    SetWindowTextW(controls[ChoiceLabel], L"秘密路线");
    SendMessageW(controls[ChoiceConditionType], CB_SETCURSEL, 3, 0);
    choiceCommand(ChoiceConditionType, CBN_SELCHANGE);
    SetWindowTextW(controls[ChoiceLua], L"return engine.get_switch(1)");
    choiceCommand(ChoiceApply);
    check(eventChoiceDraft[1].condition == ChoiceCondition::Lua &&
              eventChoiceDraft[0].label.c_str() == std::string("接受委托"),
          "Native conditional choice form");
    captureUi(output / "dialogue-choices-light.bmp", choiceEditor);
    setTheme(true);
    captureUi(output / "dialogue-choices-dark.bmp", choiceEditor);
    setTheme(false);
    auto authoredChoices = eventReadCommand();
    check(authoredChoices.choices.size() == 2, "Choices reach ordinary event command");
    eventCommand(EventInsert);
    eventCommand(EventApply);
    auto withChoices = saveEvents(editor.events);
    check(saveEvents(loadEvents(withChoices)) == withChoices, "Native choice save/load");
    command(Undo);
    check(saveEvents(editor.events) == saveEvents(originalChoiceEvents), "Native choice undo");
    command(Redo);
    check(saveEvents(editor.events) == withChoices, "Native choice redo");
    command(RenderPixelPreset);
    command(Play);
    auto& modal = simulation->world();
    modal.dialogue = {};
    modal.dialogue.active = 8;
    modal.dialogue.choosing = true;
    modal.dialogue.resultVariable = 0;
    modal.dialogue.revealed = 100;
    modal.events.dialogue = {};
    modal.events.dialogue.speaker.set("NPC");
    modal.events.dialogue.text.set("Choose a route");
    for (int i = 0; i < 2; ++i) {
        DialogueOption o;
        o.label = authoredChoices.choices[i].label;
        o.result = i + 1;
        o.enabled = i == 0;
        modal.dialogue.options.push(o);
    }
    modal.gameplayClockPaused = true;
    render();
    captureUi(output / "dialogue-choices-play.bmp");
    check(!(GetWindowLongPtrW(dialogWindow, GWL_STYLE) & WS_VISIBLE),
          "Choice panel is in same GL presentation; no GDI child overlay");
    command(Edit);
    editor.events = originalChoiceEvents;
    eventLoad(originalChoiceId);
    SendMessageW(eventEditor, WM_CLOSE, 0, 0);
    command(RenderPixelPreset);
    check(renderer.textureFilter == 0 && renderer.upscaleFilter == 0 && renderer.antiAliasing == 0 &&
              camera.snapMode == SnapMode::Crisp,
          "Native crisp preset");
    check(config.player.width * pixelsPerUnit == config.player.frame &&
              config.player.height * pixelsPerUnit == config.player.frame &&
              config.boss.width * pixelsPerUnit == config.boss.frame,
          "Demo keeps original sprite pixel size");
    auto pixelWorld = std::make_unique<WorldState>();
    *pixelWorld = makeWorld();
    pixelWorld->aster.enabled = false;
    pixelWorld->kino.x = 160;
    pixelWorld->kino.y = 200;
    pixelWorld->level.tiles.fill(Tile::Empty);
    pixelWorld->level.events.clear();
    pixelWorld->level.platforms.clear();
    pixelWorld->level.colliders.clear();
    pixelWorld->level.lights.clear();
    for (auto& layer : pixelWorld->level.layers) {
        layer.cells.fill(0);
        layer.image.set("");
    }
    CameraState pixelCamera;
    bool bg = renderer.backgroundEnabled, sun = renderer.sunlight, flies = renderer.fireflies,
         flow = renderer.fluid, sdf = renderer.raymarch;
    renderer.backgroundEnabled = renderer.sunlight = renderer.fireflies = renderer.fluid =
        renderer.raymarch = false;
    renderer.hideDialogueOverlay();
    renderer.draw(*pixelWorld, pixelCamera);
    renderer.capture(output / "native-pixels.ppm");
    renderer.captureDisplay(output / "native-pixels-display.ppm");
    auto ppm = [&](const std::filesystem::path& path, int& width, int& height) {
        std::ifstream in(path, std::ios::binary);
        std::string magic;
        int maximum;
        in >> magic >> width >> height >> maximum;
        in.get();
        check(magic == "P6" && maximum == 255, "Pixel verification capture format");
        std::vector<unsigned char> bytes(std::size_t(width) * height * 3);
        in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        check(bool(in), "Pixel verification capture truncated");
        return bytes;
    };
    int vw, vh, dw, dh;
    auto virtualPixels = ppm(output / "native-pixels.ppm", vw, vh);
    auto displayPixels = ppm(output / "native-pixels-display.ppm", dw, dh);
    check(dw == 320 * renderer.scale && dh == 180 * renderer.scale, "Integer display viewport");
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x)
            for (int c = 0; c < 3; ++c)
                check(displayPixels[(y * dw + x) * 3 + c] ==
                          virtualPixels[((y / renderer.scale) * 320 + x / renderer.scale) * 3 + c],
                      "Display contains exact uniform integer blocks; no filtering/ghosting");
    auto spriteBytes = resources.read(config.player.path);
    std::uint32_t sourceWidth;
    std::memcpy(&sourceWidth, spriteBytes.data(), 4);
    int left = int(160 * pixelsPerUnit) + int(std::floor(config.player.offsetX * pixelsPerUnit));
    int top = int(200 * pixelsPerUnit) + int(std::floor(config.player.offsetY * pixelsPerUnit));
    int opaque = 0;
    for (int y = 0; y < config.player.frame; ++y)
        for (int x = 0; x < config.player.frame; ++x) {
            std::size_t source = 8 + (y * sourceWidth + x) * 4;
            if (spriteBytes[source + 3] != 255)
                continue;
            ++opaque;
            for (int c = 0; c < 3; ++c)
                check(virtualPixels[((top + y) * 320 + left + x) * 3 + c] == spriteBytes[source + c],
                      "Native sprite loses or mixes source texels");
        }
    check(opaque > 100, "Native pixel test exercised actual sprite");
    renderer.backgroundEnabled = bg;
    renderer.sunlight = sun;
    renderer.fireflies = flies;
    renderer.fluid = flow;
    renderer.raymarch = sdf;
    render();
    captureUi(output / "original-pixel-editor.bmp");
    frameRateCommand(FrameRateEditor);
    constexpr int expectedRates[] = {30, 60, 90, 120, 144, 0, 75};
    SendMessageW(controls[FrameVsync], BM_SETCHECK, FALSE, 0);
    for (int selected = 0; selected < 7; ++selected) {
        SendMessageW(controls[FrameRatePreset], CB_SETCURSEL, selected, 0);
        frameRateCommand(FrameRatePreset);
        setNumber(FrameRateCustom, 75);
        frameRateCommand(FrameRateApply);
        check(config.frameLimit == expectedRates[selected], "Native FPS presets/custom");
    }
    captureUi(output / "frame-rate-light.bmp", frameRateEditor);
    setTheme(true);
    captureUi(output / "frame-rate-dark.bmp", frameRateEditor);
    setTheme(false);
    save();
    check(resources.config().frameLimit == 75 && !resources.config().vsync,
          "Native frame cap exported project config");
    SendMessageW(frameRateEditor, WM_CLOSE, 0, 0);
    std::ofstream(output / "verification.txt")
        << "PASS native editor commands, paint/drag/undo/save/play, oldest rewind resume\nPASS "
           "light/dark/compact native UI, audio settings and preview, map settings, portrait dialogue, "
           "shader failure recovery, map/global events, tile palette\nPASS transition forms, portal "
           "override undo, 6 builtins/custom GLSL, opaque commit/visible endpoint\nPASS named database, "
           "map tools, debug step/write/oldest branch, unsaved recovery/save/oldest resume\nPASS 7 "
           "environments, "
           "10 weather grades, GPU fluids, SDF\nMax "
           "observed environment frame including swap: "
        << maxFrame << " ms\n"
        << "GL renderer: " << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
        << "\nGL version: " << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << "\n"
        << "2048 bullet mean frame incl snapshot/render/glFinish/swap: " << totalMs / 120
        << " ms; draw calls: " << denseDraws << "\n"
        << "2048 GPU scenario p95_ms: " << denseFrameTimes[114] << "; p99_ms: " << denseFrameTimes[118]
        << "; worst_ms: " << denseFrameTimes.back() << "\n"
        << "PASS native objects, assets, animations, barrage patterns/waves/preview/event "
           "selectors/oldest resume\nPASS native focus / five hit shapes / multi-region editing / saved "
           "sampling / 90 GPU mode combinations / immutable WorldState\n"
           "PASS frame limit 30/60/90/120/144/unlimited/custom, VSync and project persistence\n"
           "PASS debug-only BGM mute / preference migration / unchanged music snapshots / choices "
           "form-runtime-undo / native source texels / exact integer display blocks\n"
           "PASS dialogue in every FRONT frame / no visible GDI overlay / custom confirm / key "
           "conflict rejection / arrow toggle / default reset / keyboard persistence\n"
           "PASS persistent tile dock / mode lists / map tree / compact toolbar / splitter / versioned "
           "preferences / gameplay unchanged / reusable scene inspector\n"
           "PASS native snapshot comparison / pinned baseline / field filter / TSV / light-dark / "
           "read-only world-history / close cleanup and history explanations\n";
    mobilityCommand(MobilityEditor);
    check(mobilityEditor && controls[MobilityFirst + 11], "Native platform preset form opens");
    auto savedFeatures = editor.level.features;
    auto savedMobility = initial_->mobility;
    SendMessageW(controls[MobilityDash], BM_SETCHECK, TRUE, 0);
    SendMessageW(controls[MobilityWall], BM_SETCHECK, TRUE, 0);
    setNumber(MobilityFirst + 4, 720);
    setNumber(MobilityFirst + 11, 180);
    mobilityCommand(MobilityApply);
    check(initial_->mobility.dashEnabled && initial_->mobility.wallJumpEnabled &&
              initial_->mobility.dashSpeed == 720 && initial_->mobility.waterFallSpeed == 180 &&
              simulation->history().diagnostics(simulation->world()).lastClear ==
                  HistoryClearReason::MobilitySettings,
          "Native mobility form applies full snapshotted config");
    auto oldFeatureCount = editor.level.features.count;
    for (int kind = 0; kind < 6; ++kind) {
        SendMessageW(controls[FeatureKindControl], CB_SETCURSEL, kind, 0);
        mobilityCommand(FeatureDraw);
        editor.paint(320 + kind * 128, 240);
        editor.paint(350 + kind * 128, 300);
        editor.end();
        check(editor.level.features.count == oldFeatureCount + kind + 1,
              "Native feature brush creates region");
        mobilityCommand(MobilityEditor);
    }
    auto definitions = saveLevel(editor.level);
    check(saveLevel(loadLevel(definitions)) == definitions, "Native feature level round trip");
    command(Undo);
    command(Redo);
    check(saveLevel(editor.level) == definitions, "Native feature undo/redo retains stable IDs");
    editor.selectedFeature = int(editor.level.features.count) - 1;
    mobilityForm();
    setNumber(FeatureSpeed, 160);
    setNumber(FeatureTravelX, 96);
    setNumber(FeatureTravelY, -256);
    setNumber(FeatureDelay, .6f);
    SendMessageW(controls[FeatureRepeat], BM_SETCHECK, FALSE, 0);
    mobilityCommand(FeatureApply);
    check(editor.level.features[editor.selectedFeature].speed == 160 &&
              editor.level.features[editor.selectedFeature].travel.x == 96 &&
              editor.level.features[editor.selectedFeature].travel.y == -256 &&
              !editor.level.features[editor.selectedFeature].repeat &&
              IsWindowEnabled(controls[FeatureTravelX]) && !IsWindowEnabled(controls[FeatureReset]),
          "Native platform property form");
    auto previous = saveLevel(editor.level);
    setNumber(FeatureSpeed, 50000);
    bool badFeature = false;
    try {
        mobilityCommand(FeatureApply);
    } catch (...) {
        badFeature = true;
    }
    check(badFeature && saveLevel(editor.level) == previous, "Invalid feature leaves map untouched");
    mobilityForm();
    captureUi(output / "platform-presets-light.bmp", mobilityEditor);
    setTheme(true);
    captureUi(output / "platform-presets-dark.bmp", mobilityEditor);
    setTheme(false);
    save();
    check(resources.config().mobility.dashEnabled && resources.config().mobility.waterFallSpeed == 180,
          "Native mobility project persistence");
    editor.level.features = savedFeatures;
    initial_->mobility = config.mobility = savedMobility;
    editor.selectedFeature = -1;
    SendMessageW(mobilityEditor, WM_CLOSE, 0, 0);
    save();
    std::ofstream(output / "verification.txt", std::ios::app)
        << "PASS platform preset GUI / six regions / lift route and once / stable-ID undo-redo / "
           "parameter rejection / "
           "mobility save / light-dark\n";
    verifyProgression(*this, output);
    verifyEventEditing(*this, output);
    // Real loader check at the end: earlier test references must not survive this reset.
    auto finalProject = resources.text("project.echo");
    initial_->rewind.fast_stop_near_bullets = false;
    initial_->rewind.fast_bullet_margin = 7;
    writeTextAtomically(resources.root / "project.echo", projectText());
    loadProject(HistoryClearReason::ProjectLoad);
    check(!initial_->rewind.fast_stop_near_bullets && initial_->rewind.fast_bullet_margin == 7 &&
              !simulation->world().rewind.fast_stop_near_bullets,
          "native saved fast proximity rules load into the runtime world");
    writeTextAtomically(resources.root / "project.echo", finalProject);
    loadProject(HistoryClearReason::ProjectLoad);
    std::ofstream(output / "verification.txt", std::ios::app)
        << "PASS native fast proximity rules / switch / margin validation / save-load / light-dark\n";
}
} // namespace echo
