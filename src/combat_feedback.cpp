#include "combat_feedback.h"

#include "cata_utility.h"
#include "creature.h"
#include "game.h"
#include "options.h"
#include "output.h"
#include "lighting/rmlui_layer.h"
#include "rng.h"
#include "sdltiles.h"
#include "cata_tiles.h"

/// Map combat_feedback_type to sct_feedback_type for the cSCT entry.
static sct_feedback_type to_sct_feedback( combat_feedback_type type )
{
    switch( type ) {
        case combat_feedback_type::damage:
        case combat_feedback_type::graze:
            return sct_feedback_type::damage;
        case combat_feedback_type::miss:
        case combat_feedback_type::dodge:
        case combat_feedback_type::parry:
        case combat_feedback_type::block:
        default:
            return sct_feedback_type::outcome;
    }
}

/// Text labels for defensive outcomes.
static const char *outcome_text( combat_feedback_type type )
{
    switch( type ) {
        case combat_feedback_type::miss:
            return "MISS";
        case combat_feedback_type::dodge:
            return "DODGE";
        case combat_feedback_type::parry:
            return "PARRY";
        case combat_feedback_type::block:
            return "BLOCK";
        case combat_feedback_type::graze:
            return "GRAZE";
        default:
            return "";
    }
}

/// Color for defensive outcomes (as game_message_type int).
static int outcome_color( combat_feedback_type type )
{
    switch( type ) {
        case combat_feedback_type::miss:
            return static_cast<int>( m_neutral ); // white/gray
        case combat_feedback_type::dodge:
            return static_cast<int>( m_info );     // light blue
        case combat_feedback_type::parry:
            return static_cast<int>( m_info );     // cyan (slightly larger handled by caller)
        case combat_feedback_type::block:
            return static_cast<int>( m_warning );  // brown/tan
        case combat_feedback_type::graze:
            return static_cast<int>( m_grazing );  // dimmed grazing color
        default:
            return static_cast<int>( m_neutral );
    }
}

void spawn_combat_feedback( const Creature &target, const combat_feedback_options &opts )
{
    // Check if SCT is enabled.
    if( !get_option<bool>( "ANIMATION_SCT" ) ) {
        return;
    }

    // Gate by option: damage numbers vs outcome indicators.
    bool show_damage = get_option<bool>( "ANIMATION_SCT_DAMAGE" );
    bool show_outcomes = get_option<bool>( "ANIMATION_SCT_OUTCOMES" );

    if( opts.type == combat_feedback_type::damage || opts.type == combat_feedback_type::graze ) {
        if( !show_damage ) {
            return;
        }
    } else {
        // Defensive outcome types.
        if( !show_outcomes ) {
            return;
        }
    }

    // Build the text content.
    std::string sText = opts.text;
    std::string sText2 = opts.text2;
    int gmt = opts.msg_type;
    int gmt2 = opts.msg_type2;
    std::string sType = opts.sct_type;

    // For outcome types without explicit text, use the standard label.
    if( sText.empty() && ( opts.type == combat_feedback_type::miss ||
                           opts.type == combat_feedback_type::dodge ||
                           opts.type == combat_feedback_type::parry ||
                           opts.type == combat_feedback_type::block ||
                           opts.type == combat_feedback_type::graze ) ) {
        sText = outcome_text( opts.type );
        gmt = outcome_color( opts.type );
    }

    // Size multiplier based on feedback type.
    float size_mult = 1.0f;
    if( opts.is_triple_crit ) {
        size_mult = 2.0f;
    } else if( opts.is_critical ) {
        size_mult = 1.5f;
    } else if( opts.is_graze || opts.type == combat_feedback_type::graze ) {
        size_mult = 0.75f;
    }

    // Damage type for color mapping.
    sct_damage_type dt = sct_damage_type::none;
    if( opts.damage_type != 0 ) {
        dt = scrollingcombattext::from_game_dt( static_cast<damage_type>( opts.damage_type ) );
    }

    // Spawn the SCT entry.
    SCT.add( opts.pos, opts.dir, sText, static_cast<game_message_type>( gmt ),
             sText2, static_cast<game_message_type>( gmt2 ), sType );

    // Apply floating combat text properties to the last-added entry.
    if( !SCT.vSCT.empty() ) {
        SCT.vSCT.back().set_damage_type( dt );
        SCT.vSCT.back().set_size_multiplier( size_mult );
        SCT.vSCT.back().set_is_critical( opts.is_critical );
        SCT.vSCT.back().set_is_triple_crit( opts.is_triple_crit );
        SCT.vSCT.back().set_feedback_type( to_sct_feedback( opts.type ) );
    }

    // Phase 5: Floating combat text with physics-based trajectory.
    // Convert target's map position to screen coordinates via tilecontext.
    if( tilecontext ) {
        const auto [sx, sy] = tilecontext->player_to_screen( target.bub_pos().xy() );

        // Color by damage type (opts.damage_type is int, cast to damage_type enum).
        std::uint32_t rgba = 0xFFFFFFFF; // white = physical
        if( opts.damage_type != 0 ) {
            const auto dt_enum = static_cast<damage_type>( opts.damage_type );
            if( dt_enum == DT_HEAT ) {
                rgba = 0xFF8040FF; // orange = fire
            } else if( dt_enum == DT_ELECTRIC ) {
                rgba = 0x40D0FFFF; // cyan = electric
            } else if( dt_enum == DT_BIOLOGICAL ) {
                rgba = 0x40FF40FF; // green = bio
            }
        }

        auto fscale = 1.0f;
        auto vy = -30.f;
        auto ay = 5.f;
        if( opts.is_triple_crit ) {
            fscale = 2.0f;
            vy = -80.f;
            ay = 20.f;
        } else if( opts.is_critical ) {
            fscale = 1.5f;
            vy = -60.f;
            ay = 15.f;
        } else if( opts.is_graze ) {
            fscale = 0.75f;
        }

        const auto vx = static_cast<float>( rng( -10, 10 ) );
        rmlui_layer::combat_text_add( {
            .x = static_cast<float>( sx ),
            .y = static_cast<float>( sy ),
            .text = sText,
            .rgba = rgba,
            .font_scale = fscale,
            .lifetime_ms = 1200.f,
            .vx = vx,
            .vy = vy,
            .ay = ay,
        } );
    }
}

