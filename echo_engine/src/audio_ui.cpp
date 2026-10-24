#include "echo/app.hpp"
#include "echo/audio_state.hpp"
#include <algorithm>
#include <stdexcept>
namespace echo {
namespace {
std::string textValue(HWND h) {
    std::wstring value(GetWindowTextLengthW(h) + 1, 0);
    GetWindowTextW(h, value.data(), int(value.size()));
    value.resize(wcslen(value.c_str()));
    int n =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}
} // namespace
void App::showAudioEditor() {
    command(Edit);
    if (audioEditor && IsWindow(audioEditor)) {
        ShowWindow(audioEditor, SW_SHOW);
        SetForegroundWindow(audioEditor);
        return;
    }
    audioEditor =
        CreateWindowW(L"EchoEngineWindow", L"BGM 与音效",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      160, 100, 700, 690, window, nullptr, instance, this);
    auto label = [&](const wchar_t* s, int y) {
        control(L"STATIC", s, 0, 20, y, 640, 23, 0, audioEditor);
    };
    auto edit = [&](int id, int x, int y, int width, const std::wstring& value) {
        control(L"EDIT", value.c_str(), id, x, y, width, 28, WS_BORDER | WS_TABSTOP, audioEditor);
    };
    auto button = [&](int id, const wchar_t* s, int x, int y, int width) {
        control(L"BUTTON", s, id, x, y, width, 30, WS_TABSTOP, audioEditor);
    };
    label(L"背景音乐 · WAV / OGG · 资源保存在作品目录", 18);
    edit(MusicPath, 20, 47, 520, wide(config.music));
    button(MusicImport, L"导入 BGM", 552, 47, 108);
    label(L"循环起点 (秒)            循环终点 (0 = 音频末尾)        渐变时长 (秒)", 87);
    edit(MusicLoopStart, 20, 114, 200, L"");
    setNumber(MusicLoopStart, config.loopStart);
    edit(MusicLoopEnd, 240, 114, 200, L"");
    setNumber(MusicLoopEnd, config.loopEnd);
    edit(MusicFadeTime, 460, 114, 200, L"");
    setNumber(MusicFadeTime, config.fadeIn > 0 ? config.fadeIn : 1);
    label(L"BGM 音量 0..1           SE 音量 0..1", 154);
    edit(MusicVolume, 20, 182, 200, L"");
    setNumber(MusicVolume, config.musicVolume);
    edit(SoundVolume, 240, 182, 200, L"");
    setNumber(SoundVolume, config.seVolume);
    button(MusicLoopEnabled, L"启用循环", 460, 182, 200);
    SendMessageW(controls[MusicLoopEnabled], BM_SETCHECK, config.musicLoop, 0);
    button(AudioApply, L"应用并保存音频设置", 20, 224, 640);
    button(MusicPreview, L"播放 / 重播", 20, 272, 120);
    button(MusicPause, L"暂停 / 继续", 150, 272, 120);
    button(MusicStop, L"停止", 280, 272, 120);
    button(MusicFadeOut, L"渐弱", 410, 272, 120);
    button(MusicFadeIn, L"渐强", 540, 272, 120);
    label(L"音效槽 · 1 跳跃 / 2 受击 / 3 R 撞击，其余供事件与 Lua 使用", 334);
    HWND slot =
        control(L"COMBOBOX", L"", SoundSlot, 20, 366, 640, 240,
                CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, audioEditor);
    for (int i = 1; i <= 32; ++i) {
        auto name = L"SE " + std::to_wstring(i);
        SendMessageW(slot, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(slot, CB_SETCURSEL, 0, 0);
    SendMessageW(slot, CB_SETITEMHEIGHT, -1, 25);
    selectedAudioSlot = 0;
    edit(SoundPath, 20, 410, 520, wide(config.effects[0]));
    button(SoundImport, L"导入 SE", 552, 410, 108);
    button(SoundPreview, L"试听当前音效", 20, 456, 640);
    label(L"循环点及渐变会进入快照；回溯恢复当时的音乐播放位置。", 518);
    label(L"音频解码在加载时完成，运行时最多混合 16 个 SE 声部。", 551);
    label(L"关闭窗口结束试听；作品内可用事件或 Lua 控制播放。", 584);
}
void App::audioCommand(int id) {
    if (id == AudioEditor) {
        showAudioEditor();
        return;
    }
    if (id == SoundSlot) {
        config.effects[selectedAudioSlot] = textValue(controls[SoundPath]);
        selectedAudioSlot = selection(SoundSlot);
        SetWindowTextW(controls[SoundPath], wide(config.effects[selectedAudioSlot]).c_str());
        return;
    }
    if (id == MusicImport || id == SoundImport) {
        auto path = choose(false, L"Audio\0*.wav;*.ogg\0\0",
                           id == MusicImport ? L"导入背景音乐" : L"导入音效", L"ogg");
        if (path.empty())
            return;
        auto name = importResource(path, "audio", AssetKind::Audio);
        SetWindowTextW(controls[id == MusicImport ? MusicPath : SoundPath], wide(name).c_str());
        message = "音频已导入，点击「应用」完成校验";
        return;
    }
    auto apply = [&] {
        auto next = config;
        next.music = textValue(controls[MusicPath]);
        next.musicEnabled = !next.music.empty();
        next.loopStart = number(MusicLoopStart);
        next.loopEnd = number(MusicLoopEnd);
        next.musicVolume = std::clamp(number(MusicVolume), 0.0f, 1.0f);
        next.seVolume = std::clamp(number(SoundVolume), 0.0f, 1.0f);
        next.fadeIn = std::clamp(number(MusicFadeTime), 0.0f, 60.0f);
        next.musicLoop = SendMessageW(controls[MusicLoopEnabled], BM_GETCHECK, 0, 0) == BST_CHECKED;
        next.effects[selectedAudioSlot] = textValue(controls[SoundPath]);
        audio.load(resources, next);
        config = std::move(next);
        audio.configure(*initial_, config);
        previewAudio = false;
        save();
        message = "音频设置已保存";
        refreshChrome();
    };
    if (id == AudioApply) {
        apply();
        return;
    }
    if (id == MusicPreview || id == SoundPreview) {
        apply();
        *audioPreview = *initial_;
        audioPreview->aster.enabled = false;
        if (id == MusicPreview)
            startMusic(*audioPreview, true);
        else {
            stopMusic(*audioPreview);
            if (!playSound(*audioPreview, selectedAudioSlot))
                throw std::runtime_error("请先选择一个已设置音效的 SE 槽位");
        }
        previewAudio = true;
        audio.pause(false);
        return;
    }
    if (!previewAudio) {
        *audioPreview = *initial_;
        previewAudio = true;
    }
    if (id == MusicPause)
        audioPreview->audio.paused = !audioPreview->audio.paused;
    if (id == MusicStop)
        stopMusic(*audioPreview);
    if (id == MusicFadeOut)
        fadeMusic(*audioPreview, 0, std::clamp(number(MusicFadeTime), 0.0f, 60.0f));
    if (id == MusicFadeIn) {
        audioPreview->audio.paused = false;
        startMusic(*audioPreview, false);
        fadeMusic(*audioPreview, config.musicVolume, std::clamp(number(MusicFadeTime), 0.0f, 60.0f));
    }
}
} // namespace echo
