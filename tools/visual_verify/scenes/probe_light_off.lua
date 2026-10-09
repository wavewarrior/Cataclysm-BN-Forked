-- Undoes `probe_light_on`: clears the tile two east of the avatar, which that Scene requires to
-- start empty.

local m = gapi.get_map()
local q = gapi.get_avatar():get_pos_ms() + coords.tripoint_rel_ms(2, 0, 0)

m:clear_items_at(q)
gdebug.log_info("probe_light_off: cleared " .. tostring(q))
return true
