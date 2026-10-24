#include "echo/app.hpp"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <wincodec.h>
namespace echo {
namespace {
std::string field(HWND h) {
    std::wstring value(GetWindowTextLengthW(h) + 1, 0);
    GetWindowTextW(h, value.data(), int(value.size()));
    value.resize(wcslen(value.c_str()));
    int n =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}
float real(HWND h) {
    auto s = field(h);
    std::size_t used;
    float value = std::stof(s, &used);
    if (!std::isfinite(value) || s.find_first_not_of(" \t\r\n", used) != std::string::npos)
        throw std::runtime_error("请输入有效的数字");
    return value;
}
int integer(HWND h, int lo, int hi) {
    auto value = real(h);
    if (std::floor(value) != value || value < lo || value > hi)
        throw std::runtime_error("整数超出显示的范围");
    return int(value);
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
void checked(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("动画图片解码失败");
}
LRESULT CALLBACK previewProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (app && (msg == WM_PAINT || msg == WM_PRINTCLIENT)) {
        PAINTSTRUCT paint{};
        HDC dc = msg == WM_PAINT ? BeginPaint(h, &paint) : reinterpret_cast<HDC>(w);
        RECT r;
        GetClientRect(h, &r);
        app->paintAnimation(dc, r);
        if (msg == WM_PAINT)
            EndPaint(h, &paint);
        return 0;
    }
    if (msg == WM_TIMER && app && app->animationPreviewPlaying) {
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(h, msg, w, l);
}
} // namespace
void App::showAnimationsEditor() {
    command(Edit);
    if (animationsEditor && IsWindow(animationsEditor)) {
        SetForegroundWindow(animationsEditor);
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = previewProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"EchoAnimationPreview";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    animationsEditor =
        CreateWindowW(L"EchoEngineWindow", L"精灵与逐帧动画",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      50, 35, 1324, 850, window, nullptr, instance, this);
    auto label = [&](const wchar_t* s, int x, int y, int width = 250) {
        control(L"STATIC", s, 0, x, y, width, 24, 0, animationsEditor);
    };
    auto edit = [&](int id, int x, int y, int width = 110) {
        control(L"EDIT", L"", id, x, y, width, 29, WS_BORDER | WS_TABSTOP, animationsEditor);
    };
    auto button = [&](int id, const wchar_t* s, int x, int y, int width = 120, bool check = false) {
        control(L"BUTTON", s, id, x, y, width, 30, WS_TABSTOP | (check ? BS_AUTOCHECKBOX : 0),
                animationsEditor);
    };
    auto combo = [&](int id, const std::vector<std::wstring>& values, int x, int y, int width = 250) {
        auto h = control(L"COMBOBOX", L"", id, x, y, width, 250,
                         CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                         animationsEditor);
        for (const auto& v : values)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v.c_str()));
        SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
        SendMessageW(h, CB_SETCURSEL, 0, 0);
    };
    label(L"动画库 · 保存后可复用", 20, 16);
    control(L"LISTBOX", L"", AnimationList, 20, 48, 250, 218,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, animationsEditor);
    button(AnimationNew, L"新建", 20, 280, 75);
    button(AnimationCopy, L"复制", 106, 280, 75);
    button(AnimationDelete, L"删除", 192, 280, 78);
    label(L"名称（63 UTF-8 字节）", 20, 328);
    edit(AnimationName, 20, 355, 250);
    label(L"精灵表资源", 20, 399);
    edit(AnimationSprite, 20, 426, 250);
    button(AnimationImport, L"导入 / 选择精灵表", 20, 469, 250);
    label(L"单帧宽 / 高：像素", 20, 517);
    edit(AnimationWidth, 20, 544);
    edit(AnimationHeight, 159, 544);
    label(L"显示倍率（逻辑 px / 图片 px）", 20, 589);
    edit(AnimationScale, 20, 616, 110);
    button(AnimationNativePixels, L"原像素 1:1", 142, 616, 128);
    workspaceTooltip(controls[AnimationNativePixels],
                     L"1 图片像素 = 1 虚拟画布像素；仅窗口整数倍放大。会应用当前动画属性。");
    label(L"锚点 X / Y：图片 px", 20, 660);
    edit(AnimationAnchorX, 20, 687);
    edit(AnimationAnchorY, 159, 687);
    button(AnimationLoop, L"循环", 20, 731, 110, true);
    button(AnimationFlip, L"跟随朝向", 159, 731, 111, true);
    button(AnimationApply, L"应用动画属性", 20, 769, 250);
    label(L"帧列表 · 每帧独立时长", 302, 16, 400);
    control(L"LISTBOX", L"", AnimationFrames, 302, 48, 402, 190,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, animationsEditor);
    button(AnimationFrameAdd, L"插入帧", 302, 250, 92);
    button(AnimationFrameRemove, L"删除帧", 406, 250, 92);
    button(AnimationFrameUp, L"上移", 510, 250, 92);
    button(AnimationFrameDown, L"下移", 614, 250, 90);
    label(L"列 / 行（从 0 开始）/ 时长 s", 302, 296, 400);
    edit(AnimationColumn, 302, 323);
    edit(AnimationRow, 448, 323);
    edit(AnimationSeconds, 594, 323);
    label(L"帧进入音效（已配置的 SE）", 302, 366, 400);
    std::vector<std::wstring> sounds{L"不播放"};
    for (int i = 0; i < 32; ++i)
        sounds.push_back(L"SE " + std::to_wstring(i + 1));
    combo(AnimationSound, sounds, 302, 393, 402);
    label(L"攻击伤害 / 变量增量（进入本帧一次）", 302, 436, 400);
    edit(AnimationDamage, 302, 463, 190);
    edit(AnimationDelta, 514, 463, 190);
    label(L"变量标记（不设置则无玩法副作用）", 302, 504, 400);
    std::vector<std::wstring> vars{L"不修改变量"};
    for (int i = 0; i < 64; ++i)
        vars.push_back(wide(dataLabel(database, true, i)));
    combo(AnimationVariable, vars, 302, 531, 402);
    button(AnimationFrameApply, L"应用当前帧（包括右侧判定框）", 302, 581, 402);
    label(L"按行切分 · 从列 / 行开始", 302, 630, 400);
    edit(AnimationSplitColumn, 302, 657, 190);
    edit(AnimationSplitRow, 514, 657, 190);
    label(L"帧数：1–128 / 帧率：1–240", 302, 700, 400);
    edit(AnimationSplitCount, 302, 727, 190);
    edit(AnimationSplitFPS, 514, 727, 190);
    button(AnimationSplit, L"切分为帧列表（替换已有帧）", 302, 769, 402);
    label(L"即时预览 · 锚点不改变世界位置", 736, 16, 550);
    auto preview =
        CreateWindowW(L"EchoAnimationPreview", L"", WS_CHILD | WS_VISIBLE, 736, 48, 552, 280,
                      animationsEditor, reinterpret_cast<HMENU>(AnimationPreview), instance, this);
    controls[AnimationPreview] = preview;
    SetTimer(preview, 1, 33, nullptr);
    button(AnimationPreviewPlay, L"播放 / 停止", 736, 339, 266);
    button(AnimationPreviewFacing, L"翻转预览", 1022, 339, 266);
    label(L"框：逻辑 px · X / Y / 宽 / 高", 736, 383, 550);
    for (auto [base, title, y] :
         {std::tuple{AnimationCollision, L"碰撞", 410}, std::tuple{AnimationHurt, L"受击", 456},
          std::tuple{AnimationAttack, L"攻击", 502}}) {
        button(base, title, 736, y, 80, true);
        for (int i = 0; i < 4; ++i)
            edit(base + 1 + i, 826 + i * 118, y, 108);
    }
    label(L"蓝：碰撞  金：受击  红：攻击  十字：对象原点", 736, 552, 550);
    label(L"状态绑定（无脚本：状态机自动播放）", 736, 607, 550);
    std::vector<std::wstring> actors{L"玩家", L"Boss"};
    for (const auto& t : objectLibrary.templates)
        actors.push_back(wide(t.name.c_str()));
    combo(AnimationBindingActor, actors, 736, 637, 552);
    combo(AnimationBindingState, {L"待机", L"奔跑", L"跳跃", L"下落"}, 736, 683, 552);
    button(AnimationBind, L"绑定所选动画到此状态", 736, 730, 266);
    button(AnimationUnbind, L"清除此状态绑定", 1022, 730, 266);
    label(L"应用后 F5 新试玩；Ctrl+S 保存工程。预览不会触发伤害。", 736, 778, 550);
    setNumber(AnimationSplitColumn, 0);
    setNumber(AnimationSplitRow, 0);
    setNumber(AnimationSplitCount, 4);
    setNumber(AnimationSplitFPS, 10);
    animationPreviewPlaying = false;
    animationForm();
    refreshChrome();
}
void App::animationImage() {
    HBITMAP next{};
    int width{}, height{};
    if (animationClipIndex >= 0 && animationClipIndex < int(animationLibrary.clips.size())) {
        const auto& clip = animationLibrary.clips[animationClipIndex];
        auto bytes = resources.read(clip.sprite.c_str());
        auto path = resources.resolve(clip.sprite.c_str());
        for (auto& c : path)
            if (c >= 'A' && c <= 'Z')
                c += 32;
        std::vector<unsigned char> pixels;
        if (path.ends_with(".rgba")) {
            if (bytes.size() < 8)
                throw std::runtime_error("无效的 RGBA 图集");
            std::memcpy(&width, bytes.data(), 4);
            std::memcpy(&height, bytes.data() + 4, 4);
            if (width < 1 || height < 1 || width > 8192 || height > 8192 ||
                std::uint64_t(width) * height * 4 > 64ull * 1024 * 1024 ||
                bytes.size() != 8 + std::uint64_t(width) * height * 4)
                throw std::runtime_error("RGBA 图集尺寸无效");
            pixels.assign(bytes.begin() + 8, bytes.end());
            for (std::size_t i = 0; i < pixels.size(); i += 4)
                std::swap(pixels[i], pixels[i + 2]);
        } else {
            Com<IWICImagingFactory> factory;
            checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_IWICImagingFactory,
                                     reinterpret_cast<void**>(factory.address())));
            Com<IWICStream> stream;
            checked(factory->CreateStream(stream.address()));
            checked(stream->InitializeFromMemory(bytes.data(), DWORD(bytes.size())));
            Com<IWICBitmapDecoder> decoder;
            checked(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnLoad,
                                                     decoder.address()));
            Com<IWICBitmapFrameDecode> frame;
            checked(decoder->GetFrame(0, frame.address()));
            unsigned w{}, h{};
            checked(frame->GetSize(&w, &h));
            if (!w || !h || w > 8192 || h > 8192 || std::uint64_t(w) * h * 4 > 64ull * 1024 * 1024)
                throw std::runtime_error("动画图集解码后超过 64 MiB 上限");
            width = int(w);
            height = int(h);
            pixels.resize(std::size_t(w) * h * 4);
            Com<IWICFormatConverter> converter;
            checked(factory->CreateFormatConverter(converter.address()));
            checked(converter->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                          nullptr, 0, WICBitmapPaletteTypeCustom));
            checked(converter->CopyPixels(nullptr, w * 4, UINT(pixels.size()), pixels.data()));
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* dest{};
        next = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &dest, nullptr, 0);
        if (!next)
            throw std::runtime_error("动画预览内存分配失败");
        std::memcpy(dest, pixels.data(), pixels.size());
    }
    if (animationBitmap)
        DeleteObject(animationBitmap);
    animationBitmap = next;
    animationBitmapW = width;
    animationBitmapH = height;
}
void App::animationForm(bool rebuild) {
    if (!animationsEditor)
        return;
    loadingAnimations = true;
    try {
        int count = int(animationLibrary.clips.size());
        animationClipIndex = count ? std::clamp(animationClipIndex, 0, count - 1) : -1;
        if (rebuild) {
            SendMessageW(controls[AnimationList], LB_RESETCONTENT, 0, 0);
            for (const auto& clip : animationLibrary.clips)
                SendMessageW(controls[AnimationList], LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(wide(clip.name.c_str()).c_str()));
        }
        SendMessageW(controls[AnimationList], LB_SETCURSEL, animationClipIndex, 0);
        for (int id = AnimationName; id <= AnimationFrameDown; ++id)
            EnableWindow(controls[id], count != 0);
        for (int id = AnimationSplit; id <= AnimationDelta; ++id)
            EnableWindow(controls[id], count != 0);
        EnableWindow(controls[AnimationBind], count != 0);
        SendMessageW(controls[AnimationFrames], LB_RESETCONTENT, 0, 0);
        if (count) {
            const auto& clip = animationLibrary.clips[animationClipIndex];
            SetWindowTextW(controls[AnimationName], wide(clip.name.c_str()).c_str());
            SetWindowTextW(controls[AnimationSprite], wide(clip.sprite.c_str()).c_str());
            setNumber(AnimationWidth, clip.frameWidth);
            setNumber(AnimationHeight, clip.frameHeight);
            setNumber(AnimationScale, clip.scale);
            setNumber(AnimationAnchorX, clip.anchorX);
            setNumber(AnimationAnchorY, clip.anchorY);
            SendMessageW(controls[AnimationLoop], BM_SETCHECK, clip.loop, 0);
            SendMessageW(controls[AnimationFlip], BM_SETCHECK, clip.flip, 0);
            for (std::size_t i = 0; i < clip.frames.size(); ++i) {
                const auto& f = clip.frames[i];
                auto value = L"帧 " + std::to_wstring(i + 1) + L"  (" + std::to_wstring(f.column) +
                             L", " + std::to_wstring(f.row) + L")  " +
                             std::to_wstring(int(std::round(f.seconds * 1000))) + L" ms";
                if (f.attack.enabled)
                    value += L" · 攻击";
                if (f.sound >= 0)
                    value += L" · SE";
                SendMessageW(controls[AnimationFrames], LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(value.c_str()));
            }
            animationFrameIndex = std::clamp(animationFrameIndex, 0, int(clip.frames.size()) - 1);
            SendMessageW(controls[AnimationFrames], LB_SETCURSEL, animationFrameIndex, 0);
            const auto& f = clip.frames[animationFrameIndex];
            setNumber(AnimationColumn, f.column);
            setNumber(AnimationRow, f.row);
            setNumber(AnimationSeconds, f.seconds);
            setNumber(AnimationDamage, f.damage);
            setNumber(AnimationDelta, f.delta);
            SendMessageW(controls[AnimationSound], CB_SETCURSEL, f.sound + 1, 0);
            SendMessageW(controls[AnimationVariable], CB_SETCURSEL, f.variable + 1, 0);
            for (auto [id, b] :
                 {std::pair{AnimationCollision, f.collision}, std::pair{AnimationHurt, f.hurt},
                  std::pair{AnimationAttack, f.attack}}) {
                SendMessageW(controls[id], BM_SETCHECK, b.enabled, 0);
                setNumber(id + 1, b.x);
                setNumber(id + 2, b.y);
                setNumber(id + 3, b.w);
                setNumber(id + 4, b.h);
            }
        } else {
            SetWindowTextW(controls[AnimationName], L"");
            SetWindowTextW(controls[AnimationSprite], L"");
        }
        animationImage();
        InvalidateRect(controls[AnimationPreview], nullptr, FALSE);
    } catch (...) {
        loadingAnimations = false;
        throw;
    }
    loadingAnimations = false;
}
void App::paintAnimation(HDC dc, RECT r) {
    int width = r.right - r.left, height = r.bottom - r.top;
    if (width < 1 || height < 1 || width > 1024 || height > 1024)
        return;
    std::vector<std::uint32_t> pixels(std::size_t(width) * height);
    auto light = darkTheme ? 0x273346u : 0xe9edf4u, dark = darkTheme ? 0x222c3du : 0xdce3eeu;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixels[y * width + x] = ((x / 16 + y / 16) & 1) ? light : dark;
    float originX = width * .5f, originY = height * .5f, ratio = 1;
    float bodyWidth = playerWidth;
    int actor = selection(AnimationBindingActor);
    if (actor == 1)
        bodyWidth = 48;
    else if (actor >= 2 && actor - 2 < int(objectLibrary.templates.size()))
        bodyWidth = objectLibrary.templates[actor - 2].width;
    const AnimationFrame* frame{};
    const AnimationClip* clip{};
    if (animationClipIndex >= 0 && animationClipIndex < int(animationLibrary.clips.size()) &&
        animationBitmap) {
        clip = &animationLibrary.clips[animationClipIndex];
        int index = std::clamp(animationFrameIndex, 0, int(clip->frames.size()) - 1);
        if (animationPreviewPlaying) {
            double total = 0;
            for (const auto& f : clip->frames)
                total += f.seconds;
            double elapsed = (GetTickCount64() - animationPreviewStarted) / 1000.0;
            if (clip->loop)
                elapsed = std::fmod(elapsed, total);
            index = 0;
            while (index + 1 < int(clip->frames.size()) && elapsed >= clip->frames[index].seconds) {
                elapsed -= clip->frames[index].seconds;
                ++index;
            }
        }
        frame = &clip->frames[index];
        bool flip = animationPreviewFacing && clip->flip;
        float anchor = flip ? clip->frameWidth - clip->anchorX : clip->anchorX;
        float x0 = (flip ? bodyWidth / clip->scale : 0) - anchor, x1 = x0 + clip->frameWidth;
        float y0 = -clip->anchorY, y1 = y0 + clip->frameHeight;
        for (const auto box : {frame->collision, frame->hurt, frame->attack}) {
            if (!box.enabled)
                continue;
            auto b = animationBox(box, {0, 0}, bodyWidth, animationPreviewFacing ? -1 : 1);
            x0 = std::min(x0, b.x / clip->scale);
            x1 = std::max(x1, (b.x + b.w) / clip->scale);
            y0 = std::min(y0, b.y / clip->scale);
            y1 = std::max(y1, (b.y + b.h) / clip->scale);
        }
        float zoom = std::min({4.f, (width * .5f - 16) / std::max({1.f, std::abs(x0), std::abs(x1)}),
                               (height * .5f - 16) / std::max({1.f, std::abs(y0), std::abs(y1)})});
        zoom =
            std::max(1.f, std::floor(zoom)); // Preview preserves source texels; oversized frames clip.
        ratio = zoom / clip->scale;
        int left = int(std::floor(originX + (flip ? bodyWidth * ratio : 0) - anchor * zoom));
        int top = int(std::floor(originY - clip->anchorY * zoom));
        int drawW = std::max(1, int(clip->frameWidth * zoom)),
            drawH = std::max(1, int(clip->frameHeight * zoom));
        BITMAP bitmap{};
        GetObjectW(animationBitmap, sizeof(bitmap), &bitmap);
        auto* src = static_cast<const unsigned char*>(bitmap.bmBits);
        for (int y = std::max(0, top); y < std::min(height, top + drawH); ++y)
            for (int x = std::max(0, left); x < std::min(width, left + drawW); ++x) {
                int sx = (x - left) * clip->frameWidth / drawW,
                    sy = (y - top) * clip->frameHeight / drawH;
                if (flip)
                    sx = clip->frameWidth - 1 - sx;
                sx += frame->column * clip->frameWidth;
                sy += frame->row * clip->frameHeight;
                if (sx < 0 || sx >= animationBitmapW || sy < 0 || sy >= animationBitmapH)
                    continue;
                auto* p = src + (std::size_t(sy) * animationBitmapW + sx) * 4;
                unsigned background = pixels[y * width + x], result = 0;
                for (int i = 0; i < 3; ++i)
                    result |=
                        ((p[i] * p[3] + ((background >> (i * 8)) & 255) * (255 - p[3]) + 127) / 255)
                        << (i * 8);
                pixels[y * width + x] = result;
            }
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, width, height, 0, 0, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
    if (frame) {
        for (auto [b, color] :
             {std::pair{frame->collision, RGB(76, 143, 221)}, std::pair{frame->hurt, RGB(211, 162, 56)},
              std::pair{frame->attack, RGB(220, 96, 107)}}) {
            if (!b.enabled)
                continue;
            auto box = animationBox(b, {0, 0}, bodyWidth, animationPreviewFacing ? -1 : 1);
            auto pen = CreatePen(PS_SOLID, 2, color);
            auto old = SelectObject(dc, pen);
            auto brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, int(originX + box.x * ratio), int(originY + box.y * ratio),
                      int(originX + (box.x + box.w) * ratio), int(originY + (box.y + box.h) * ratio));
            SelectObject(dc, brush);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }
    auto pen = CreatePen(PS_SOLID, 1, darkTheme ? RGB(237, 242, 251) : RGB(64, 78, 102));
    auto old = SelectObject(dc, pen);
    MoveToEx(dc, int(originX) - 6, int(originY), nullptr);
    LineTo(dc, int(originX) + 7, int(originY));
    MoveToEx(dc, int(originX), int(originY) - 6, nullptr);
    LineTo(dc, int(originX), int(originY) + 7);
    SelectObject(dc, old);
    DeleteObject(pen);
}
void App::animationCommand(int id, int notification) {
    if (id == AnimationsEditor) {
        showAnimationsEditor();
        return;
    }
    if (!animationsEditor || loadingAnimations)
        return;
    if (id == AnimationList) {
        if (notification != LBN_SELCHANGE)
            return;
        animationClipIndex = int(SendMessageW(controls[id], LB_GETCURSEL, 0, 0));
        animationFrameIndex = 0;
        animationPreviewPlaying = false;
        animationForm(false);
        return;
    }
    if (id == AnimationFrames) {
        if (notification != LBN_SELCHANGE)
            return;
        animationFrameIndex = int(SendMessageW(controls[id], LB_GETCURSEL, 0, 0));
        animationPreviewPlaying = false;
        animationForm(false);
        return;
    }
    if (id == AnimationPreviewPlay) {
        animationPreviewPlaying = !animationPreviewPlaying;
        animationPreviewStarted = GetTickCount64();
        InvalidateRect(controls[AnimationPreview], nullptr, FALSE);
        return;
    }
    if (id == AnimationPreviewFacing) {
        animationPreviewFacing = !animationPreviewFacing;
        InvalidateRect(controls[AnimationPreview], nullptr, FALSE);
        return;
    }
    if (id == AnimationBindingActor) {
        if (notification != CBN_SELCHANGE)
            return;
        int actor = selection(id), kind = actor;
        if (actor >= 2 && actor - 2 < int(objectLibrary.templates.size())) {
            auto objectKind = objectLibrary.templates[actor - 2].kind;
            kind = objectKind == ObjectKind::Player ? 0 : objectKind == ObjectKind::Boss ? 1 : 2;
        }
        std::vector<std::wstring> states =
            kind == 0 ? std::vector<std::wstring>{L"待机", L"奔跑", L"跳跃", L"下落"}
            : kind == 1
                ? std::vector<std::wstring>{L"待机", L"行走", L"施法"}
                : std::vector<std::wstring>{L"待机", L"巡逻", L"追击", L"攻击", L"返回", L"死亡"};
        SendMessageW(controls[AnimationBindingState], CB_RESETCONTENT, 0, 0);
        for (const auto& s : states)
            SendMessageW(controls[AnimationBindingState], CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(s.c_str()));
        SendMessageW(controls[AnimationBindingState], CB_SETCURSEL, 0, 0);
        return;
    }
    if (id != AnimationNew && id != AnimationCopy && id != AnimationDelete && id != AnimationApply &&
        id != AnimationImport && id != AnimationFrameApply && id != AnimationFrameAdd &&
        id != AnimationFrameRemove && id != AnimationFrameUp && id != AnimationFrameDown &&
        id != AnimationSplit && id != AnimationBind && id != AnimationUnbind)
        return;
    command(Edit);
    auto next = animationLibrary;
    int index = animationClipIndex, frameIndex = animationFrameIndex;
    bool has = index >= 0 && index < int(next.clips.size());
    if (id == AnimationNew || id == AnimationCopy) {
        AnimationClip clip;
        if (id == AnimationCopy && has)
            clip = next.clips[index];
        else {
            clip.sprite.set(config.player.path);
            clip.frameWidth = clip.frameHeight = config.player.frame;
            clip.scale = config.player.width / clip.frameWidth;
            clip.anchorX = -config.player.offsetX / clip.scale;
            clip.anchorY = -config.player.offsetY / clip.scale;
            clip.frames.push_back({});
        }
        clip.id = allocateAnimationId(next);
        clip.name.set("新动画");
        next.clips.push_back(std::move(clip));
        index = int(next.clips.size()) - 1;
        frameIndex = 0;
    } else if (id == AnimationDelete && has) {
        auto deleted = next.clips[index].id;
        std::string uses;
        for (int i = 0; i < 4; ++i)
            if (next.player[i] == deleted)
                uses += "Player state " + std::to_string(i) + "\n";
        for (int i = 0; i < 3; ++i)
            if (next.boss[i] == deleted)
                uses += "Boss state " + std::to_string(i) + "\n";
        for (const auto& b : next.bindings)
            for (int i = 0; i < 6; ++i)
                if (b.clips[i] == deleted)
                    uses += "Template " + std::to_string(b.templateId) + " state " + std::to_string(i) +
                            "\n";
        if (!uses.empty())
            throw std::runtime_error("删除前请先解除以下绑定：\n" + uses);
        next.clips.erase(next.clips.begin() + index);
        frameIndex = 0;
    } else if (id == AnimationBind || id == AnimationUnbind) {
        if (!has && id == AnimationBind)
            return;
        auto clipId = id == AnimationBind ? next.clips[index].id : 0;
        int actor = selection(AnimationBindingActor), state = selection(AnimationBindingState);
        if (actor < 0 || state < 0 || state >= 6)
            throw std::runtime_error("请选择角色和状态");
        if (actor == 0) {
            if (state >= 4)
                throw std::runtime_error("无效的玩家状态");
            next.player[state] = clipId;
        } else if (actor == 1) {
            if (state >= 3)
                throw std::runtime_error("无效的 Boss 状态");
            next.boss[state] = clipId;
        } else {
            if (actor - 2 >= int(objectLibrary.templates.size()))
                throw std::runtime_error("缺少对象模板");
            auto templateId = objectLibrary.templates[actor - 2].id;
            auto it = std::find_if(next.bindings.begin(), next.bindings.end(),
                                   [&](const auto& b) { return b.templateId == templateId; });
            if (it == next.bindings.end()) {
                next.bindings.push_back({templateId, {}});
                it = next.bindings.end() - 1;
            }
            it->clips[state] = clipId;
            if (std::all_of(it->clips.begin(), it->clips.end(), [](auto v) { return v == 0; }))
                next.bindings.erase(it);
        }
    } else if (!has)
        return;
    else {
        auto& clip = next.clips[index];
        if (id == AnimationApply) {
            auto name = field(controls[AnimationName]), sprite = field(controls[AnimationSprite]);
            if (name.empty() || name.size() >= 64 || sprite.empty() || sprite.size() >= 256)
                throw std::runtime_error("动画名称或精灵引用为空或过长");
            clip.name.set(name);
            clip.sprite.set(sprite);
            clip.frameWidth = integer(controls[AnimationWidth], 1, 4096);
            clip.frameHeight = integer(controls[AnimationHeight], 1, 4096);
            clip.scale = real(controls[AnimationScale]);
            clip.anchorX = real(controls[AnimationAnchorX]);
            clip.anchorY = real(controls[AnimationAnchorY]);
            clip.loop = SendMessageW(controls[AnimationLoop], BM_GETCHECK, 0, 0) == BST_CHECKED;
            clip.flip = SendMessageW(controls[AnimationFlip], BM_GETCHECK, 0, 0) == BST_CHECKED;
        } else if (id == AnimationImport) {
            auto path = choose(false, L"Images\0*.png;*.bmp;*.jpg;*.jpeg;*.gif;*.rgba\0\0",
                               L"选择精灵表", L"png");
            if (path.empty())
                return;
            clip.sprite.set(importResource(path, "assets", AssetKind::Image));
        } else if (id == AnimationSplit) {
            int startColumn = integer(controls[AnimationSplitColumn], 0, 8191);
            int startRow = integer(controls[AnimationSplitRow], 0, 8191);
            int count = integer(controls[AnimationSplitCount], 1, 128);
            float fps = real(controls[AnimationSplitFPS]);
            if (fps < 1 || fps > 240 || !animationBitmap || animationBitmapW < clip.frameWidth)
                throw std::runtime_error("需要有效的图集，帧率需为 1–240");
            int columns = animationBitmapW / clip.frameWidth;
            if (startColumn >= columns)
                throw std::runtime_error("切分起始列超出了图集范围");
            clip.frames.clear();
            for (int i = 0; i < count; ++i) {
                AnimationFrame f;
                int cell = startRow * columns + startColumn + i;
                f.column = cell % columns;
                f.row = cell / columns;
                f.seconds = 1 / fps;
                clip.frames.push_back(f);
            }
            frameIndex = 0;
        } else {
            frameIndex = std::clamp(frameIndex, 0, int(clip.frames.size()) - 1);
            if (id == AnimationFrameApply) {
                auto& f = clip.frames[frameIndex];
                f.column = integer(controls[AnimationColumn], 0, 8191);
                f.row = integer(controls[AnimationRow], 0, 8191);
                f.seconds = real(controls[AnimationSeconds]);
                f.damage = integer(controls[AnimationDamage], 0, 1000000);
                f.delta = integer(controls[AnimationDelta], -1000000, 1000000);
                f.sound = selection(AnimationSound) - 1;
                f.variable = selection(AnimationVariable) - 1;
                auto read = [&](int base) {
                    return AnimationBox{real(controls[base + 1]), real(controls[base + 2]),
                                        real(controls[base + 3]), real(controls[base + 4]),
                                        SendMessageW(controls[base], BM_GETCHECK, 0, 0) == BST_CHECKED};
                };
                f.collision = read(AnimationCollision);
                f.hurt = read(AnimationHurt);
                f.attack = read(AnimationAttack);
            } else if (id == AnimationFrameAdd) {
                auto frame = clip.frames[frameIndex];
                clip.frames.insert(clip.frames.begin() + frameIndex + 1, frame);
                ++frameIndex;
            } else if (id == AnimationFrameRemove) {
                if (clip.frames.size() == 1)
                    throw std::runtime_error("至少保留一帧动画");
                clip.frames.erase(clip.frames.begin() + frameIndex);
            } else if (id == AnimationFrameUp && frameIndex > 0) {
                std::swap(clip.frames[frameIndex], clip.frames[frameIndex - 1]);
                --frameIndex;
            } else if (id == AnimationFrameDown && frameIndex + 1 < int(clip.frames.size())) {
                std::swap(clip.frames[frameIndex], clip.frames[frameIndex + 1]);
                ++frameIndex;
            }
        }
    }
    validateAnimations(next);
    renderer.setAnimations(
        next); // Stage all textures and frame bounds before replacing the working definition.
    animationLibrary = std::move(next);
    animationClipIndex = index;
    animationFrameIndex = frameIndex;
    animationPreviewPlaying = false;
    editor.dirty = true;
    animationForm();
    message = "动画已应用，F5 试玩将使用新时间线";
    refreshChrome();
}
} // namespace echo
