-- Optional startup/event sample; use a project template that exists.
-- Ordinary authoring uses the template's rider/crush checkboxes.
local slot, generation = engine.create_object(4, 512, 544)
if slot then
    engine.state[1] = slot
    engine.state[2] = generation
    engine.configure_object_movement(slot, generation, {
        rides_platforms = true, crush_kills = false
    })
end
-- Cross-frame logic belongs to C++ snapshot slots, never a Lua closure timer.
if engine.state[2] >= 1 then
    local object = engine.get_object(engine.state[1], engine.state[2])
    if object then engine.state[3] = object.standing_platform end
end
