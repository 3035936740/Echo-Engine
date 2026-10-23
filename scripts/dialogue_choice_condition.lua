-- Paste into a dialogue option's advanced read-only Lua condition.
-- Result is evaluated on opening the menu and stored in WorldState.
return engine.get_switch(1) and engine.get_variable(2) >= 3
