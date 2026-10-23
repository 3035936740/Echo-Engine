#include "echo/app.hpp"
#include <commctrl.h>
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
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string fold(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
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
void checked(HRESULT h) {
    if (FAILED(h))
        throw std::runtime_error("Thumbnail decode failed");
}
HBITMAP thumbnail(Resources& r, const AssetRecord& asset) {
    if (asset.kind != AssetKind::Image)
        return nullptr;
    auto bytes = r.read(assetReference(asset.id));
    if (fold(asset.path).ends_with(".rgba")) {
        float factor = std::min(192.f / asset.width, 160.f / asset.height);
        unsigned width = std::max(1u, unsigned(asset.width * factor)),
                 height = std::max(1u, unsigned(asset.height * factor));
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -int(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels{};
        auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap)
            throw std::runtime_error("Thumbnail allocation failed");
        if (bytes.size() != 8 + std::uint64_t(asset.width) * asset.height * 4) {
            DeleteObject(bitmap);
            throw std::runtime_error("Invalid RGBA image");
        }
        auto* dst = static_cast<unsigned char*>(pixels);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                auto* src =
                    bytes.data() + 8 +
                    (std::uint64_t(y * asset.height / height) * asset.width + x * asset.width / width) *
                        4;
                auto index = (y * width + x) * 4;
                dst[index] = src[2];
                dst[index + 1] = src[1];
                dst[index + 2] = src[0];
                dst[index + 3] = src[3];
            }
        return bitmap;
    }
    Com<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_IWICImagingFactory, reinterpret_cast<void**>(factory.address())));
    Com<IWICStream> stream;
    checked(factory->CreateStream(stream.address()));
    checked(stream->InitializeFromMemory(bytes.data(), DWORD(bytes.size())));
    Com<IWICBitmapDecoder> decoder;
    checked(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnLoad,
                                             decoder.address()));
    Com<IWICBitmapFrameDecode> frame;
    checked(decoder->GetFrame(0, frame.address()));
    unsigned width = asset.width, height = asset.height;
    float factor = std::min(192.f / width, 160.f / height);
    width = std::max(1u, unsigned(width * factor));
    height = std::max(1u, unsigned(height * factor));
    Com<IWICBitmapScaler> scaler;
    checked(factory->CreateBitmapScaler(scaler.address()));
    checked(scaler->Initialize(frame.p, width, height, WICBitmapInterpolationModeNearestNeighbor));
    Com<IWICFormatConverter> converter;
    checked(factory->CreateFormatConverter(converter.address()));
    checked(converter->Initialize(scaler.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                  nullptr, 0, WICBitmapPaletteTypeCustom));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -int(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels{};
    auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap)
        throw std::runtime_error("Thumbnail bitmap allocation failed");
    try {
        checked(
            converter->CopyPixels(nullptr, width * 4, width * height * 4, static_cast<BYTE*>(pixels)));
    } catch (...) {
        DeleteObject(bitmap);
        throw;
    }
    return bitmap;
}
} // namespace
std::string App::importResource(const std::filesystem::path& path, std::string_view folder,
                                AssetKind kind) {
    auto metadata = probeAsset(path, std::string(folder) + "/" + pathUtf8(path.filename()));
    if (metadata.kind != kind)
        throw std::runtime_error("Resource format does not match this field");
    auto e = importAsset(resources, assetLibrary, path, folder);
    return assetReference(e.id);
}
void App::showAssetsEditor() {
    command(Edit);
    if (assetsEditor && IsWindow(assetsEditor)) {
        SetForegroundWindow(assetsEditor);
        return;
    }
    // Register existing files without changing their project references.
    if (!resources.contains("assets.echo")) {
        writeTextAtomically(resources.root / "assets.echo", saveAssets(assetLibrary));
        resources.setAssetCatalog(assetLibrary);
    }
    assetsEditor =
        CreateWindowW(L"EchoEngineWindow", L"资源管理 · 稳定身份与引用",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      100, 70, 1160, 805, window, nullptr, instance, this);
    auto label = [&](const wchar_t* s, int x, int y, int width) {
        control(L"STATIC", s, 0, x, y, width, 25, 0, assetsEditor);
    };
    auto edit = [&](int id, int x, int y, int width) {
        control(L"EDIT", L"", id, x, y, width, 29, WS_BORDER | WS_TABSTOP, assetsEditor);
    };
    auto button = [&](int id, const wchar_t* s, int x, int y, int width) {
        control(L"BUTTON", s, id, x, y, width, 31, WS_TABSTOP, assetsEditor);
    };
    label(L"搜索名称 / 路径 / 标签", 20, 18, 450);
    edit(AssetSearch, 20, 47, 450);
    label(L"文件夹（例如 assets/；空白显示全部）", 20, 92, 450);
    edit(AssetFolder, 20, 121, 450);
    auto type =
        control(L"COMBOBOX", L"", AssetType, 20, 166, 450, 250,
                CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, assetsEditor);
    for (const auto& s : {L"全部类型", L"图片", L"音频", L"Lua", L"着色器", L"字体", L"其他"})
        SendMessageW(type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(type, CB_SETITEMHEIGHT, -1, 25);
    SendMessageW(type, CB_SETCURSEL, 0, 0);
    control(L"LISTBOX", L"", AssetList, 20, 214, 450, 389,
            WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL | WS_TABSTOP,
            assetsEditor);
    button(AssetImport, L"导入资源", 20, 623, 210);
    button(AssetScan, L"扫描新文件", 246, 623, 224);
    label(L"缩略图与元数据", 502, 18, 622);
    control(L"STATIC", L"", AssetThumbnail, 502, 49, 206, 165, SS_BITMAP | SS_CENTERIMAGE | WS_BORDER,
            assetsEditor);
    control(L"STATIC", L"", AssetInfo, 728, 49, 396, 165, 0, assetsEditor);
    label(L"显示名称 / 标签（多选时标签批量应用）", 502, 231, 622);
    edit(AssetName, 502, 260, 280);
    edit(AssetTags, 798, 260, 326);
    label(L"项目内路径（移动 / 改名不会改变资源 ID）", 502, 307, 622);
    edit(AssetPath, 502, 336, 622);
    button(AssetApply, L"应用名称 / 标签", 502, 380, 280);
    button(AssetCopyID, L"复制稳定引用", 798, 380, 326);
    button(AssetMove, L"移动 / 改名", 502, 424, 196);
    button(AssetRelink, L"替换 / 重新关联", 714, 424, 196);
    button(AssetDelete, L"删除所选", 926, 424, 198);
    label(L"依赖位置 · 引用中的资源会阻止删除", 502, 479, 622);
    control(L"LISTBOX", L"", AssetUses, 502, 511, 622, 178,
            WS_BORDER | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, assetsEditor);
    label(L"图片导入检查尺寸与解码预算；音频检查格式和时长。", 20, 709, 1104);
    label(L"移动前保存当前工程；名称与标签不改变文件路径。", 20, 739, 1104);
    assetForm();
}
void App::assetForm(bool rebuild) {
    if (!assetsEditor)
        return;
    loadingAssets = true;
    struct Loading {
        bool& b;
        ~Loading() {
            b = false;
        }
    } loading{loadingAssets};
    if (rebuild) {
        visibleAssets.clear();
        SendMessageW(controls[AssetList], LB_RESETCONTENT, 0, 0);
        auto search = fold(field(controls[AssetSearch])), folder = fold(field(controls[AssetFolder]));
        int type = selection(AssetType) - 1;
        for (const auto& e : assetLibrary.entries) {
            if (type >= 0 && int(e.kind) != type)
                continue;
            if (!folder.empty() && !fold(e.path).starts_with(folder))
                continue;
            if (!search.empty() &&
                fold(e.name + " " + e.path + " " + e.tags).find(search) == std::string::npos)
                continue;
            visibleAssets.push_back(e.id);
            bool missing = !resources.contains(assetReference(e.id));
            auto label = wide((missing ? "[缺失] " : "") + e.name + "  ·  " + e.path);
            SendMessageW(controls[AssetList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (e.id == selectedAsset)
                SendMessageW(controls[AssetList], LB_SETSEL, TRUE, visibleAssets.size() - 1);
        }
        if (!selectedAsset && !visibleAssets.empty()) {
            selectedAsset = visibleAssets[0];
            SendMessageW(controls[AssetList], LB_SETSEL, TRUE, 0);
        }
    }
    const auto* e = findAsset(assetLibrary, selectedAsset);
    if (!e)
        return;
    SetWindowTextW(controls[AssetName], wide(e->name).c_str());
    SetWindowTextW(controls[AssetTags], wide(e->tags).c_str());
    SetWindowTextW(controls[AssetPath], wide(e->path).c_str());
    std::ostringstream info;
    info << assetReference(e->id) << "\n"
         << e->width << " × " << e->height << " px\n"
         << e->bytes / 1024.0 << " KiB\n";
    if (e->duration > 0)
        info << e->duration << " s\n";
    info << "来源: " << e->source;
    SetWindowTextW(controls[AssetInfo], wide(info.str()).c_str());
    SendMessageW(controls[AssetUses], LB_RESETCONTENT, 0, 0);
    for (const auto& use : assetUsages(resources, *e)) {
        auto line = wide(use.file + " · " + use.location);
        SendMessageW(controls[AssetUses], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    SendMessageW(controls[AssetThumbnail], STM_SETIMAGE, IMAGE_BITMAP, 0);
    if (assetThumbnail) {
        DeleteObject(assetThumbnail);
        assetThumbnail = nullptr;
    }
    if (resources.contains(assetReference(e->id)) && e->kind == AssetKind::Image) {
        try {
            assetThumbnail = thumbnail(resources, *e);
        } catch (const std::exception& error) {
            message = error.what();
        }
        SendMessageW(controls[AssetThumbnail], STM_SETIMAGE, IMAGE_BITMAP,
                     reinterpret_cast<LPARAM>(assetThumbnail));
    }
}
void App::assetCommand(int id, int notification) {
    if (id == AssetsEditor) {
        showAssetsEditor();
        return;
    }
    if (!assetsEditor || loadingAssets)
        return;
    if ((id == AssetSearch || id == AssetFolder) && notification == EN_CHANGE) {
        assetForm();
        return;
    }
    if (id == AssetType && notification == CBN_SELCHANGE) {
        assetForm();
        return;
    }
    if (id == AssetList && notification == LBN_SELCHANGE) {
        int index = int(SendMessageW(controls[AssetList], LB_GETCARETINDEX, 0, 0));
        if (index >= 0 && index < int(visibleAssets.size())) {
            selectedAsset = visibleAssets[index];
            assetForm(false);
        }
        return;
    }
    if (id == AssetScan) {
        auto next = assetLibrary;
        scanAssets(resources, next);
        writeTextAtomically(resources.root / "assets.echo", saveAssets(next));
        assetLibrary = std::move(next);
        resources.setAssetCatalog(assetLibrary);
        assetForm();
        return;
    }
    if (id == AssetImport) {
        auto path = choose(false,
                           L"Resources\0*.png;*.bmp;*.jpg;*.ogg;*.wav;*.lua;*.frag;*.vert;*.ttf;*."
                           L"otf\0All files\0*.*\0\0",
                           L"Import resource", L"png");
        if (path.empty())
            return;
        auto folder = field(controls[AssetFolder]);
        if (folder.empty())
            folder = "assets";
        auto imported = importAsset(resources, assetLibrary, path, folder);
        selectedAsset = imported.id;
        assetForm();
        return;
    }
    if (id == AssetApply) {
        auto next = assetLibrary;
        auto name = field(controls[AssetName]), tags = field(controls[AssetTags]);
        if (name.empty() || name.size() > 255 || tags.size() > 1024)
            throw std::runtime_error("Asset name or tags exceed limits");
        int n = int(SendMessageW(controls[AssetList], LB_GETSELCOUNT, 0, 0));
        std::vector<int> indexes(std::max(0, n));
        SendMessageW(controls[AssetList], LB_GETSELITEMS, indexes.size(),
                     reinterpret_cast<LPARAM>(indexes.data()));
        for (auto& e : next.entries)
            for (int index : indexes)
                if (index >= 0 && index < int(visibleAssets.size()) && e.id == visibleAssets[index]) {
                    e.tags = tags;
                    if (indexes.size() == 1)
                        e.name = name;
                }
        validateAssets(next);
        writeTextAtomically(resources.root / "assets.echo", saveAssets(next));
        assetLibrary = std::move(next);
        resources.setAssetCatalog(assetLibrary);
        assetForm();
        return;
    }
    auto* asset = findAsset(assetLibrary, selectedAsset);
    if (!asset)
        return;
    if (id == AssetCopyID) {
        auto value = wide(assetReference(asset->id));
        if (!OpenClipboard(window))
            throw std::runtime_error("Clipboard is busy");
        auto block = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
        if (!block) {
            CloseClipboard();
            throw std::runtime_error("Clipboard allocation failed");
        }
        auto data = GlobalLock(block);
        if (!data) {
            GlobalFree(block);
            CloseClipboard();
            throw std::runtime_error("Clipboard lock failed");
        }
        std::memcpy(data, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(block);
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, block))
            GlobalFree(block);
        CloseClipboard();
        return;
    }
    if (id != AssetMove && id != AssetRelink && id != AssetDelete)
        return;
    std::filesystem::path replacement;
    if (id == AssetRelink) {
        replacement = choose(false, L"Resources\0*.*\0\0", L"Replace or relink resource", L"png");
        if (replacement.empty())
            return;
    }
    auto target = field(controls[AssetPath]);
    auto chosen = selectedAsset;
    save();
    if (id == AssetMove)
        moveAsset(resources, assetLibrary, chosen, target);
    if (id == AssetRelink)
        relinkAsset(resources, assetLibrary, chosen, replacement);
    if (id == AssetDelete) {
        int count = int(SendMessageW(controls[AssetList], LB_GETSELCOUNT, 0, 0));
        std::vector<int> indexes(std::max(0, count));
        SendMessageW(controls[AssetList], LB_GETSELITEMS, indexes.size(),
                     reinterpret_cast<LPARAM>(indexes.data()));
        std::vector<std::uint64_t> ids;
        for (int index : indexes)
            if (index >= 0 && index < int(visibleAssets.size()))
                ids.push_back(visibleAssets[index]);
        if (ids.empty())
            ids.push_back(chosen);
        deleteAssets(resources, assetLibrary, ids);
    }
    loadProject(HistoryClearReason::ResourceReload);
    editing = true;
    selectedAsset = id == AssetDelete ? 0 : chosen;
    showAssetsEditor();
    message = "Asset operation completed; project references reloaded";
}
} // namespace echo
