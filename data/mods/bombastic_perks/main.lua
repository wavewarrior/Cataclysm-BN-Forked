gdebug.log_info("Bombastic Perks: Main")

local mod = game.mod_runtime[game.current_mod]
local storage = game.mod_storage[game.current_mod]
local mutations = require("bp_mutations")
local Mutation = mutations.Mutation
local PERKS = mutations.PERKS
local ALL_PERK_IDS = mutations.ALL_PERK_IDS
local STAT_BONUS_IDS = mutations.STAT_BONUS_IDS
local PERIODIC_BONUS_IDS = mutations.PERIODIC_BONUS_IDS
local KILL_MONSTER_BONUS_IDS = mutations.KILL_MONSTER_BONUS_IDS
local ACTIVATED_PERK_IDS = mutations.ACTIVATED_PERK_IDS
local CONDITIONAL_PERK_IDS = mutations.CONDITIONAL_PERK_IDS
local register_perk = mutations.register_perk
local gettext = locale.gettext
local vgettext = locale.vgettext

local function color_text(text, color) return string.format("<color_%s>%s</color>", color, text) end
local function color_good(text) return color_text(text, "light_green") end
local function color_bad(text) return color_text(text, "light_red") end
local function color_warning(text) return color_text(text, "yellow") end
local function color_info(text) return color_text(text, "light_cyan") end
local function color_highlight(text) return color_text(text, "white") end

local function create_progress_bar(current, max, width)
  width = width or 20
  local filled = math.floor((current / max) * width)
  local empty = width - filled
  local bar = color_good(string.rep("█", filled)) .. color_text(string.rep("░", empty), "dark_gray")
  local percent = math.floor((current / max) * 100)
  return string.format("[%s] %d%%", bar, percent)
end

local XP_COEFFICIENT = 2.2387
local XP_EXPONENT = 3.65
local LEVELS_PER_PERK = 3

local BOMBASTIC_PERK_MENU_ITEM_ID = ItypeId.new("bombastic_perk_menu")

mod.add_perk = function(config)
  if not config or not config.id then
    gdebug.log_error("Bombastic Perks: add_perk called without valid config or id")
    return false
  end

  if PERKS[config.id] then return false end

  local perk = Mutation.new(config)
  PERKS[config.id] = perk
  register_perk(perk)

  gdebug.log_info("Bombastic Perks: Registered new perk: " .. config.id)
  return true
end

local function get_perk_level(exp)
  local level = math.floor((exp / XP_COEFFICIENT) ^ (1 / XP_EXPONENT))
  return math.min(level, 40)
end

local function rpg_xp_needed(level)
  if level >= 40 then return XP_COEFFICIENT * (40 ^ XP_EXPONENT) end
  return XP_COEFFICIENT * (level ^ XP_EXPONENT)
end

local function get_char_value(char, key, default)
  local val = char:get_value(key)
  if val == "" then return default end
  return tonumber(val) or default
end

local function set_char_value(char, key, value) char:set_value(key, tostring(value)) end

local function check_requirements(player, perk, current_level)
  local reqs = perk.requirements
  if not reqs or (not reqs.level and not reqs.stats and not reqs.skills) then return true, {} end

  local unmet = {}

  if reqs.level and current_level < reqs.level then
    table.insert(unmet, { type = "level", label = "Lv", current = current_level, required = reqs.level })
  end

  if reqs.stats then
    local stats_map = {
      STR = player:get_str(),
      DEX = player:get_dex(),
      INT = player:get_int(),
      PER = player:get_per(),
    }
    for stat, required in pairs(reqs.stats) do
      local current = stats_map[stat]
      if current < required then
        table.insert(unmet, { type = "stat", label = stat, current = current, required = required })
      end
    end
  end

  if reqs.skills then
    for skill_name, required in pairs(reqs.skills) do
      local current = player:get_skill_level(SkillId.new(skill_name))
      if current < required then
        local display_name = skill_name:gsub("^%l", string.upper)
        table.insert(unmet, { type = "skill", label = display_name, current = current, required = required })
      end
    end
  end

  return #unmet == 0, unmet
end

local function format_requirement(label, current, required, show_current)
  local met = current >= required
  local color_fn = met and color_good or color_bad
  if show_current then
    return color_fn(string.format("%s %d/%d", label, current, required))
  else
    return color_fn(string.format("%s %d+", label, required))
  end
