-- Optional startup/event Lua overrides; all persistent values are in WorldState.
mobility.dashEnabled = true
mobility.wallJumpEnabled = true
mobility.dashSpeed = 720
mobility.waterFallSpeed = 180
-- IDs are map-local and stable, shown in the platform editor.
-- engine.configure_platform(1, {enabled=true, speed=650, delay=0.2})
-- local feature = engine.get_platform(1)
-- Lift route example (replace 6 with the active map's lift ID):
-- engine.configure_platform(6, {speed=160, travel_x=96, travel_y=-256, delay=0.6, repeat_route=true})
-- local route = engine.get_platform(6) -- progress/direction are read-only copies
-- engine.configure_platform(6, {speed=0}) -- pause route AND endpoint dwell
