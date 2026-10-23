#include "echo/app.hpp"
#include <algorithm>
#include <stdexcept>
namespace echo {
void App::showLayerEditor() {
    command(Edit);
    if (layerEditor && IsWindow(layerEditor))
        DestroyWindow(layerEditor);
    layerEditorIndex = editor.layer;
    const auto& layer = editor.level.layers[layerEditorIndex];
    layerEditor =
        CreateWindowW(L"EchoEngineWindow", L"图层图片与视差",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      180, 140, 680, 480, window, nullptr, instance, this);
    auto label = [&](const wchar_t* value, int y) {
        control(L"STATIC", value, 0, 20, y, 620, 23, 0, layerEditor);
    };
    auto edit = [&](int id, int x, int y, int width, float n) {
        control(L"EDIT", L"", id, x, y, width, 28, WS_BORDER | WS_TABSTOP, layerEditor);
        setNumber(id, n);
    };
    const wchar_t* names[] = {L"后景图层", L"世界图层", L"前景图层"};
    label(names[layerEditorIndex], 18);
    label(L"整张图片 · PNG / BMP · 可同时铺设当前图层的地砖", 48);
    control(L"EDIT", wide(layer.image.c_str()).c_str(), LayerImagePath, 20, 80, 480, 28,
            WS_BORDER | WS_TABSTOP, layerEditor);
    control(L"BUTTON", L"导入图片", LayerImageImport, 514, 80, 120, 30, WS_TABSTOP, layerEditor);
    label(L"视差系数 0..2 · 0 固定背景 / 1 跟随世界 / 0.5 远景", 124);
    edit(LayerParallax, 20, 154, 614, layer.parallax);
    label(L"左上角 X / Y（逻辑坐标）", 198);
    edit(LayerImageX, 20, 227, 296, layer.imageBounds.x);
    edit(LayerImageY, 336, 227, 298, layer.imageBounds.y);
    label(L"宽 / 高（逻辑像素；默认 1280 × 720）", 270);
    edit(LayerImageW, 20, 299, 296, layer.imageBounds.w);
    edit(LayerImageH, 336, 299, 298, layer.imageBounds.h);
    control(L"BUTTON", L"移除图层图片", LayerImageClear, 20, 358, 196, 32, WS_TABSTOP, layerEditor);
    control(L"BUTTON", L"应用并保存", LayerImageApply, 232, 358, 402, 32, WS_TABSTOP, layerEditor);
}
void App::layerCommand(int id) {
    if (id == LayerEditor) {
        showLayerEditor();
        return;
    }
    if (id == LayerImageImport) {
        auto path = choose(false, L"Layer image\0*.png;*.bmp\0\0", L"导入图层图片", L"png");
        if (path.empty())
            return;
        auto ref = importResource(path, "assets", AssetKind::Image);
        SetWindowTextW(controls[LayerImagePath], wide(ref).c_str());
        return;
    }
    if (id != LayerImageApply && id != LayerImageClear)
        return;
    std::wstring value(GetWindowTextLengthW(controls[LayerImagePath]) + 1, 0);
    GetWindowTextW(controls[LayerImagePath], value.data(), int(value.size()));
    value.resize(wcslen(value.c_str()));
    int n =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string path(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), path.data(), n, nullptr, nullptr);
    if (id == LayerImageClear)
        path.clear();
    if (path.size() >= 256)
        throw std::runtime_error("Layer image path is too long");
    auto previous = editor.level.layers[layerEditorIndex];
    editor.begin();
    auto& layer = editor.level.layers[layerEditorIndex];
    layer.image.set(path);
    layer.parallax = std::clamp(number(LayerParallax), 0.0f, 2.0f);
    layer.imageBounds = {number(LayerImageX), number(LayerImageY),
                         std::clamp(number(LayerImageW), 1.0f, 16384.0f),
                         std::clamp(number(LayerImageH), 1.0f, 16384.0f)};
    editor.end();
    editor.dirty = true;
    try {
        save();
        renderer.setResources(&resources);
    } catch (...) {
        editor.level.layers[layerEditorIndex] = previous;
        save();
        throw;
    }
    message = "Layer image and parallax saved";
    refreshChrome();
}
} // namespace echo
