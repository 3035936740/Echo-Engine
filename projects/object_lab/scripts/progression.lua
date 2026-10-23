-- Optional startup/event example. Define items and quests in the database first.
-- The default project may not have ID 1; missing queries return nil.
local item = engine.get_item(1)
if item and item.count < item.limit then
    engine.state[10] = engine.change_item(1, 1) -- result 0 means success
end
local quest = engine.get_quest(1)
if quest and quest.status == 1 then
    engine.add_quest_progress(1, 1)
end
-- engine.set_quest_status(1, 1) -- accept/reaccept
-- Use C++ snapshot slots for cross-frame values, never a Lua closure timer.
