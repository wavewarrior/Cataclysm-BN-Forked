#pragma once

#include "damage.h"
#include "enums.h"
#include "line.h"
#include "output.h"
#include "point.h"
#include <string>

class Creature;
struct damage_unit;

/// High-level combat feedback type for SCT spawning.
enum class combat_feedback_type {
    none,
    /// Damage number (with optional crit/graze modifiers).
    damage,
    /// Defensive outcome: miss, dodge, parry, block, graze.
    miss,
    dodge,
    parry,
    block,
    graze,
};

/// Options for spawning floating combat text via spawn_combat_feedback().
struct combat_feedback_options {
    /// What kind of feedback to show.
    combat_feedback_type type = combat_feedback_type::none;

    /// Target creature position (screen-space).
    point pos{};

    /// Direction from attacker to target.
    direction dir = direction::NORTH;

    /// Text content (damage number, "MISS", etc.).
    std::string text;

    /// Game message type for color rendering.
    int msg_type = 0; // game_message_type as int

    /// Damage type for color mapping (used when type == damage or graze).
    int damage_type = 0; // damage_type as int

    /// Whether this is a critical hit.
    bool is_critical = false;

    /// Whether this is a triple critical hit.
    bool is_triple_crit = false;

    /// Whether this is a grazing hit (reduced damage).
    bool is_graze = false;

    /// Secondary text (e.g., remaining HP bar label "hp").
    std::string text2;

    /// Secondary message type.
    int msg_type2 = 0;

    /// SCT type tag (e.g., "hp" for HP bar entries).
    std::string sct_type;
};

/// Spawn floating combat text (SCT) for a combat outcome.
/// This is the centralized helper that encapsulates all SCT spawn logic:
/// position jitter calculation, color mapping, size scaling, and flag setting.
/// Callers pass high-level info and the helper handles everything else.
void spawn_combat_feedback( const Creature &target, const combat_feedback_options &opts );

/// Convenience overload for spawning damage numbers with HP bar secondary text.
/// Used by melee and ranged attack code paths.
void spawn_damage_number( const Creature &target, int damage, direction dir,
                          bool is_critical = false, bool is_triple_crit = false,
                          int dt = 0, bool is_graze = false );

/// Convenience overload for armor feedback (damage or destruction).
void spawn_armor_feedback( const Creature &target, const std::string &armor_name,
                           const std::string &verb_or_status, game_message_type verb_color,
                           bool is_destroyed = false );