end

local function format_requirements_list(unmet, show_current)
  local parts = {}
  for _, req in ipairs(unmet) do
    table.insert(parts, format_requirement(req.label, req.current, req.required, show_current))
  end
  return table.concat(parts, ", ")
end

local function get_current_perks(char)
  local current = {}
  for _, perk_id in ipairs(ALL_PERK_IDS) do
    if char:has_trait(perk_id) then
      local perk = PERKS[perk_id:str()]
      if perk then
        table.insert(current, { name = perk.name, desc = perk.description })
      else
        gdebug.log_warning("Bombastic Perks: missing PERKS entry for " .. perk_id:str())
      end
    end
  end
  return current
end

local function give_perk_menu_item(char)
  if char:has_item_with_id(BOMBASTIC_PERK_MENU_ITEM_ID, true) then return end
  char:add_item_with_id(BOMBASTIC_PERK_MENU_ITEM_ID, 1)
end

mod.on_game_started = function()
  local player = gapi.get_avatar()

  set_char_value(player, "bp_level", 0)
  set_char_value(player, "bp_num_perks", 0)
  set_char_value(player, "bp_max_perks", 1)
  set_char_value(player, "bp_exp", 0)
  set_char_value(player, "bp_xp_to_perk", rpg_xp_needed(1))
  set_char_value(player, "bp_perk_points", 0)
  set_char_value(player, "bp_turn_counter", 0)
  set_char_value(player, "bp_last_kill_turn", 0)

  give_perk_menu_item(player)

  gapi.add_msg(
    MsgType.mixed,
    string.format(
      gettext("%s initialized! You now gain perks by defeating your enemies."),
      color_info(gettext("[Bombastic Perks]"))
    )
  )
end

mod.on_game_load = function()
  local player = gapi.get_avatar()

  local level = get_char_value(player, "bp_level", -1)
  if level < 0 then
    set_char_value(player, "bp_level", 0)
    set_char_value(player, "bp_num_perks", 0)
    set_char_value(player, "bp_max_perks", 1)
    set_char_value(player, "bp_exp", 0)
    set_char_value(player, "bp_xp_to_perk", rpg_xp_needed(1))
    set_char_value(player, "bp_perk_points", 0)
    set_char_value(player, "bp_turn_counter", 0)
    set_char_value(player, "bp_last_kill_turn", 0)

    gapi.add_msg(
      MsgType.mixed,
      string.format(
        gettext("%s initialized! You now gain perks by defeating your enemies."),
        color_info(gettext("[Bombastic Perks]"))
      )
    )
  end

  give_perk_menu_item(player)

  gdebug.log_info("Bombastic Perks: Loaded character at level " .. level)
end

mod.on_character_reset_stats = function(params)
  local character = params.character
  if not character then return end
  if not character:is_avatar() then return end

  local level = get_char_value(character, "bp_level", 0)

  for _, perk_id in ipairs(STAT_BONUS_IDS) do
    if character:has_trait(perk_id) then
      local perk = PERKS[perk_id:str()]
      if not perk then
        gdebug.log_warning("Bombastic Perks: missing PERKS entry for " .. perk_id:str())
      else
        local bonuses = perk.stat_bonuses
        local bonus_val = bonuses.val or 1

        if bonuses.str then character:mod_str_bonus(math.floor(level * bonuses.str * bonus_val)) end
        if bonuses.dex then character:mod_dex_bonus(math.floor(level * bonuses.dex * bonus_val)) end
        if bonuses.int then character:mod_int_bonus(math.floor(level * bonuses.int * bonus_val)) end
        if bonuses.per then character:mod_per_bonus(math.floor(level * bonuses.per * bonus_val)) end
        if bonuses.speed then character:mod_speed_bonus(math.floor(level * bonuses.speed * bonus_val)) end
      end
    end
  end

  if character:has_trait(MutationBranchId.new("perk_non_combatant")) then
    if not character:has_weapon() then
      local perk = PERKS["perk_non_combatant"]
      if perk and perk.conditional then
        local cond = perk.conditional
        character:mod_armor_bash(cond.armor_bash or 0)
        character:mod_armor_cut(cond.armor_cut or 0)
        character:mod_armor_bullet(cond.armor_bullet or 0)
      end
    end
  end
