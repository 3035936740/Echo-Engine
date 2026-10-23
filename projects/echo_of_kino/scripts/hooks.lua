-- Cross-frame state belongs to engine.state, NOT Lua closures.
function on_key_press(key)
    if key == "Space" then engine.state[1] = engine.state[1] + 1 end
end
function on_collision(bullet_id, entity)
    engine.state[2] = bullet_id
end
function on_enter_region(zone)
    engine.state[3] = zone
end
function on_state_change(from, to)
    engine.state[4] = to
end
-- Optional Lua persistence: serialize all persistent Lua values into <=4095 bytes.
-- on_snapshot/on_restore are not required for engine.state (already in WorldState).
function on_snapshot() return "" end
function on_restore(serialized) end
