#include "echo/app.hpp"
#include "echo/recovery.hpp"
#include <fstream>
#include <stdexcept>
namespace echo {
namespace {
Recovery capture(App& a) {
    if (a.eventEditor && a.eventId >= 0)
        a.eventCommand(EventApply);
    Recovery r;
    r.map = a.config.maps.at(a.editMap);
    r.files = {{"project.echo", a.projectText()},
               {"database.echo", saveDatabase(a.database)},
               {"objects.echo", saveObjects(a.objectLibrary)},
               {"animations.echo", saveAnimations(a.animationLibrary)},
               {"barrage.echo", saveBarrage(a.editor.barrage)},
               {"assets.echo", saveAssets(a.assetLibrary)},
               {"events/common.echo", saveEvents(a.editor.common, true)},
               {r.map, saveLevel(a.editor.level)},
               {r.map + ".events", saveEvents(a.editor.events)}};
    return r;
}
std::string readRecoveryFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("没有恢复文件，请先创建恢复点");
    auto length = f.tellg();
    if (length < 0 || std::uint64_t(length) > recoveryBudget)
        throw std::runtime_error("恢复文件超过 32 MiB");
    std::string value(static_cast<std::size_t>(length), '\0');
    f.seekg(0);
    f.read(value.data(), length);
    if (!f)
        throw std::runtime_error("无法读取恢复文件");
    return value;
}
} // namespace
void App::autosave() {
    if (resources.published || !editing)
        return;
    auto text = saveRecovery(capture(*this));
    auto folder = resources.root / ".echo";
    // Two bounded recovery generations. Atomic replacement prevents partial snapshots.
    auto resolved = std::filesystem::weakly_canonical(folder).lexically_relative(
        std::filesystem::weakly_canonical(resources.root));
    if (resolved.empty() || *resolved.begin() == "..")
        throw std::runtime_error("恢复目录不在工程内");
    std::filesystem::create_directories(folder);
    auto latest = folder / "recovery-latest.echo";
    if (std::filesystem::exists(latest))
        writeTextAtomically(folder / "recovery-previous.echo", readRecoveryFile(latest));
    writeTextAtomically(latest, text);
    message = "恢复点已保存";
}
void App::showProjectHealth() {
    command(Edit);
    if (healthEditor && IsWindow(healthEditor)) {
        SetForegroundWindow(healthEditor);
        return;
    }
    healthEditor =
        CreateWindowW(L"EchoEngineWindow", L"工程检查与恢复",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      100, 80, 1090, 650, window, nullptr, instance, this);
    control(L"STATIC",
            L"检查当前编辑内容的资源引用、事件调用与地图入口；列表显示可定位的文件与事件编号。", 0, 20,
            18, 1030, 28, 0, healthEditor);
    control(L"LISTBOX", L"", HealthList, 20, 60, 1030, 390,
            WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | WS_TABSTOP, healthEditor);
    SendMessageW(controls[HealthList], LB_SETHORIZONTALEXTENT, 2200, 0);
    control(L"BUTTON", L"重新检查", HealthRefresh, 20, 471, 300, 33, WS_TABSTOP, healthEditor);
    control(L"BUTTON", L"保存恢复点", RecoverySave, 336, 471, 350, 33, WS_TABSTOP, healthEditor);
    control(L"BUTTON", L"恢复最近自动保存", RecoveryRestore, 702, 471, 350, 33, WS_TABSTOP,
            healthEditor);
    control(L"STATIC",
            L"编辑时每60秒自动保存脏工程。恢复只载入编辑器，Ctrl+S才写回工程；恢复前另留备份。", 0, 20,
            531, 1030, 27, 0, healthEditor);
    control(L"STATIC",
            L".echo内保留最新／上一代／恢复前共3份，每份最多32 MiB；不含图片、音频和Lua文件，也不导出。",
            0, 20, 565, 1030, 27, 0, healthEditor);
    healthCommand(HealthRefresh);
}
void App::healthCommand(int id) {
    if (id == ProjectHealthEditor) {
        showProjectHealth();
        return;
    }
    if (id == RecoverySave) {
        autosave();
        healthCommand(HealthRefresh);
        return;
    }
    if (id == RecoveryRestore) {
        if (!editing)
            throw std::runtime_error("请先返回编辑模式再恢复");
        auto restored = loadRecovery(readRecoveryFile(resources.root / ".echo/recovery-latest.echo"));
        validateRecovery(restored, resources);
        auto folder = std::filesystem::weakly_canonical(resources.root / ".echo");
        auto relative = folder.lexically_relative(std::filesystem::weakly_canonical(resources.root));
        if (relative.empty() || *relative.begin() == "..")
            throw std::runtime_error("恢复目录不在工程内");
        Resources candidate = resources;
        candidate.setEditorOverlay(restored.files);
        for (const auto& issue : inspectProject(candidate))
            if (issue.error)
                throw std::runtime_error("恢复资源校验失败：" + issue.location + " / " +
                                         issue.message);
        writeTextAtomically(folder / "recovery-before-restore.echo", saveRecovery(capture(*this)));
        resources.setEditorOverlay(restored.files);
        loadProject(HistoryClearReason::Recovery);
        auto found = std::find(config.maps.begin(), config.maps.end(), restored.map);
        editMap = int(found - config.maps.begin());
        editor.set(maps[editMap], eventMaps[editMap], commonEvents);
        initial_->level = editor.level;
        initial_->activeMap = editMap;
        editing = true;
        editor.dirty = true;
        initial_->kino.x = editor.level.spawn.x;
        initial_->kino.y = editor.level.spawn.y;
        camera.update(*initial_, 0, true);
        renderer.environment = editor.level.background;
        SendMessageW(controls[Environment], CB_SETCURSEL, renderer.environment, 0);
        message = "已载入恢复草稿，保存后生效";
        refreshChrome();
        return;
    }
    if (id != HealthRefresh || !healthEditor)
        return;
    auto r = capture(*this);
    Resources candidate = resources;
    candidate.setEditorOverlay(r.files);
    auto issues = inspectProject(candidate);
    SendMessageW(controls[HealthList], LB_RESETCONTENT, 0, 0);
    if (issues.empty()) {
        std::wstring text = L"检查通过：没有发现资源、事件引用或地图入口问题。";
        SendMessageW(controls[HealthList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    for (const auto& issue : issues) {
        auto text = wide(std::string(issue.error ? "错误  " : "提示  ") + issue.location + "  ·  " +
                         issue.message);
        SendMessageW(controls[HealthList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
}
} // namespace echo
