#include "echo/app.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace echo {
namespace {
std::string readFilter(HWND h) {
    std::wstring text(GetWindowTextLengthW(h) + 1, 0);
    GetWindowTextW(h, text.data(), int(text.size()));
    text.resize(wcslen(text.c_str()));
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), n, nullptr, nullptr);
    return result;
}
const WorldState& selectedState(App& a, int control) {
    auto& h = a.simulation->history();
    int source = a.selection(control);
    if (source == 0)
        return a.simulation->world();
    if (source < 1 || source > 3)
        throw std::runtime_error("Select a snapshot source");
    if (!h.size())
        throw std::runtime_error("History is empty; select the current world");
    int cursor = source == 2   ? 0
                 : source == 3 ? h.size() - 1
                               : int(SendMessageW(a.controls[DebugTimeline], TBM_GETPOS, 0, 0));
    return h.at(std::clamp(cursor, 0, h.size() - 1));
}
std::wstring stamp(const WorldState& w) {
    std::wostringstream s;
    s << std::fixed << std::setprecision(3) << L"地图 " << w.activeMap + 1 << L" · 时间 " << w.clock
      << L"秒 · Tick " << w.tick << L" · 时间线 " << w.timelineGenerationId;
    return s.str();
}
const wchar_t* clearReason(HistoryClearReason r) {
    switch (r) {
    case HistoryClearReason::DatabaseDefinitions:
        return L"数据库定义修改";
    case HistoryClearReason::MobilitySettings:
        return L"平台动作参数应用";
    case HistoryClearReason::CollisionSettings:
        return L"\u5e94\u7528\u78b0\u649e\u5206\u7c7b";
    case HistoryClearReason::Startup:
        return L"启动，尚未建立历史";
    case HistoryClearReason::Manual:
        return L"显式清空历史";
    case HistoryClearReason::Reset:
        return L"显式重置试玩世界";
    case HistoryClearReason::ProjectLoad:
        return L"加载工程";
    case HistoryClearReason::Play:
        return L"开始新试玩／提交编辑定义";
    case HistoryClearReason::FocusSettings:
        return L"应用判定／低速参数";
    case HistoryClearReason::MapSettings:
        return L"应用地图设置";
    case HistoryClearReason::ResourceReload:
        return L"资源变更后重新载入";
    case HistoryClearReason::ScriptReload:
        return L"重新载入脚本";
    case HistoryClearReason::Recovery:
        return L"载入工程恢复草稿";
    case HistoryClearReason::Restart:
        return L"玩家重开";
    case HistoryClearReason::EventDefinitions:
        return L"事件定义改变";
    case HistoryClearReason::Enabled:
        return L"启用回溯，重新积累";
    case HistoryClearReason::Disabled:
        return L"关闭回溯";
    case HistoryClearReason::ScriptEnabled:
        return L"运行时脚本启用回溯";
    case HistoryClearReason::ScriptDisabled:
        return L"运行时脚本关闭回溯";
    }
    return L"未知";
}
} // namespace
void App::refreshHistoryDiagnostics() {
    auto d = simulation->history().diagnostics(simulation->world());
    std::wostringstream line;
    line << std::fixed << std::setprecision(2) << L"回溯请求 " << d.requestedSeconds << L"s · 保留 "
         << d.retainedSeconds << L"s · 当前可退 " << d.availableSeconds << L"s · 有效／预留 "
         << d.usedBytes / 1048576. << L"／" << d.reservedBytes / 1048576. << L" MiB";
    SetWindowTextW(controls[DebugHistoryInfo], line.str().c_str());
    std::wostringstream reason;
    if (!d.enabled)
        reason << L"回溯关闭；不记录。运行时已有预留保留到重新载入。";
    else {
        if (d.budgetLimited)
            reason << L"预算限制：按当前 " << d.recordHz << L" Hz，容量约 " << std::fixed
                   << std::setprecision(2) << d.nominalCapacitySeconds << L"s。 ";
        if (!d.frames)
            reason << L"历史为空。 ";
        else if (d.rewinding)
            reason << L"正在恢复历史；当前可退时间随游标减少。 ";
        else if (d.recordingPaused)
            reason << L"受击／玩法冻结期间不积累新历史。 ";
        else if (d.building)
            reason << (d.futureDiscarded ? L"分支截断后重新积累。 " : L"历史正在积累。 ");
        else
            reason << L"历史已达到当前窗口／容量。 ";
        if (d.windowReduced)
            reason << L"最近记录检测到窗口调小。";
    }
    SetWindowTextW(controls[DebugHistoryReason], reason.str().c_str());
    std::wostringstream cleared;
    cleared << L"最近清空：" << clearReason(d.lastClear) << L" · " << d.clears
            << L"次；本轮容量覆盖／窗口淘汰／截断未来 " << d.budgetEvictions << L"／"
            << d.windowEvictions << L"／" << d.futureDiscarded << L"帧";
    SetWindowTextW(controls[DebugHistoryClear], cleared.str().c_str());
}
void App::snapshotCompareCommand(int id) {
    if (id == SnapshotCompareEditor) {
        if (!debuggerEditor)
            showDebugger();
        if (snapshotCompareEditor && IsWindow(snapshotCompareEditor)) {
            SetForegroundWindow(snapshotCompareEditor);
            return;
        }
        snapshotCompareEditor = CreateWindowW(L"EchoEngineWindow", L"快照比对 · 只读字段诊断",
                                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                                  ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                                              70, 65, 1320, 795, window, nullptr, instance, this);
        auto label = [&](const wchar_t* text, int x, int y, int width, int controlId = 0) {
            control(L"STATIC", text, controlId, x, y, width, 25, 0, snapshotCompareEditor);
        };
        auto button = [&](const wchar_t* text, int cid, int x, int y, int width) {
            control(L"BUTTON", text, cid, x, y, width, 32, WS_TABSTOP, snapshotCompareEditor);
        };
        label(L"A 固定为独立副本；B 在点击比对时取样。比对不定位、不截断、不修改世界。", 24, 18, 1240);
        label(L"A 来源", 24, 56, 400);
        label(L"B 来源", 680, 56, 400);
        for (int cid : {SnapshotSourceA, SnapshotSourceB}) {
            auto h = control(L"COMBOBOX", L"", cid, cid == SnapshotSourceA ? 24 : 680, 87, 438, 230,
                             CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                             snapshotCompareEditor);
            for (auto text : {L"当前世界", L"调试时间轴所选帧", L"最远历史帧", L"最新历史帧"})
                SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        }
        SendMessageW(controls[SnapshotSourceA], CB_SETCURSEL, simulation->history().size() ? 2 : 0, 0);
        SendMessageW(controls[SnapshotSourceB], CB_SETCURSEL, 0, 0);
        button(L"固定 A", SnapshotPinA, 476, 85, 150);
        button(L"比对 A → B", SnapshotCompare, 1132, 85, 150);
        label(L"", 24, 138, 1240, SnapshotBaseline);
        label(L"字段筛选（包含匹配）：kino.jumpHeld / px / generation / script", 24, 180, 830);
        control(L"EDIT", L"", SnapshotFilter, 24, 214, 1010, 30, WS_BORDER | WS_TABSTOP,
                snapshotCompareEditor);
        SendMessageW(controls[SnapshotFilter], EM_SETLIMITTEXT, 192, 0);
        button(L"导出 TSV", SnapshotExport, 1050, 213, 232);
        label(L"", 24, 260, 1240, SnapshotSummary);
        label(L"字段路径", 24, 299, 475);
        label(L"A 值", 530, 299, 330);
        label(L"B 值", 902, 299, 365);
        auto rows =
            control(L"LISTBOX", L"", SnapshotRows, 24, 332, 1258, 341,
                    WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_USETABSTOPS | WS_TABSTOP,
                    snapshotCompareEditor);
        auto dc = GetDC(rows);
        auto oldFont = SelectObject(dc, font);
        // List-box tab units use the mean width of the Latin alphabet, not tmAveCharWidth
        // (the two differ for the editor's CJK font).
        SIZE alphabet{};
        constexpr wchar_t letters[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        GetTextExtentPoint32W(dc, letters, 52, &alphabet);
        SelectObject(dc, oldFont);
        ReleaseDC(rows, dc);
        int unit = std::max(1L, (alphabet.cx / 26 + 1) / 2);
        int stops[]{506 * 4 / unit, 878 * 4 / unit};
        SendMessageW(rows, LB_SETTABSTOPS, 2, reinterpret_cast<LPARAM>(stops));
        SendMessageW(rows, LB_SETHORIZONTALEXTENT, 1900, 0);
        label(L"最多显示／导出512条；筛选后重新比对可定位更多差异。枚举为编号，文本显示字节路径。", 24,
              697, 1240);
        snapshotReport = std::make_unique<SnapshotDiff>();
        snapshotB = std::make_unique<WorldState>();
        snapshotCompareCommand(SnapshotPinA);
        return;
    }
    if (!snapshotCompareEditor)
        return;
    if (id == SnapshotPinA) {
        snapshotA = std::make_unique<WorldState>(selectedState(*this, SnapshotSourceA));
        snapshotCompareCommand(SnapshotCompare);
        return;
    }
    if (id == SnapshotCompare) {
        if (!snapshotA)
            throw std::runtime_error("Pin snapshot A first");
        *snapshotB = selectedState(*this, SnapshotSourceB);
        snapshotFilterText = readFilter(controls[SnapshotFilter]);
        compareSnapshots(*snapshotA, *snapshotB, *snapshotReport, snapshotFilterText);
        SetWindowTextW(controls[SnapshotBaseline],
                       (L"A：" + stamp(*snapshotA) + L"      B：" + stamp(*snapshotB)).c_str());
        auto rows = controls[SnapshotRows];
        SendMessageW(rows, WM_SETREDRAW, FALSE, 0);
        SendMessageW(rows, LB_RESETCONTENT, 0, 0);
        for (const auto& e : snapshotReport->entries) {
            auto line =
                wide(e.field.c_str()) + L"\t" + wide(e.before.c_str()) + L"\t" + wide(e.after.c_str());
            SendMessageW(rows, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        }
        SendMessageW(rows, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(rows, nullptr, FALSE);
        std::wostringstream s;
        s << L"检查 " << snapshotReport->inspected << L" 个标量；全部差异 "
          << snapshotReport->differences << L"；筛选匹配 " << snapshotReport->matching << L"；显示 "
          << snapshotReport->entries.count
          << (snapshotReport->equal()       ? L" · 字段完全一致（忽略填充字节）"
              : snapshotReport->truncated() ? L" · 结果达到上限，请缩小筛选"
                                            : L"");
        SetWindowTextW(controls[SnapshotSummary], s.str().c_str());
        return;
    }
    if (id == SnapshotExport) {
        wchar_t path[32768] = L"snapshot-diff.tsv";
        OPENFILENAMEW f{};
        f.lStructSize = sizeof(f);
        f.hwndOwner = snapshotCompareEditor;
        f.lpstrFilter = L"TSV report\0*.tsv\0\0";
        f.lpstrFile = path;
        f.nMaxFile = std::size(path);
        f.lpstrDefExt = L"tsv";
        f.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&f))
            exportSnapshotReport(path);
    }
}
void App::exportSnapshotReport(const std::filesystem::path& path) {
    if (!snapshotA || !snapshotB || !snapshotReport)
        throw std::runtime_error("Compare two snapshots before exporting");
    std::ostringstream out;
    out << "# Echo Engine snapshot comparison\n# A tick=" << snapshotA->tick
        << " map=" << snapshotA->activeMap << " time=" << std::setprecision(17) << snapshotA->clock
        << "\n# B tick=" << snapshotB->tick << " map=" << snapshotB->activeMap
        << " time=" << snapshotB->clock << "\n# inspected=" << snapshotReport->inspected
        << " differences=" << snapshotReport->differences << " matching=" << snapshotReport->matching
        << " shown=" << snapshotReport->entries.count << " truncated=" << snapshotReport->truncated()
        << "\nfield\tbefore\tafter\n";
    for (const auto& e : snapshotReport->entries)
        out << e.field.c_str() << '\t' << e.before.c_str() << '\t' << e.after.c_str() << '\n';
    writeTextAtomically(path, out.str());
    message = "Snapshot report exported";
}
} // namespace echo
