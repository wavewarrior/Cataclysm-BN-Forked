-- Lighting-heavy scene: lights only. Run from the in-game Lua console with
--   require("lib.lightscene")
-- dofile/loadfile/load are nil'd by cata::detail::forbid_unsafe_functions
-- (src/catalua_bindings.cpp:391), so require + the lib.* searcher
-- (src/catalua_loader.cpp:111) is the only way to run a script from a file.
--
-- Deliberately no monsters here: adjacent zombies start combat immediately and
-- the resulting message popups cover the viewport, which ruins a visual check.
-- Monsters live in lib.lightmobs so the perf pass can add them separately.
--
-- Layout: a dense grid spanning the whole viewport rather than a tight cluster.
-- A small cluster fails at both ends of the zoom range -- zoomed out each pool
-- is a dot, zoomed in most of the lights sit off-screen. Spacing 4 against a
-- LIGHT_200 radius of ~14 tiles guarantees heavy pool overlap, which is the
-- point: overlapping emitters are what actually stresses the lighting path.
--
-- Lights are permanent by design. wearable_atomic_light (LIGHT_200) and
-- radiant_core (LIGHT_300) carry no ammo and never burn out, so the scene is
-- stable frame to frame and an A/B is not chasing a decaying light.
--
-- Count stays well under the 256 GPU emitter slots (src/lighting/snapshot.cpp).

local m = gapi.get_map()
local p = gapi.get_avatar():get_pos_ms()

local function at(dx, dy)
  return p + coords.tripoint_rel_ms(dx, dy, 0)
end

local lights = 0
local failed = 0
local function put(dx, dy, id)
  local ok, err = pcall(function()
    m:create_item_at(at(dx, dy), ItypeId.new(id), 1)
  end)
  if ok then
    lights = lights + 1
  else
    failed = failed + 1
    if failed <= 3 then
      gdebug.log_info("LIGHTSCENE put(" .. dx .. "," .. dy .. ") failed: " .. tostring(err))
    end
  end
end

for dx = -24, 24, 4 do
  for dy = -16, 16, 4 do
    if not (dx == 0 and dy == 0) then
      put(dx, dy, "wearable_atomic_light")
    end
  end
end

-- Brighter accents so the tonemap sees a real dynamic range, not one flat level.
for _, o in ipairs({ { 6, 6 }, { -6, -6 }, { 14, -8 }, { -14, 8 } }) do
  put(o[1], o[2], "radiant_core")
end

local report = "LIGHTSCENE_RESULT lights=" .. lights .. " failed=" .. failed ..
    " origin=" .. tostring(p)
gdebug.log_info(report)
gapi.add_msg(report)

return true
