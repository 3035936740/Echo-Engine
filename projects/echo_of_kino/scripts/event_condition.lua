-- Copy into "Condition: Lua (advanced)" in a map/common event chain.
-- Do not add this file to project startup scripts; context is event-owned.
-- No writes/random calls here. Put state changes in the selected branch.
local ready = engine.get_variable(1) >= 3
return player.hp > 0 and ready and not engine.get_self_switch("A")
