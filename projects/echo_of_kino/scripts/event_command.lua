-- 复制到事件“执行 Lua 脚本（高级）”正文，不放入启动scripts。
-- 等待、分支、对话继续使用表单指令。
local visits = engine.get_variable(1)
engine.set_variable(1, math.min(visits + 1, 1000000))
engine.set_self_switch("A", true)
local map, common, id, page, command = engine.event_context()
engine.state[1] = engine.state[1] + 1