void spawn_damage_number( const Creature &target, int damage, direction dir,
                          bool is_critical, bool is_triple_crit, int dt, bool is_graze )
{
    // Build the HP bar string for the damage number.
    auto hp_bar = get_hp_bar( damage, target.get_hp_max(), true );

    combat_feedback_options opts;
    opts.type = combat_feedback_type::damage;
    opts.pos = target.bub_pos().xy().raw();
    opts.dir = dir;
    opts.text = hp_bar.first;
    opts.msg_type = static_cast<int>( m_good );
    opts.damage_type = dt;
    opts.is_critical = is_critical;
    opts.is_triple_crit = is_triple_crit;
    opts.is_graze = is_graze;

    spawn_combat_feedback( target, opts );

    // Add HP bar secondary text if creature still has HP.
    if( target.get_hp() > 0 ) {
        auto hp_bar_remaining = get_hp_bar( target.get_hp(), target.get_hp_max(), true );

        combat_feedback_options opts2;
        opts2.type = combat_feedback_type::damage;
        opts2.pos = target.bub_pos().xy().raw();
        opts2.dir = dir;
        opts2.text = hp_bar_remaining.first;
        opts2.msg_type = static_cast<int>( m_good );
        opts2.text2 = _( "hp" );
        opts2.msg_type2 = static_cast<int>( m_neutral );
        opts2.sct_type = "hp";

        spawn_combat_feedback( target, opts2 );
    } else {
        SCT.removeCreatureHP();
    }
}

void spawn_armor_feedback( const Creature &target, const std::string &armor_name,
                           const std::string &verb_or_status, game_message_type verb_color,
                           bool is_destroyed )
{
    combat_feedback_options opts;
    opts.type = combat_feedback_type::damage; // Use damage type for armor feedback
    opts.pos = target.bub_pos().xy().raw();
    opts.dir = direction::NORTH;
    opts.text = armor_name;
    opts.msg_type = static_cast<int>( m_neutral );
    opts.text2 = verb_or_status;
    opts.msg_type2 = static_cast<int>( verb_color );

    spawn_combat_feedback( target, opts );
}
