#include "echo/app.hpp"
#include "echo/presets.hpp"
#include <sstream>
#include <stdexcept>
#include <wincodec.h>
namespace echo {
namespace {
std::string utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring value(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
template <class T> struct Com {
    T* p{};
    ~Com() {
        if (p)
            p->Release();
    }
    T** address() {
        return &p;
    }
    T* operator->() {
        return p;
    }
};
int choicePanelHeight(const DialogueState& d, int scale) {
    int text = std::clamp(4 * scale, 10, 18), padding = 3 * scale;
    return 3 * padding + text + 4 + 3 * scale + 2 * text + 6 + int(d.options.count) * (text + 6) + text +
           6;
}
void checked(HRESULT h) {
    if (FAILED(h))
        throw std::runtime_error("Portrait image decode failed");
}
} // namespace
void App::dialogueUi() {
    auto& w = simulation->world();
    if (!dialogWindow)
        return;
    // The hidden HWND supplies GDI layout only. Presentation belongs to Renderer.
    ShowWindow(dialogWindow, SW_HIDE);
    if (editing || w.dialogue.active < 0 || w.transition.phase != TransitionPhase::Idle) {
        renderer.hideDialogueOverlay();
        dialogueRasterKey.clear();
        return;
    }
    auto& d = activeDialogue(w);
    std::string key =
        std::string(d.text.c_str()) + '\x01' + d.speaker.c_str() + '\x01' + d.portrait.c_str() + '\x01' +
        std::to_string(w.dialogue.page) + '/' + std::to_string(int(w.dialogue.revealed)) + '/' +
        std::to_string(renderer.scale) + '/' + std::to_string(config.keys.keys[4]) + '/' +
        std::to_string(config.keys.keys[6]) + '/' + std::to_string(w.rewind.enabled) + '/' +
        std::to_string(w.dialogue.choosing) + '/' + std::to_string(w.dialogue.selected);
    for (const auto& o : w.dialogue.options)
        key += std::string(o.label.c_str()) + '/' + std::to_string(o.enabled) + '/' +
               std::to_string(o.result);
    key += '/' + std::to_string(config.keys.keys[0]) + '/' + std::to_string(config.keys.keys[1]);
    if (key == dialogueRasterKey)
        return;
    int width = 304 * renderer.scale, height = w.dialogue.choosing
                                                   ? choicePanelHeight(w.dialogue, renderer.scale)
                                                   : 46 * renderer.scale;
    if (std::uint64_t(width) * height * 4 > 64ull * 1024 * 1024)
        throw std::runtime_error("Dialogue raster exceeds visual memory budget");
    SetWindowPos(dialogWindow, nullptr, 0, 0, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels{};
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        throw std::runtime_error("Dialogue raster DC unavailable");
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        throw std::runtime_error("Dialogue raster allocation failed");
    }
    auto old = SelectObject(dc, bitmap);
    try {
        paintDialogue(dialogWindow, dc);
        GdiFlush();
        renderer.setDialogueOverlay(pixels, width, height, 8 * renderer.scale,
                                    172 * renderer.scale - height);
    } catch (...) {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        throw;
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    dialogueRasterKey = std::move(key);
}
void App::paintDialogue(HWND h, HDC supplied) {
    PAINTSTRUCT paint{};
    HDC dc = supplied ? supplied : BeginPaint(h, &paint);
    RECT r;
    GetClientRect(h, &r);
    auto& w = simulation->world();
    auto gamePanel = CreateSolidBrush(RGB(43, 48, 64));
    FillRect(dc, &r, gamePanel);
    auto pen = CreatePen(PS_SOLID, 1, RGB(91, 132, 178));
    auto oldP = SelectObject(dc, pen);
    auto oldB = SelectObject(dc, gamePanel);
    RoundRect(dc, 0, 0, r.right, r.bottom, 10, 10);
    SelectObject(dc, oldP);
    SelectObject(dc, oldB);
    DeleteObject(pen);
    if (w.dialogue.active >= 0) {
        auto& d = activeDialogue(w);
        int textHeight = std::clamp(4 * renderer.scale, 10, 18), padding = 3 * renderer.scale;
        HFONT choiceFont = nullptr;
        if (w.dialogue.choosing)
            choiceFont = CreateFontW(-textHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        std::string path(d.portrait.c_str());
        int size = std::min(132, int(r.bottom) - 24), left = 16;
        if (!path.empty()) {
            auto found = portraits.find(path);
            if (found == portraits.end()) {
                if (portraits.size() >= 8) {
                    auto first = portraits.begin();
                    DeleteObject(std::get<0>(first->second));
                    portraits.erase(first);
                }

                try {
                    auto bytes = resources.read(path);
                    Com<IWICImagingFactory> factory;
                    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                             IID_IWICImagingFactory,
                                             reinterpret_cast<void**>(factory.address())));
                    Com<IWICStream> stream;
                    checked(factory->CreateStream(stream.address()));
                    checked(stream->InitializeFromMemory(bytes.data(), DWORD(bytes.size())));
                    Com<IWICBitmapDecoder> decoder;
                    checked(factory->CreateDecoderFromStream(
                        stream.p, nullptr, WICDecodeMetadataCacheOnLoad, decoder.address()));
                    Com<IWICBitmapFrameDecode> frame;
                    checked(decoder->GetFrame(0, frame.address()));
                    UINT width{}, height{};
                    checked(frame->GetSize(&width, &height));
                    if (width > 2048 || height > 2048)
                        throw std::runtime_error("Portrait maximum size: 2048");
                    Com<IWICFormatConverter> converter;
                    checked(factory->CreateFormatConverter(converter.address()));
                    checked(converter->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA,
                                                  WICBitmapDitherTypeNone, nullptr, 0,
                                                  WICBitmapPaletteTypeCustom));
                    BITMAPINFO info{};
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth = width;
                    info.bmiHeader.biHeight = -int(height);
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biBitCount = 32;
                    void* pixels{};
                    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
                    if (!bitmap)
                        throw std::runtime_error("Portrait allocation failed");
                    checked(converter->CopyPixels(nullptr, width * 4, width * height * 4,
                                                  static_cast<BYTE*>(pixels)));
                    found = portraits.emplace(path, std::tuple{bitmap, int(width), int(height)}).first;
                } catch (...) {
                    found = portraits.emplace(path, std::tuple{HBITMAP{}, 0, 0}).first;
                }
            }
            auto [bitmap, width, height] = found->second;
            if (bitmap) {
                HDC source = CreateCompatibleDC(dc);
                auto old = SelectObject(source, bitmap);
                SetStretchBltMode(dc, COLORONCOLOR);
                // Square portraits or the first square cell of an imported sprite sheet.
                int crop = std::min(width, height);
                StretchBlt(dc, left, 12, size, size, source, 0, 0, crop, crop, SRCCOPY);
                SelectObject(source, old);
                DeleteDC(source);
            }
            left += size + 20;
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(120, 172, 230));
        auto old = SelectObject(dc, strongFont);
        auto speaker = wide(d.speaker.c_str());
        RECT heading{left, w.dialogue.choosing ? padding : 12, r.right - 24,
                     w.dialogue.choosing ? padding + textHeight + 4 : 38};
        if (choiceFont)
            SelectObject(dc, choiceFont);
        DrawTextW(dc, speaker.c_str(), -1, &heading, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, choiceFont ? choiceFont : font);
        auto text = wide(std::string(dialoguePage(d, w.dialogue.page)));
        std::size_t units = 0, characters = 0;
        while (units < text.size() && characters < std::size_t(w.dialogue.revealed)) {
            auto code = text[units++];
            if (code >= 0xd800 && code <= 0xdbff && units < text.size())
                ++units;
            ++characters;
        }
        text.resize(units);
        int choiceTop = padding + textHeight + 4 + 3 * renderer.scale;
        RECT content{left, w.dialogue.choosing ? choiceTop : 43, r.right - 24,
                     w.dialogue.choosing ? choiceTop + 2 * textHeight + 6 : r.bottom - 30};
        SetTextColor(dc, RGB(229, 235, 241));
        DrawTextW(dc, text.c_str(), -1, &content, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        if (w.dialogue.choosing) {
            int rowHeight = textHeight + 6;
            int rowTop = content.bottom;
            for (std::size_t i = 0; i < w.dialogue.options.count; ++i) {
                const auto& o = w.dialogue.options[i];
                RECT row{left, rowTop + int(i) * rowHeight, r.right - 24,
                         rowTop + (int(i) + 1) * rowHeight};
                bool selected = int(i) == w.dialogue.selected;
                if (selected) {
                    auto brush = CreateSolidBrush(RGB(61, 79, 107));
                    FillRect(dc, &row, brush);
                    DeleteObject(brush);
                }
                SetTextColor(dc, o.enabled ? RGB(229, 235, 241) : RGB(111, 123, 139));
                auto label = std::wstring(selected ? L"> " : L"  ") + wide(o.label.c_str());
                DrawTextW(dc, label.c_str(), -1, &row,
                          DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            }
        }
        SelectObject(dc, choiceFont ? choiceFont : smallFont);
        RECT hint{left, w.dialogue.choosing ? r.bottom - textHeight - padding : r.bottom - 25,
                  r.right - 24, w.dialogue.choosing ? r.bottom - padding : r.bottom - 7};
        SetTextColor(dc, RGB(137, 155, 174));
        auto hintText = keyName(config.keys.keys[4]) + L" \u7ee7\u7eed";
        if (w.dialogue.choosing)
            hintText = keyName(config.keys.keys[0]) + L" / " + keyName(config.keys.keys[1]) +
                       L" 选择 · " + hintText;
        if (w.rewind.enabled)
            hintText += L"  \u00b7  " + keyName(config.keys.keys[6]) + L" \u56de\u6eaf";
        DrawTextW(dc, hintText.c_str(), -1, &hint, DT_RIGHT | DT_SINGLELINE);
        SelectObject(dc, old);
        if (choiceFont)
            DeleteObject(choiceFont);
    }
    DeleteObject(gamePanel);
    if (!supplied)
        EndPaint(h, &paint);
}
void App::showDialogueEditor() {
    command(Edit);
    if (dialogEditor && IsWindow(dialogEditor)) {
        ShowWindow(dialogEditor, SW_SHOW);
        SetForegroundWindow(dialogEditor);
        return;
    }
    int index = editor.selectedDialogue;
    if (index < 0 || index >= int(editor.level.dialogues.count)) {
        if (!editor.level.dialogues.count) {
            editor.begin();
            DialogueDefinition d;
            d.region = {editor.level.spawn.x + 96, editor.level.spawn.y, 64, 64};
            d.speaker.set("Character");
            d.text.set("Write your dialogue here.");
            editor.level.dialogues.push(d);
            editor.end();
            editor.dirty = true;
        }
        index = 0;
        editor.selectedDialogue = 0;
    }
    auto& d = editor.level.dialogues[index];
    dialogEditor =
        CreateWindowW(L"EchoEngineWindow", L"对话与头像 · 每行一页",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      180, 120, 680, 570, window, nullptr, instance, this);
    auto title = [&](const wchar_t* s, int x, int y, int width = 600) {
        control(L"STATIC", s, 0, x, y, width, 24, 0, dialogEditor);
    };
    title(L"说话者", 20, 18);
    control(L"EDIT", wide(d.speaker.c_str()).c_str(), DialogSpeaker, 20, 45, 420, 28,
            WS_BORDER | WS_TABSTOP, dialogEditor);
    title(L"头像资源 · PNG / BMP（可留空）", 20, 88);
    control(L"EDIT", wide(d.portrait.c_str()).c_str(), DialogPortrait, 20, 115, 420, 28,
            WS_BORDER | WS_TABSTOP, dialogEditor);
    control(L"BUTTON", L"选择头像", DialogImport, 458, 115, 170, 28, WS_TABSTOP, dialogEditor);
    title(L"对话正文 · 每行一页，按配置的继续键翻页（总计最多 1023 UTF-8 字节）", 20, 159);
    control(L"EDIT", wide(d.text.c_str()).c_str(), DialogText, 20, 188, 608, 198,
            WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
            dialogEditor);
    title(L"触发区域：X / Y / 宽度 / 高度，也可在画布用「对话区域」工具绘制", 20, 401);
    int ids[] = {DialogX, DialogY, DialogW, DialogH};
    float nums[] = {d.region.x, d.region.y, d.region.w, d.region.h};
    for (int i = 0; i < 4; ++i) {
        control(L"EDIT", L"", ids[i], 20 + i * 154, 430, 145, 28, WS_BORDER | WS_TABSTOP, dialogEditor);
        setNumber(ids[i], nums[i]);
    }
    control(L"BUTTON", L"应用到对话区域", DialogApply, 20, 478, 608, 32, WS_TABSTOP, dialogEditor);
}
void App::dialogueCommand(int id) {
    if (id == DialogEditor) {
        showDialogueEditor();
        return;
    }
    if (id == DialogImport) {
        auto path = choose(false, L"Portrait image\0*.png;*.bmp\0\0", L"选择对话头像", L"png");
        if (path.empty())
            return;
        auto resource = importResource(path, "assets", AssetKind::Image);
        SetWindowTextW(controls[DialogPortrait], wide(resource).c_str());
        return;
    }
    if (id != DialogApply)
        return;
    auto speaker = utf8(value(controls[DialogSpeaker])),
         portrait = utf8(value(controls[DialogPortrait])), text = utf8(value(controls[DialogText]));
    if (speaker.size() >= 64 || portrait.size() >= 256 || text.size() >= 1024)
        throw std::runtime_error("Dialogue exceeds fixed snapshot text budget");
    if (editor.selectedDialogue < 0 || editor.selectedDialogue >= int(editor.level.dialogues.count))
        throw std::runtime_error("Select a dialogue region");
    editor.begin();
    auto& d = editor.level.dialogues[editor.selectedDialogue];
    d.speaker.set(speaker);
    d.portrait.set(portrait);
    d.text.set(text);
    d.region = {number(DialogX), number(DialogY), std::max(1.0f, number(DialogW)),
                std::max(1.0f, number(DialogH))};
    editor.end();
    editor.dirty = true;
    message = "Dialogue saved to level";
    refreshChrome();
    ShowWindow(dialogEditor, SW_HIDE);
    SetFocus(canvas);
}
} // namespace echo
