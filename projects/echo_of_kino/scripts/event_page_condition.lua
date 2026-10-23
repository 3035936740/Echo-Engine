-- Copy into an event page's advanced read-only Lua condition.
-- Do not list this file among project.echo startup scripts.
-- Return exactly one boolean. All persistent state remains in WorldState.
local map, common, event, page, command = engine.event_context()
assert(command == 0)
return player.hp > 0
    and engine.get_switch(1)
    and engine.get_variable(1) >= 3
    and not engine.get_self_switch("A")
