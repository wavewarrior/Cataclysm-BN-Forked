gdebug.log_info("Bombastic Perks: Loading Mutations")

local Mutation = {}
Mutation.__index = Mutation

function Mutation.new(config)
  local self = setmetatable({}, Mutation)
  self.id = config.id
  self.name = config.name or config.id
  self.description = config.description or ""
  self.requirements = config.requirements or {}
  self.stat_bonuses = config.stat_bonuses or {}
  self.periodic_bonuses = config.periodic_bonuses or {}
  self.kill_monster_bonuses = config.kill_monster_bonuses or {}
  self.activated = config.activated or false
  self.iuse_function = config.iuse_function or nil
  return self
end

function Mutation:get_perk_id() return MutationBranchId.new(self.id) end

local PERKS = {
  PERK_STR_UP = Mutation.new({
    id = "perk_STR_UP",
    name = "Stronger",
    description = "Have you been working out? +1 Strength per level.",
    requirements = { stats = { STR = 6 } },
    stat_bonuses = { str = 1, val = 1 },
  }),

  PERK_STR_UP_2 = Mutation.new({
    id = "perk_STR_UP_2",
    name = "Even Stronger",
    description = "You HAVE been working out. +2 Strength per level.",
    requirements = { level = 5, stats = { STR = 8 } },
    stat_bonuses = { str = 2, val = 1 },
  }),

  PERK_DEX_UP = Mutation.new({
    id = "perk_DEX_UP",
    name = "Faster",
    description = "Have you been doing cardio? +1 Dexterity per level.",
    requirements = { stats = { DEX = 6 } },
    stat_bonuses = { dex = 1, val = 1 },
  }),

  PERK_DEX_UP_2 = Mutation.new({
    id = "perk_DEX_UP_2",
    name = "Even Faster",
    description = "You HAVE been doing cardio. +2 Dexterity per level.",
    requirements = { level = 5, stats = { DEX = 8 } },
    stat_bonuses = { dex = 2, val = 1 },
  }),

  PERK_INT_UP = Mutation.new({
    id = "perk_INT_UP",
    name = "Smarter",
    description = "Have you been reading? +1 Intelligence per level.",
    requirements = { stats = { INT = 6 } },
    stat_bonuses = { int = 1, val = 1 },
  }),

  PERK_INT_UP_2 = Mutation.new({
    id = "perk_INT_UP_2",
    name = "Even Smarter",
    description = "You HAVE been reading. +2 Intelligence per level.",
    requirements = { level = 5, stats = { INT = 8 } },
    stat_bonuses = { int = 2, val = 1 },
  }),

  PERK_PER_UP = Mutation.new({
    id = "perk_PER_UP",
    name = "Sharper",
    description = "Have you been staring out into the middle distance? +1 Perception per level.",
    requirements = { stats = { PER = 6 } },
    stat_bonuses = { per = 1, val = 1 },
  }),

  PERK_PER_UP_2 = Mutation.new({
    id = "perk_PER_UP_2",
    name = "Even Sharper",
    description = "You HAVE been staring out into the middle distance. +2 Perception per level.",
    requirements = { level = 5, stats = { PER = 8 } },
    stat_bonuses = { per = 2, val = 1 },
  }),

  PERK_MIGHTY_THEWS = Mutation.new({
    id = "perk_mighty_thews",
    name = "Mighty Thews",
    description = "Your rippling muscles enable you to split the skulls of your enemies. +5% to Lifting and Grip.",
    requirements = { stats = { STR = 8 } },
    stat_bonuses = { str = 0.5, val = 1 },
  }),

  PERK_PANTHERISH_GRACE = Mutation.new({
    id = "perk_pantherish_grace",
    name = "Pantherish Grace",
    description = "Your movements bring to mind a multitude of cat-based metaphors. +5% to Balance and Footing.",
    requirements = { stats = { DEX = 8 } },
    stat_bonuses = { dex = 0.5, val = 1 },
  }),

  PERK_EAGLE_EYES = Mutation.new({
    id = "perk_eagle_eyes",
    name = "Eagle Eyes",
    description = "Your sharp gazes misses little. +25% to Vision and Night Vision.",
    requirements = { stats = { PER = 8 } },
    stat_bonuses = { per = 0.5, val = 1 },
  }),

  PERK_THICK_SKULL = Mutation.new({
    id = "perk_thick_skull",
    name = "Thick Skull",
    description = "You've taken so many blows to the head you've lost count. Additional skull armor.",
    requirements = { level = 3 },
  }),

  PERK_BUILT_TOUGH = Mutation.new({
    id = "perk_built_tough",
    name = "Built Tough",
    description = "You're like a walking tank! Take 5% less damage from all sources.",
    requirements = { level = 5, stats = { STR = 10 } },
    stat_bonuses = {},
  }),

  PERK_HAULER = Mutation.new({
    id = "perk_hauler",
    name = "Hauler",
    description = "All that hauling has paid off. You can carry 10kg of additional items.",
    requirements = { stats = { STR = 8 } },
    stat_bonuses = {},
  }),

  PERK_VACATIONER = Mutation.new({
    id = "perk_vacationer",
    name = "Vacationer",
    description = "Every day in the apocalypse is just like the beach. You are more comfortable in cold and warm weather.",
    requirements = { level = 3 },
    stat_bonuses = {},
  }),

  PERK_GYM_RAT = Mutation.new({
    id = "perk_gym_rat",
    name = "Gym Rat",
    description = "Your time exercising has gotten you used to being drenched in your own sweat. Being wet doesn't bother you as much.",
    requirements = { level = 3 },
    stat_bonuses = {},
  }),

  PERK_QUICKDRAW = Mutation.new({
    id = "perk_quickdraw",
    name = "Quickdraw",
    description = "Practicing with handguns has improved your draw speed with pretty much everything. Retrieve objects 25% faster from containers.",
    requirements = { stats = { DEX = 8 } },
    stat_bonuses = {},
  }),

  PERK_ANIMAL_FRIEND = Mutation.new({
    id = "perk_animal_friend",
    name = "Animal Friend",
    description = "Even with everything that's happened, you still find solace in nature. Natural animals are less likely to attack you.",
    requirements = { level = 5 },
    stat_bonuses = {},
  }),

  PERK_VENGEFUL = Mutation.new({
    id = "perk_vengeful",
    name = "Vengeful",
    description = "Oh, they're not getting away with THAT. You do +15% damage for two turns after you're hit.",
    requirements = { level = 5 },
    stat_bonuses = {},
  }),

  PERK_TWICE_SHY = Mutation.new({
    id = "perk_twice_shy",
    name = "Twice Shy",
    description = "Pain is really painful; best to avoid it. For five turns after you are hit, you gain +1 bonus dodge and +2 dodge skill.",
    requirements = { level = 5, stats = { DEX = 8 } },
    stat_bonuses = {},
  }),

  PERK_GRIT_YOUR_TEETH = Mutation.new({
    id = "perk_grit_your_teeth",
    name = "Grit Your Teeth",
    description = "When you have to you can wipe the blood off your chin, grit your teeth, and get it done. Use to reduce your pain by 30 or half (whichever is greater) for 3 minutes.",
    requirements = { level = 3 },
    activated = true,
    iuse_function = "grit_your_teeth",
  }),

  PERK_SECOND_WIND = Mutation.new({
    id = "perk_second_wind",
    name = "Second Wind",
    description = "Anyone can pause and catch their breath but you're extremely good at doing so. You can regenerate one-third of your stamina over ten seconds, once every fifteen minutes.",
    requirements = { level = 5 },
    periodic_bonuses = { stamina = 100 },
  }),

  PERK_SLIPPERY_ESCAPE = Mutation.new({
    id = "perk_slippery_escape",
    name = "Slippery Escape",
    description = "Your joints are flexible enough that you can twist out of nearly anything when given a mind. At the cost of some pain, you can twist out of enemy grabs.",
    requirements = { level = 3, stats = { DEX = 8 } },
    activated = true,
    iuse_function = "slippery_escape",
  }),

  PERK_SURGICAL_STRIKES = Mutation.new({
    id = "perk_surgical_strikes",
    name = "Surgical Strikes",
    description = "Careful aim allows you to take down game with little tissue damage. Enemies you kill tend to stay intact.",
    requirements = { level = 5, stats = { DEX = 10 } },
    stat_bonuses = {},
  }),

  PERK_BLOODY_MESS = Mutation.new({
    id = "perk_bloody_mess",
    name = "Bloody Mess",
    description = "For whatever reason you seem to always make a real mess of things. Enemies you kill tend to explode into a mess of viscera.",
    requirements = { level = 3 },
    stat_bonuses = {},
  }),

  PERK_PERFECT_TIME = Mutation.new({
    id = "perk_perfect_time",
    name = "Perfect Timing",
    description = "You used to annoy your friends before the Cataclysm by always being *exactly* on time. You always know what time it is.",
    requirements = { level = 3 },
    stat_bonuses = {},
  }),

  PERK_ALL_TERRAIN = Mutation.new({
    id = "perk_all_terrain",
    name = "All Terrain",
    description = "You have a persistent sense of how to move around in sharp and rough terrain. You'll never accidentally snag or cut yourself on protrusions.",
    requirements = { level = 5 },
    stat_bonuses = {},
  }),

  PERK_JUMPY = Mutation.new({
    id = "perk_jumpy",
    name = "Jumpy",
    description = "You've not been the same since the apocalypse, almost everything makes you jump. You are just a bit quicker to act.",
    requirements = { level = 3 },
    stat_bonuses = { speed = 3 },
  }),

  PERK_QUICK_RECOVERY = Mutation.new({
    id = "perk_quick_recovery",
    name = "Quick Recovery",
    description = "You get knocked down, but you get up again. There is a 75% chance when you are downed that you will stand up again immediately.",
    requirements = { level = 5, stats = { STR = 8 } },
    stat_bonuses = {},
  }),

  PERK_SLEEPY = Mutation.new({
    id = "perk_sleepy",
    name = "Easy Sleeper",
    description = "You've always been able to fall asleep within a few minutes of closing your eyes. You have a much easier time falling asleep.",
    requirements = { level = 3 },
    periodic_bonuses = {},
  }),

  PERK_RUNNING_IN_HEELS = Mutation.new({
    id = "perk_running_in_heels",
    name = "Running in Heels",
    description = "You have mastered the ways of the 90s action heroine. Heels only encumber you as much as normal shoes.",
    requirements = { level = 3, stats = { DEX = 8 } },
    stat_bonuses = {},
  }),

  PERK_DUCK_AND_WEAVE = Mutation.new({
    id = "perk_duck_and_weave",
    name = "Duck and Weave",
    description = "Run without rhythm. After a short wind-up period, you gain 5 effective dodge and +1 bonus dodge attempt as long as you keep running.",
    requirements = { level = 5, stats = { DEX = 10 } },
    stat_bonuses = {},
  }),

  PERK_HOBBYIST = Mutation.new({
    id = "perk_hobbyist",
    name = "Hobbyist",
    description = "No work, no social obligations, lots of time to focus on your hobbies. You feel like you've been picking up skills faster recently.",
    requirements = { level = 5, stats = { INT = 8 } },
    stat_bonuses = {},
  }),

  PERK_NON_COMBATANT = Mutation.new({
    id = "perk_non_combatant",
    name = "Non-Combatant",
    description = "Hey, that's against the Geneva Convention! When completely unarmed, you take -25% reduced damage.",
    requirements = { level = 5 },
    stat_bonuses = {},
  }),

  PERK_BLOOD_DRINKER = Mutation.new({
    id = "perk_blood_drinker",
    name = "Hemophage",
    description = "You do not drink…wine. You are immune to the negative consequences of drinking blood, such as parasitic infection.",
    requirements = { level = 5 },
    stat_bonuses = {},
  }),

  PERK_BLOOD_DRINKING_HEAL = Mutation.new({
    id = "perk_blood_drinking_heal",
    name = "The Blood is the Life",
    description = "Drinking blood makes you feel really good. Every time you drink blood, you heal a bit of damage and lose a bit of pain.",
    requirements = { level = 5 },
    kill_monster_bonuses = { heal_percent = 25 },
  }),

  PERK_POPEYE = Mutation.new({
    id = "perk_popeye",
    name = "Of Sailors and Spinach",
    description = "You spent several years on the high sea and now your muscles visibly swell and clench when you eat raw spinach.",
    requirements = { level = 3, stats = { STR = 8 } },
    activated = true,
    iuse_function = "popeye",
  }),

  PERK_THICKBLOOD = Mutation.new({
    id = "perk_thickblood",
    name = "Thick-blooded",
    description = "Your blood has always been thicker than most, making you lose less of it when bleeding.",
    requirements = { level = 3 },
    stat_bonuses = {},
  }),

  PERK_RED_RAGE = Mutation.new({
    id = "perk_red_rage",
    name = "Red rage",
    description = "You hate the feeling of your blood leaking so much that it empowers you. You deal more melee damage the more you are bleeding.",
    requirements = { level = 5, stats = { STR = 10 } },
    stat_bonuses = {},
  }),

  PERK_PEER_PRESSURE = Mutation.new({
    id = "perk_peer_pressure",
    name = "Peer pressure",
    description = "You know how to leverage your allies' presence when talking with others, granting you a small boost to lying, persuading and intimidating for each nearby ally.",
    requirements = { level = 5 },
    stat_bonuses = {},
  }),
}

