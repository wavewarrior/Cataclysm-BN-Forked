-- Turns a fresh Play Now world into the stand-in for the macOS Bairdford save that the bnplay
-- real-binary contracts assume (tools/bnplay/*_contract.ts), then saves the world in place.
-- Run it once over a copy of the save in a direct driver session, never in an Episode (an
-- Episode's clone is thrown away): see the bnplay skill, "Tests on Windows".
--
-- What the contracts need, and how this provides it:
--   * every compass move is blocked: the eight neighbours become concrete walls. `move up` logs
--     "You can't climb here" under the roof, the blocked move with a message the time contract
--     repeats (a locked door does not do: the avatar walks through it)
--   * nothing hostile in view for a 1000-turn wait: the walls are opaque
--   * the avatar wields a pocket knife and carries a smartphone, a lighter and a plastic bottle of
--     clean water, and nothing it could wear
--   * it wears several items and has room to take any one of them off: the backpack, cargo pants
--     and army blouse each leave enough storage when another comes off
--   * it is not thirsty enough to drink without the game asking "drink anyway?"
--   * it saves with the moves it loaded with: wielding costs moves, and a save short of them makes
--     the first command after load finish a turn, so a refusal would report `time_passed`

local m = gapi.get_map()
local you = gapi.get_avatar()
local p = you:get_pos_ms()
local moves = you:get_moves()

local function at(dx, dy)
  return p + coords.tripoint_rel_ms(dx, dy, 0)
end

-- Strip the avatar and clear its tile and the ring around it.
you:drop_all_items()
local nofurn = FurnId.new("f_null"):int_id()
for dx = -1, 1 do
  for dy = -1, 1 do
    m:clear_items_at(at(dx, dy))
    m:set_furn_at(at(dx, dy), nofurn)
  end
end

local wall = TerId.new("t_concrete_wall"):int_id()
for dx = -1, 1 do
  for dy = -1, 1 do
    if dx ~= 0 or dy ~= 0 then
      m:set_ter_at(at(dx, dy), wall)
    end
  end
end

-- `count` is the charge count for a tool: a smartphone needs at least 5 for its flashlight.
local function give(id, count)
  return you:create_item(ItypeId.new(id), count or 1)
end

for _, id in ipairs({ "socks", "sneakers", "tshirt", "pants_cargo", "jacket_army", "backpack" }) do
  if not you:wear(give(id), false) then
    gdebug.log_error("bairdford_fixture: could not wear " .. id)
    return false
  end
end
if not you:wield(give("pockknife")) then
  gdebug.log_error("bairdford_fixture: could not wield the pocket knife")
  return false
end
give("smart_phone", 160)
give("lighter", 25)
give("bottle_plastic"):add_item_with_id(ItypeId.new("water_clean"), 2)
you:set_thirst(0)
you:set_moves(moves)

local saved = gdebug.save_game()
gdebug.log_info("bairdford_fixture: walled in at " .. tostring(p) .. ", moves=" .. moves ..
  ", saved=" .. tostring(saved))
return saved
