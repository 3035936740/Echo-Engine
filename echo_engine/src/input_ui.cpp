#include "echo/app.hpp"
#include <stdexcept>
namespace echo {
std::wstring App::keyName(int vk) const {
    wchar_t name[80]{};
    auto scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (vk >= VK_PRIOR && vk <= VK_DELETE)
        scan |= 0x100;
    GetKeyNameTextW(LONG(scan << 16), name, 80);
    return *name ? std::wstring(name) : L"VK " + std::to_wstring(vk);
}
void App::inputCommand(int id) {
    auto fill = [&](const KeyBindings& keys) {
        for (int i = 0; i < 8; ++i)
            SendMessageW(controls[InputKeyFirst + i], CB_SETCURSEL, keys.keys[i] - 8, 0);
        SendMessageW(controls[InputArrows], BM_SETCHECK, keys.arrowMovement, 0);
    };
    if (id == InputEditor) {
        command(Edit);
        if (inputEditor && IsWindow(inputEditor)) {
            ShowWindow(inputEditor, SW_SHOW);
            SetForegroundWindow(inputEditor);
            return;
        }
        inputEditor = CreateWindowW(L"EchoEngineWindow", L"\u4f5c\u54c1\u952e\u4f4d\u8bbe\u7f6e",
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                        ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                                    180, 100, 670, 500, window, nullptr, instance, this);
        const wchar_t* names[]{L"\u5411\u5de6\u79fb\u52a8",
                               L"\u5411\u53f3\u79fb\u52a8",
                               L"\u8df3\u8dc3",
                               L"\u4e0b\u7a7f\u5e73\u53f0",
                               L"\u4e92\u52a8 / \u5bf9\u8bdd\u7ee7\u7eed",
                               L"\u5feb\u901f\u56de\u6eaf",
                               L"\u8f68\u8ff9\u56de\u6eaf",
                               L"\u6b7b\u4ea1\u91cd\u5f00"};
        for (int i = 0; i < 8; ++i) {
            int x = 20 + (i % 2) * 315, y = 20 + (i / 2) * 78;
            control(L"STATIC", names[i], 0, x, y, 290, 22, 0, inputEditor);
            auto box = control(L"COMBOBOX", L"", InputKeyFirst + i, x, y + 26, 290, 280,
                               CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                               inputEditor);
            for (int k = 8; k <= 254; ++k) {
                auto label = keyName(k);
                SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
        }
        control(L"BUTTON", L"\u540c\u65f6\u5141\u8bb8 \u2190 / \u2192 \u79fb\u52a8", InputArrows, 20,
                337, 610, 28, BS_AUTOCHECKBOX | WS_TABSTOP, inputEditor);
        control(
            L"STATIC",
            L"\u7ee7\u7eed\u952e\u4e0e\u56de\u6eaf\u952e\u5fc5\u987b\u4e0d\u540c\u3002\u914d\u7f6e\u968f"
            L"\u9879\u76ee\u4fdd\u5b58\uff0c\u5bfc\u51fa\u4f5c\u54c1\u540c\u6837\u751f\u6548\u3002",
            0, 20, 377, 610, 24, 0, inputEditor);
        control(L"BUTTON", L"\u6062\u590d\u9ed8\u8ba4\u5230\u8868\u5355", InputReset, 20, 416, 290, 30,
                WS_TABSTOP, inputEditor);
        control(L"BUTTON", L"\u5e94\u7528\u952e\u4f4d", InputApply, 335, 416, 290, 30, WS_TABSTOP,
                inputEditor);
        fill(config.keys);
    } else if (id == InputReset) {
        fill(KeyBindings{});
    } else if (id == InputApply) {
        auto candidate = config.keys;
        for (int i = 0; i < 8; ++i)
            candidate.keys[i] = selection(InputKeyFirst + i) + 8;
        candidate.arrowMovement = SendMessageW(controls[InputArrows], BM_GETCHECK, 0, 0) == BST_CHECKED;
        validateKeys(candidate); // Validate all bindings before committing any field.
        config.keys = candidate;
        editor.dirty = true;
        message = "键位已应用，保存工程后保留";
        refreshChrome();
    }
}
} // namespace echo