end

local function level_up(player, monster_hp, xp_gain)
  local exp = get_char_value(player, "bp_exp", 0)
  local old_level = get_char_value(player, "bp_level", 0)
  local show_messages = player:is_avatar()

  exp = exp + xp_gain
  set_char_value(player, "bp_exp", exp)

  local new_level = get_perk_level(exp)
  if new_level > old_level then
    set_char_value(player, "bp_level", new_level)

    if show_messages then
      local level_msg = color_good("★ ")
        .. color_highlight(gettext("LEVEL UP!"))
        .. color_good(" ★")
        .. " "
        .. string.format(gettext("You are now %s!"), color_info(gettext("Level") .. " " .. new_level))
      gapi.add_msg(MsgType.good, level_msg)
    end

    local old_max_perks = 1 + math.floor(old_level / LEVELS_PER_PERK)
    local new_max_perks = 1 + math.floor(new_level / LEVELS_PER_PERK)
    set_char_value(player, "bp_max_perks", new_max_perks)

    if new_max_perks > old_max_perks and show_messages then
      local perks_gained = new_max_perks - old_max_perks
      gapi.add_msg(
        MsgType.good,
        color_good(vgettext("New perk slot unlocked!", "New perk slots unlocked!", perks_gained))
          .. " "
          .. string.format(gettext("You now have %s perk slots."), color_highlight(new_max_perks))
      )
    end
  end

  local xp_needed = rpg_xp_needed(new_level + 1)
  local xp_to_next = xp_needed - exp
  set_char_value(player, "bp_xp_to_perk", xp_to_next)
end

local function apply_kill_perk_bonuses(player, monster_hp)
  local level = get_char_value(player, "bp_level", 0)

  for _, perk_id in ipairs(KILL_MONSTER_BONUS_IDS) do
    if player:has_trait(perk_id) then
      local perk = PERKS[perk_id:str()]
      if not perk then
        gdebug.log_warning("Bombastic Perks: missing PERKS entry for " .. perk_id:str())
      else
        local bonuses = perk.kill_monster_bonuses

        if bonuses.heal_percent then
          local heal_amount = math.max(1, math.floor(monster_hp * (bonuses.heal_percent * level / 100)))
          player:healall(heal_amount)
        end
      end
    end
  end
end

mod.on_monster_killed = function(params)
  local killer = params.killer
  local monster = params.mon

  if not killer or not monster then return end

  local player = killer:as_character()
  if not player then return end

  local monster_hp = monster:get_hp_max()
  local xp_gain = math.max(1, math.floor(monster_hp / 10))

  level_up(player, monster_hp, xp_gain)
  apply_kill_perk_bonuses(player, monster_hp)

  local current_turn = get_char_value(player, "bp_turn_counter", 0)
  set_char_value(player, "bp_last_kill_turn", current_turn)
end

local function handle_conditional_perks(player)
  local level = get_char_value(player, "bp_level", 0)
  if level <= 0 then return end

  for _, perk_id in ipairs(CONDITIONAL_PERK_IDS) do
    if player:has_trait(perk_id) then
      local perk = PERKS[perk_id:str()]
      if not perk or not perk.conditional then
        gdebug.log_warning("Bombastic Perks: missing PERKS entry or conditional for " .. perk_id:str())
      else
        local cond = perk.conditional

        if cond.type == "damage_buff" then
          local last_kill_turn = get_char_value(player, "bp_last_kill_turn", 0)
          local current_turn = get_char_value(player, "bp_turn_counter", 0)
          if current_turn - last_kill_turn <= cond.buff_duration then
            local bonus = cond.effect.melee_damage or 0
            player:mod_str_bonus(math.floor(level * bonus * 100))
          end
        end

        if cond.type == "dodge_buff" then
          local last_kill_turn = get_char_value(player, "bp_last_kill_turn", 0)
          local current_turn = get_char_value(player, "bp_turn_counter", 0)
          if current_turn - last_kill_turn <= cond.buff_duration then
            if cond.effect.dodge then player:mod_dex_bonus(cond.effect.dodge * level) end
            if cond.effect.dodge_skill then
              player:mod_skill_level(SkillId.new("dodge"), cond.effect.dodge_skill * level)
            end
          end
        end

        if cond.type == "downed_recovery" then
          local downed = player:get_character_state_for_group("downed")
          if downed and math.random() < cond.chance then
            player:set_value("bp_downed_check", "1")
            player:add_effect(EffectTypeId.new("downed"), 0)
          end
        end

        if cond.type == "bleed_damage_scaling" then
          local bleed_intensity = player:get_effect_int(EffectTypeId.new("bleed"), BodyPart.new("bp_torso"))
          if bleed_intensity > 0 then
            local multiplier = math.min(cond.max_multiplier, bleed_intensity * 0.05)
            player:mod_str_bonus(math.floor(level * multiplier * 100))
          end
        end
      end
    end
  end