local ALL_PERK_IDS = {}
local STAT_BONUS_IDS = {}
local PERIODIC_BONUS_IDS = {}
local KILL_MONSTER_BONUS_IDS = {}
local ACTIVATED_PERK_IDS = {}

local function register_perk(perk)
  local perk_id = perk:get_perk_id()
  table.insert(ALL_PERK_IDS, perk_id)

  if next(perk.stat_bonuses) ~= nil then table.insert(STAT_BONUS_IDS, perk_id) end
  if next(perk.periodic_bonuses) ~= nil then table.insert(PERIODIC_BONUS_IDS, perk_id) end
  if next(perk.kill_monster_bonuses) ~= nil then table.insert(KILL_MONSTER_BONUS_IDS, perk_id) end
  if perk.activated then table.insert(ACTIVATED_PERK_IDS, perk_id) end
end

for id, perk in pairs(PERKS) do
  register_perk(perk)
end

return {
  Mutation = Mutation,
  PERKS = PERKS,
  ALL_PERK_IDS = ALL_PERK_IDS,
  STAT_BONUS_IDS = STAT_BONUS_IDS,
  PERIODIC_BONUS_IDS = PERIODIC_BONUS_IDS,
  KILL_MONSTER_BONUS_IDS = KILL_MONSTER_BONUS_IDS,
  ACTIVATED_PERK_IDS = ACTIVATED_PERK_IDS,
  register_perk = register_perk,
}