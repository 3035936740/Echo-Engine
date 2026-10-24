#include "echo/app.hpp"
#include <wincodec.h>
#include <stdexcept>
namespace echo {
namespace {
template <class T> struct Ref {
    T* p{};
    ~Ref() {
        if (p)
            p->Release();
    }
    T** out() {
        return &p;
    }
    T* operator->() {
        return p;
    }
};
void ok(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("图块调色板图片解码失败");
}
} // namespace
void App::loadTileAtlas() {
    if (tileAtlas) {
        DeleteObject(tileAtlas);
        tileAtlas = nullptr;
    }
    tileAtlasW = tileAtlasH = 0;
    if (config.tileSprite.empty())
        return;
    auto bytes = resources.read(config.tileSprite);
    Ref<IWICImagingFactory> factory;
    Ref<IWICStream> stream;
    Ref<IWICBitmapDecoder> decoder;
    Ref<IWICBitmapFrameDecode> frame;
    Ref<IWICFormatConverter> converter;
    ok(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                        reinterpret_cast<void**>(factory.out())));
    ok(factory->CreateStream(stream.out()));
    ok(stream->InitializeFromMemory(bytes.data(), DWORD(bytes.size())));
    ok(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnLoad, decoder.out()));
    ok(decoder->GetFrame(0, frame.out()));
    UINT width{}, height{};
    ok(frame->GetSize(&width, &height));
    if (!width || !height || width > 2048 || height > 2048)
        throw std::runtime_error("图块图集最大 2048 × 2048");
    ok(factory->CreateFormatConverter(converter.out()));
    ok(converter->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                             WICBitmapPaletteTypeCustom));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -int(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels{};
    auto dc = GetDC(window);
    tileAtlas = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(window, dc);
    if (!tileAtlas)
        throw std::runtime_error("图块调色板位图分配失败");
    HRESULT copied =
        converter->CopyPixels(nullptr, width * 4, width * height * 4, static_cast<BYTE*>(pixels));
    if (FAILED(copied)) {
        DeleteObject(tileAtlas);
        tileAtlas = nullptr;
        ok(copied);
    }
    auto* rgba = static_cast<BYTE*>(pixels);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            auto* p = rgba + 4 * (y * width + x);
            int background = ((x / 4 + y / 4) & 1) ? 180 : 205;
            for (int k = 0; k < 3; ++k)
                p[k] = BYTE((int(p[k]) * p[3] + background * (255 - p[3])) / 255);
            p[3] = 255;
        }
    tileAtlasW = width;
    tileAtlasH = height;
}
void App::showTilePalette() {
    command(Edit);
    if (tilePalette && IsWindow(tilePalette)) {
        SetForegroundWindow(tilePalette);
        return;
    }
    if (!tileAtlas)
        loadTileAtlas();
    tilePalette =
        CreateWindowW(L"EchoEngineWindow", L"图块调色板 · 点击图块后在地图铺设",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      250, 150, 444, 500, window, nullptr, instance, this);
    control(L"STATIC", L"选图块 → 在地图拖动铺设 · 右键擦除", 0, 20, 12, 390, 25, 0, tilePalette);
    for (int i = 0; i < 16; ++i)
        control(L"BUTTON", L"", 400 + i, 20 + (i % 4) * 98, 48 + (i / 4) * 96, 88, 86, WS_TABSTOP,
                tilePalette);
    control(L"STATIC", L"图层沿用左侧图块栏；碰撞选项在场景设置", 0, 20, 440, 390, 23, 0, tilePalette);
}
void App::tileCommand(int id) {
    if (id == TilePaletteEditor) {
        showTilePalette();
        return;
    }
    if (id >= 400 && id < 416) {
        int index = id - 400;
        if (index >= tileAtlasW / config.tileFrame * (tileAtlasH / config.tileFrame))
            return;
        editor.tileIndex = index + 1;
        if (editor.tool < 1 || editor.tool > 3)
            editor.tool = 1;
        SendMessageW(controls[TileIndex], CB_SETCURSEL, index, 0);
        if (tilePalette)
            InvalidateRect(tilePalette, nullptr, FALSE);
        for (int i = 400; i < 416; ++i)
            if (controls.contains(i))
                InvalidateRect(controls[i], nullptr, FALSE);
        refreshChrome();
        SetFocus(canvas);
    }
}
} // namespace echo