end

mod.on_every_5_minutes = function()
  local player = gapi.get_avatar()
  if not player then return end

  local level = get_char_value(player, "bp_level", 0)
  if level <= 0 then return end

  local turn = get_char_value(player, "bp_turn_counter", 0)
  set_char_value(player, "bp_turn_counter", turn + 1)

  for _, perk_id in ipairs(PERIODIC_BONUS_IDS) do
    if player:has_trait(perk_id) then
      local perk = PERKS[perk_id:str()]
      if not perk then
        gdebug.log_warning("Bombastic Perks: missing PERKS entry for " .. perk_id:str())
      else
        local bonuses = perk.periodic_bonuses

        if bonuses.fatigue then player:mod_fatigue(math.floor(level * bonuses.fatigue)) end
        if bonuses.stamina then player:mod_stamina(math.floor(level * bonuses.stamina)) end
        if bonuses.thirst and player:get_thirst() >= 40 and math.random() > 0.75 then
          player:mod_thirst(math.floor(level * bonuses.thirst * 4))
        end
        if bonuses.rad then player:mod_rad(math.floor(level * bonuses.rad)) end
        if bonuses.healthy_mod then player:mod_healthy_mod(bonuses.healthy_mod * level, 100) end
        if bonuses.power_level then
          local power_regen = Energy.from_joule(math.floor(level * bonuses.power_level * 1000))
          player:mod_power_level(power_regen)
        end
      end
    end
  end

  handle_conditional_perks(player)
end

function grit_your_teeth(avatar, item)
  local player = avatar or gapi.get_avatar()
  if not player then return 0 end
  local pain = player:get_pain()
  local reduction = math.max(30, math.floor(pain / 2))
  player:mod_pain(-reduction)
  gapi.add_msg(MsgType.good, "You grit your teeth against the pain!")
  return 1
end

function slippery_escape(avatar, item)
  local player = avatar or gapi.get_avatar()
  if not player then return 0 end
  player:mod_pain(10)
  gapi.add_msg(MsgType.good, "You twist out of their grasp!")
  return 1
end

function popeye(avatar, item)
  local player = avatar or gapi.get_avatar()
  if not player then return 0 end
  gapi.add_msg(MsgType.good, "Your muscles swell and clench!")
  return 1
end

