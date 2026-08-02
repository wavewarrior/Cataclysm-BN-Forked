-- Isolation test: exactly ONE light, nothing else.
--   require("lib.lightone")
-- The dense scene made it impossible to tell my own item sprites apart from a
-- suspected rendering artefact. One light on open ground answers it directly:
-- if a dotted ring appears around THIS light, the ring is light-related; if it
-- does not, the rings seen earlier belong to something else entirely.

local m = gapi.get_map()
local p = gapi.get_avatar():get_pos_ms()
local q = p + coords.tripoint_rel_ms(12, 0, 0)

m:create_item_at(q, ItypeId.new("radiant_core"), 1)

local report = "LIGHTONE_RESULT player=" .. tostring(p) .. " light=" .. tostring(q)
gdebug.log_info(report)
gapi.add_msg(report)

return true
