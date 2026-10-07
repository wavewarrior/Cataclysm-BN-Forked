-- Reversible A/B toggle for capture oracles: one light two tiles east of the avatar.
-- `probe_light_off` removes it again. The tile must start empty, so that clearing it restores the
-- original scene exactly (a triplet oracle's "restored" must match "original").

local m = gapi.get_map()
local q = gapi.get_avatar():get_pos_ms() + coords.tripoint_rel_ms(2, 0, 0)

if #m:get_items_at(q) > 0 then
  gdebug.log_error("probe_light_on: the tile east of the avatar is not empty " .. tostring(q))
  return false
end
m:create_item_at(q, ItypeId.new("radiant_core"), 1)
gdebug.log_info("probe_light_on: light at " .. tostring(q))
return true