mod.open_perk_menu = function(params)
  local who = params.user
  if not who:is_avatar() then return 0 end

  local player = who
  local keep_open = true

  while keep_open do
    player:reset()

    local exp = get_char_value(player, "bp_exp", 0)
    local level = get_char_value(player, "bp_level", 0)
    local num_perks = get_char_value(player, "bp_num_perks", 0)
    local max_perks = get_char_value(player, "bp_max_perks", 1)
    local xp_to_next = get_char_value(player, "bp_xp_to_perk", 0)
    local current_perks = get_current_perks(player)

    local current_level_xp = rpg_xp_needed(level)
    local next_level_xp = rpg_xp_needed(level + 1)
    local xp_in_level = exp - current_level_xp
    local xp_for_level = next_level_xp - current_level_xp

    local ui = UiList.new()
    ui:title(gettext("=== [BOMBASTIC PERKS] ==="))

    local info_text = ""
    info_text = info_text .. color_highlight(gettext("Level:")) .. " " .. color_good(string.format("%d", level))
    if level < 40 then
      info_text = info_text
        .. color_text(
          string.format(" (" .. gettext("Next:") .. " %.0f " .. gettext("XP") .. ")", xp_to_next),
          "light_gray"
        )
    else
      info_text = info_text .. color_warning(" [" .. gettext("MAX") .. "]")
    end
    info_text = info_text .. "\n"

    if level > 0 and level < 40 then
      local progress = create_progress_bar(xp_in_level, xp_for_level, 30)
      info_text = info_text .. "XP: " .. progress .. "\n"
    end

    if level > 0 then
      info_text = info_text .. color_highlight(gettext("Total XP:")) .. " " .. string.format("%.0f", exp) .. "\n"
    end

    info_text = info_text
      .. color_text(
        "─────────────────────────────────────────",
        "light_gray"
      )
      .. "\n"

    info_text = info_text
      .. color_highlight(gettext("Perk Slots:"))
      .. " "
      .. color_good(string.format("%d", num_perks))
      .. color_text("/", "light_gray")
      .. color_highlight(string.format("%d", max_perks))
      .. "\n\n"

    if #current_perks > 0 then
      info_text = info_text .. color_highlight(gettext("Active Perks:")) .. "\n"
      for _, perk in ipairs(current_perks) do
        info_text = info_text .. color_good("  • " .. perk.name) .. "\n"
        info_text = info_text .. color_text("    " .. perk.desc, "light_gray") .. "\n"
      end
    else
      info_text = info_text .. color_text(gettext("  No perks selected yet."), "dark_gray") .. "\n"
    end

    ui:text(info_text)

    local menu_items = {}
    table.insert(menu_items, { text = gettext("Choose Perk"), action = "choose" })
    table.insert(menu_items, { text = gettext("Close"), action = "close" })

    for i, item_entry in ipairs(menu_items) do
      ui:add(i, item_entry.text)
    end

    local choice_index = ui:query()

    if choice_index > 0 and choice_index <= #menu_items then
      local chosen = menu_items[choice_index]

      if chosen.action == "close" then
        keep_open = false
      elseif chosen.action == "choose" then
        mod.choose_perk_menu(player)
      end
    else
      keep_open = false
    end
  end

  return 0
end

mod.choose_perk_menu = function(player)
  local level = get_char_value(player, "bp_level", 0)
  local num_perks = get_char_value(player, "bp_num_perks", 0)
  local max_perks = get_char_value(player, "bp_max_perks", 1)
  local has_available_slots = num_perks < max_perks

  local ui = UiList.new()
  ui:title(string.format(gettext("=== Select Perk (%d/%d) ==="), num_perks, max_perks))
  ui:desc_enabled(true)

  local perks = {}
  local index = 1

  for id, perk in pairs(PERKS) do
    local perk_id = perk:get_perk_id()
    local already_has = player:has_trait(perk_id)
    local can_select_reqs, unmet = check_requirements(player, perk, level)
    local can_select = has_available_slots and not already_has and can_select_reqs

    local display_name
    if already_has then
      display_name = color_text("✓ " .. perk.name, "dark_gray")
        .. color_text(" (" .. gettext("Owned") .. ")", "dark_gray")
    elseif not has_available_slots then
      display_name = color_text("◆ " .. perk.name, "dark_gray")
        .. color_text(" (" .. gettext("No slots available") .. ")", "dark_gray")
    elseif not can_select_reqs then
      display_name = color_text("◆ " .. perk.name, "dark_gray") .. " - " .. format_requirements_list(unmet, false)
    else
      display_name = color_good("◆ " .. perk.name)
    end

    table.insert(perks, {
      id = perk_id,
      name = display_name,
      desc = perk.description,
      can_select = can_select,
      index = index,
    })
    index = index + 1
  end

  table.insert(perks, {
    name = color_text(gettext("← Back"), "light_gray"),
    desc = gettext("Return to main menu"),
    action = "back",
    can_select = true,
    index = index,
  })

  for i, perk_entry in ipairs(perks) do
    ui:add_w_desc(perk_entry.index, perk_entry.name, perk_entry.desc or "")
  end

  local choice_index = ui:query()

  if choice_index > 0 and choice_index <= #perks then
    local chosen = perks[choice_index]

    if not chosen.can_select then
      gapi.add_msg(MsgType.warning, color_warning(gettext("You can't select this perk.")))
      return
    end

    if chosen.action == "back" then
      return
    elseif chosen.id then
      player:set_mutation(chosen.id)
      set_char_value(player, "bp_num_perks", num_perks + 1)
      gapi.add_msg(
        MsgType.good,
        string.format(gettext("✓ You have gained %s!"), color_highlight(chosen.id:obj():name()))
      )
    end
  end
end
