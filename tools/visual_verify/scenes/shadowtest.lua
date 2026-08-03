-- Controlled wall-shadow scene.  require("lib.shadowtest")
--
-- Geometry is dictated by line of sight, and getting it wrong makes the test
-- prove nothing. A shadow cast directly BEHIND an opaque wall is not merely
-- dark, it is UNSEEN: it draws from map memory, so the shader never touches it.
-- Put the light on the far side of a wall from the player and the measurement
-- comes back empty - which is exactly what a first attempt here did (0.01%
-- changed, below the capture null).
--
-- So the light and the player sit on the SAME side of the occluder, and the
-- wedge falls across ground the player is standing on and can see:
--
--        light (-6,-6)
--             \
--              \   wall arm  y=-3, x=0..4
--        wall   +==========
--        stem   |     \
--        x=0    |      \  shadow wedge falls SE, onto lit ground
--               |       \
--            player (0,0) stands in it
--
-- The contact edge just south of the arm is the thing under test: it should be
-- near-hard where it meets the wall and widen with distance. The convex corner
-- at (0,-3) is the second case - a right angle is where a single-sample miss
-- estimate is worst, and where the smear shows up.

local m = gapi.get_map()
local p = gapi.get_avatar():get_pos_ms()

local function at(dx, dy)
  return p + coords.tripoint_rel_ms(dx, dy, 0)
end

-- Flatten a patch so nothing pre-existing occludes the test or seeds the SDF.
local floor = TerId.new("t_pavement"):int_id()
local nofurn = FurnId.new("f_null"):int_id()
local cleared = 0
for dx = -20, 20 do
  for dy = -20, 20 do
    local q = at(dx, dy)
    m:set_ter_at(q, floor)
    m:set_furn_at(q, nofurn)
    cleared = cleared + 1
  end
end

local wall = TerId.new("t_wall"):int_id()
local walls = 0
local function put_wall(dx, dy)
  m:set_ter_at(at(dx, dy), wall)
  walls = walls + 1
end

-- L-shaped occluder: a horizontal arm to cast the wedge over the player, and a
-- stem so the junction at (0,-3) is a genuine convex corner rather than an end.
for dx = 0, 4 do put_wall(dx, -3) end
for dy = -7, -4 do put_wall(0, dy) end

-- Single emitter, north-west, on the player's side of the arm.
m:create_item_at(at(-6, -6), ItypeId.new("radiant_core"), 1)

-- Self-validation. Scripted menu driving is not reliable enough on its own: a
-- dropped key in the set-time menu leaves the world in daylight and shifts
-- whole-frame luma by ~20, silently corrupting the A/B. Log the hour so a run
-- that never reached night is DISCARDED rather than measured.
local now = gapi.current_turn()
local report = "SHADOWTEST_RESULT cleared=" .. cleared .. " walls=" .. walls ..
    " hour=" .. now:hour_of_day() .. " night=" .. tostring(now:is_night()) ..
    " player=" .. tostring(p)
gdebug.log_info(report)
gapi.add_msg(report)

return true
