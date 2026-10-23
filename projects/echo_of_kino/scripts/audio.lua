-- Optional example. BGM and SE files / loop points are selected in the editor.
-- These APIs alter WorldState, so fades and voice age are restored by rewind.
-- Merge hooks with scripts/hooks.lua: defining the same hook replaces that hook.
function on_key_press(key)
    if key == "Down" then
        engine.fade_bgm(1.5, 0.2) -- seconds, target volume
        engine.play_se(1, 0.7, 1.0) -- slot 1..32, gain, pitch
    end
end
-- engine.play_bgm(true)   -- restart at 0
-- engine.stop_bgm()
-- music.loopStart = 2.0
-- music.loopEnd = 8.0
-- music.loop = true
