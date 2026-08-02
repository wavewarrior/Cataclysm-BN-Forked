-- Creature load for the performance pass. Run after lib.lightscene with
--   require("lib.lightmobs")
--
-- Kept out of lib.lightscene because the visual pass needs an uncluttered
-- viewport, and combat message popups cover it.
--
-- mon_cow, not mon_zombie, and the reason is measurement validity rather than
-- taste. Zombies converging on a character who is standing still for 20 WAIT
-- turns land free hits every turn: damage/pain popups interrupt the waits and
-- the character can go down mid-run, truncating the perf window. mon_cow has
-- aggression -50 (never attacks) but morale 60, so unlike mon_rabbit
-- (aggression -99) it does not flee off-screen and stop contributing load.
-- Same sprite count, same monster-AI and pathing work, zero interference.

local p = gapi.get_avatar():get_pos_ms()

local function at(dx, dy)
  return p + coords.tripoint_rel_ms(dx, dy, 0)
end

local mons = 0
local spots = {
  { 14, 0 }, { 10, 10 }, { 0, 14 }, { -10, 10 },
  { -14, 0 }, { -10, -10 }, { 0, -14 }, { 10, -10 },
  { 18, 6 }, { -18, 6 }, { 6, -18 }, { -6, -18 },
}
for _, o in ipairs(spots) do
  local ok, res = pcall(function()
    return gapi.place_monster_at(MonsterTypeId.new("mon_cow"), at(o[1], o[2]))
  end)
  if ok and res then
    mons = mons + 1
  elseif not ok then
    gdebug.log_info("LIGHTMOBS failed: " .. tostring(res))
  end
end

local report = "LIGHTMOBS_RESULT monsters=" .. mons
gdebug.log_info(report)
gapi.add_msg(report)

return true
