#include "ranged.h"

#include "activity_actor_definitions.h"
#include "animation.h"
#include "avatar.h"
#include "ballistics.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "catalua_coord.h"
#include "catalua_hooks.h"
#include "catalua_icallback_actor.h"
#include "catalua_sol.h"
#include "character.h"
#include "character_functions.h"
#include "color.h"
#include "coordinates.h"
#include "creature.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "sound_visualization.h"
#include "dispersion.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "input.h"
#include "item.h"
#include "item_functions.h"
#include "item_reload_option.h"
#include "itype.h"
#include "line.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "map.h"
#include "material.h"
#include "math_defines.h"
#include "messages.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "panels.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "projectile.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "rng.h"
#include "shape_impl.h"
#include "skill.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "ui_manager.h"
#include "units.h"
#include "units_angle.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "sdl_window_dims.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

struct ammo_effect;

using ammo_effect_str_id = string_id<ammo_effect>;

static const weapon_category_id weapon_cat_GRENADE_LAUNCHERS( "GRENADE_LAUNCHERS" );
static const weapon_category_id weapon_cat_MAGNETIC( "MAGNETIC" );
static const weapon_category_id weapon_cat_PNEUMATIC( "PNEUMATIC" );
static const weapon_category_id weapon_cat_FLAMETHROWERS( "FLAMETHROWERS" );
static const weapon_category_id weapon_cat_SPRAY_GUNS( "SPRAY_GUNS" );
static const weapon_category_id weapon_cat_WATER_CANNONS( "WATER_CANNONS" );
static const weapon_category_id weapon_cat_ROCKET_LAUNCHERS( "ROCKET_LAUNCHERS" );
static const weapon_category_id weapon_cat_ELASTIC( "ELASTIC" );
static const weapon_category_id weapon_cat_S_XBOWS( "S_XBOWS" );
static const weapon_category_id weapon_cat_M_XBOWS( "M_XBOWS" );
static const weapon_category_id weapon_cat_ENERGY_WEAPONS( "ENERGY_WEAPONS" );

static const ammo_effect_str_id ammo_effect_ACT_ON_RANGED_HIT( "ACT_ON_RANGED_HIT" );
static const ammo_effect_str_id ammo_effect_BLACKPOWDER( "BLACKPOWDER" );
static const ammo_effect_str_id ammo_effect_BOUNCE( "BOUNCE" );
static const ammo_effect_str_id ammo_effect_BLINDS_EYES( "BLINDS_EYES" );
static const ammo_effect_str_id ammo_effect_BURST( "BURST" );
static const ammo_effect_str_id ammo_effect_CUSTOM_EXPLOSION( "CUSTOM_EXPLOSION" );
static const ammo_effect_str_id ammo_effect_EMP( "EMP" );
static const ammo_effect_str_id ammo_effect_EXPLOSIVE( "EXPLOSIVE" );
static const ammo_effect_str_id ammo_effect_HEAVY_HIT( "HEAVY_HIT" );
static const ammo_effect_str_id ammo_effect_IGNITE( "IGNITE" );
static const ammo_effect_str_id ammo_effect_LASER( "LASER" );
static const ammo_effect_str_id ammo_effect_LIGHTNING( "LIGHTNING" );
static const ammo_effect_str_id ammo_effect_JET( "JET" );
static const ammo_effect_str_id ammo_effect_NO_CRIT( "NO_CRIT" );
static const ammo_effect_str_id ammo_effect_NO_DAMAGE( "NO_DAMAGE" );
static const ammo_effect_str_id ammo_effect_NO_OVERSHOOT( "NO_OVERSHOOT" );
static const ammo_effect_str_id ammo_effect_NO_EMBED( "NO_EMBED" );
static const ammo_effect_str_id ammo_effect_NO_ITEM_DAMAGE( "NO_ITEM_DAMAGE" );
static const ammo_effect_str_id ammo_effect_NON_FOULING( "NON_FOULING" );
static const ammo_effect_str_id ammo_effect_PLASMA( "PLASMA" );
static const ammo_effect_str_id ammo_effect_RECYCLED( "RECYCLED" );
static const ammo_effect_str_id ammo_effect_DRAW_AS_LINE( "DRAW_AS_LINE" );
static const ammo_effect_str_id ammo_effect_SHATTER_SELF( "SHATTER_SELF" );
static const ammo_effect_str_id ammo_effect_SHOT( "SHOT" );
static const ammo_effect_str_id ammo_effect_STREAM( "STREAM" );
static const ammo_effect_str_id ammo_effect_STREAM_BIG( "STREAM_BIG" );
static const ammo_effect_str_id ammo_effect_TANGLE( "TANGLE" );
static const ammo_effect_str_id ammo_effect_NET_TANGLE( "NET_TANGLE" );
static const ammo_effect_str_id ammo_effect_WIDE( "WIDE" );
static const ammo_effect_str_id ammo_effect_THROWN( "THROWN" );

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_hit_by_player( "hit_by_player" );
static const efftype_id effect_on_roof( "on_roof" );

static const itype_id itype_UPS( "UPS" );

static const trap_str_id tr_practice_target( "tr_practice_target" );

static const fault_id fault_gun_blackpowder( "fault_gun_blackpowder" );
static const fault_id fault_gun_chamber_spent( "fault_gun_chamber_spent" );
static const fault_id fault_gun_dirt( "fault_gun_dirt" );
static const fault_id fault_gun_unlubricated( "fault_gun_unlubricated" );

static const skill_id skill_driving( "driving" );
static const skill_id skill_gun( "gun" );
static const skill_id skill_launcher( "launcher" );
static const skill_id skill_pistol( "pistol" );
static const skill_id skill_rifle( "rifle" );
static const skill_id skill_throw( "throw" );

static const bionic_id bio_railgun( "bio_railgun" );
static const bionic_id bio_targeting( "bio_targeting" );
static const bionic_id bio_ups( "bio_ups" );

static const trait_id trait_LASER_GUIDED( "LASER_GUIDED" );
static const trait_id trait_PYROMANIA( "PYROMANIA" );
static const trait_id trait_NORANGEDCRIT( "NO_RANGED_CRIT" );

// not to confuse with item flags (json_flag)
static const std::string flag_SHOOT_ME( "SHOOT_ME" );

// Maximum duration of aim-and-fire loop, in turns
static constexpr int AIF_DURATION_LIMIT = 10;

static void cycle_action( item& weap, const tripoint_bub_ms& pos );

namespace
{

constexpr auto vehicle_recoil_velocity_scale = 1.4;
constexpr auto vehicle_recoil_lateral_scale = 0.1;

struct shot_target_options {
    tripoint_bub_ms source;
    const tripoint_bub_ms &target;
    const projectile &proj;
    const dispersion_sources &dispersion;
};

struct pellet_target_options {
    tripoint_bub_ms source;
    const tripoint_bub_ms &target;
    double half_angle;
};

struct grouped_shot_hit {
    Creature *target = nullptr;
    std::string target_name;
    bool target_is_player = false;
    bool player_saw_hit = false;
    int pellet_hits = 0;
    int total_damage = 0;
};

struct grouped_shot_message_options {
    Character &source;
    item &gun;
    const std::vector<grouped_shot_hit> &grouped_hits;
};

auto get_shot_data( const item& gun ) -> const islot_ammo::shot_data*
{
    const auto* const ammo_type = gun.ammo_data();
    if( ammo_type == nullptr || ammo_type->ammo == nullptr ) { return nullptr; }

    return ammo_type->ammo->shot ? &*ammo_type->ammo->shot : nullptr;
}

auto get_shot_count( const item& gun ) -> int
{
    const auto* const shot_data = get_shot_data( gun );
    if( shot_data == nullptr ) { return 1; }

    return shot_data->count;
}

auto get_shot_half_angle( const item& gun ) -> double
{
    const auto* const shot_data = get_shot_data( gun );
    if( shot_data == nullptr ) { return 0.0; }

    return shot_data->half_angle;
}

auto get_shot_target( const shot_target_options& options ) -> tripoint_bub_ms
{
    const auto range = rl_dist( options.source, options.target );
    Creature* target_critter = g->critter_at( options.target );
    const auto target_size = target_critter != nullptr ? target_critter->ranged_target_size() : 0.0;
    const auto aim = projectile_attack_roll( options.dispersion, range, target_size );
    if( aim.missed_by_tiles < 1.0 ) { return options.target; }

    auto shot_target = options.target;
    const auto dx = options.target.x() - options.source.x();
    const auto dy = options.target.y() - options.source.y();
    auto rad = std::atan2( dy, dx );
    const auto dispersion_angle = units::to_radians(
                                      std::min( units::from_arcmin( aim.dispersion ), 30_degrees ) );
    rad += ( one_in( 2 ) ? 1 : -1 ) * dispersion_angle;

    const auto no_overshoot =
        options.proj.has_effect( ammo_effect_NO_OVERSHOOT )
        || ( g->critter_at( options.target ) == nullptr && get_map().passable( options.target ) );
    const auto offset = std::min<int>( range, std::sqrt( aim.missed_by_tiles ) );
    auto new_range =
        no_overshoot ? range + rng( -offset, offset ) : rng( range - offset, options.proj.range );
    new_range = std::max( new_range, 1 );
    shot_target.x() = options.source.x() + roll_remainder( new_range * std::cos( rad ) );
    shot_target.y() = options.source.y() + roll_remainder( new_range * std::sin( rad ) );
    if( shot_target == options.source ) {
        shot_target.x() = options.source.x() + sgn( dx );
        shot_target.y() = options.source.y() + sgn( dy );
    }

    return shot_target;
}

auto get_pellet_target( const pellet_target_options& options ) -> tripoint_bub_ms
{
    if( options.half_angle <= 0.0 ) { return options.target; }

    const auto dx = options.target.x() - options.source.x();
    const auto dy = options.target.y() - options.source.y();
    const auto range =
        std::max( static_cast<double>( trig_dist( options.source, options.target ) ), 1.0 );
    const auto base_angle = std::atan2( dy, dx );
    const auto angle_offset = units::to_radians( rng_float(
                                  units::from_degrees( -options.half_angle ), units::from_degrees( options.half_angle ) ) );
    const auto pellet_angle = base_angle + angle_offset;

    auto pellet_target = options.target;
    pellet_target.x() = options.source.x() + roll_remainder( range * std::cos( pellet_angle ) );
    pellet_target.y() = options.source.y() + roll_remainder( range * std::sin( pellet_angle ) );
    if( pellet_target == options.source ) {
        pellet_target.x() = options.source.x() + sgn( dx );
        pellet_target.y() = options.source.y() + sgn( dy );
    }

    return pellet_target;
}

auto get_projectile_animation_symbol( const projectile& proj ) -> char
{
    const auto stream =
        proj.has_effect( ammo_effect_STREAM ) || proj.has_effect( ammo_effect_STREAM_BIG )
    || proj.has_effect( ammo_effect_JET );
    return stream ? '#' : '*';
}

auto projectile_draws_as_line( const projectile& proj ) -> bool
{
    return proj.has_effect( ammo_effect_DRAW_AS_LINE ) ||
    get_option<bool>( "BULLETS_AS_LASERS" );
}

auto add_grouped_shot_hit(
    std::vector<grouped_shot_hit> &grouped_hits, const dealt_projectile_attack& shot ) -> void
{
    if( shot.hit_critter == nullptr ) { return; }

    const auto it = std::ranges::find( grouped_hits, shot.hit_critter, &grouped_shot_hit::target );
    if( it == grouped_hits.end() ) {
        grouped_hits.push_back( grouped_shot_hit{
            .target = shot.hit_critter,
            .target_name = shot.hit_critter->disp_name(),
            .target_is_player = shot.hit_critter->is_player(),
            .player_saw_hit = g->u.sees( *shot.hit_critter ),
            .pellet_hits = 1,
            .total_damage = shot.dealt_dam.total_damage(),
        } );
        return;
    }

    it->pellet_hits++;
    it->player_saw_hit |= g->u.sees( *shot.hit_critter );
    it->total_damage += shot.dealt_dam.total_damage();
}

auto print_grouped_shot_hit_messages( const grouped_shot_message_options& options ) -> void
{
    const auto ammo_name =
        options.gun.ammo_data() != nullptr ? options.gun.ammo_data()->nname( 1 ) : options.gun.tname();
    for( const auto& grouped_hit : options.grouped_hits ) {
        if( grouped_hit.target == nullptr ) { continue; }

        if( grouped_hit.target_is_player && grouped_hit.target != nullptr ) {
            if( grouped_hit.total_damage > 0 ) {
                grouped_hit.target->add_msg_if_player(
                    m_bad, _( "%1$d %2$s pellets hit you for %3$d damage." ), grouped_hit.pellet_hits,
                    ammo_name, grouped_hit.total_damage );
            } else {
                grouped_hit.target->add_msg_if_player(
                    m_warning, _( "%1$d %2$s pellets hit you but deal no damage." ),
                    grouped_hit.pellet_hits, ammo_name );
            }
            continue;
        }

        if( !grouped_hit.player_saw_hit ) { continue; }

        if( grouped_hit.total_damage > 0 ) {
            add_msg( options.source.is_player() ? m_good : m_neutral,
                     _( "%1$d %2$s pellets hit %3$s for %4$d damage." ), grouped_hit.pellet_hits,
                     ammo_name, grouped_hit.target_name, grouped_hit.total_damage );
        } else {
            add_msg( options.source.is_player() ? m_bad : m_neutral,
                     _( "%1$d %2$s pellets hit %3$s but deal no damage." ), grouped_hit.pellet_hits,
                     ammo_name, grouped_hit.target_name );
        }
    }
}

/// more generic version of `item::gunmod_find`
auto gunmod_find_with( item& it, std::function < auto( const item* )->bool > pred ) -> item*
{
    std::vector<item *> gunmods = it.gunmods();
    auto res = std::ranges::find_if( gunmods, pred );
    return res != gunmods.end() ? *res : nullptr;
}

} // namespace

#include "target_ui.h"
#include "ranged_internal.h"


target_handler::trajectory target_handler::mode_fire( avatar& you, aim_activity_actor& activity )
{
    target_ui ui = target_ui();
    ui.you = &you;
    ui.mode = target_ui::TargetMode::Fire;
    ui.activity = &activity;
    ui.relevant = activity.get_weapon();
    if( ui.relevant != nullptr ) { ui.shape_gen = ranged::get_target_shape_factory( *ui.relevant ); }

    return ui.run();
}

target_handler::trajectory target_handler::mode_throw(
    avatar& you, item& relevant, bool blind_throwing )
{
    target_ui ui = target_ui();
    ui.you = &you;
    ui.mode = blind_throwing ? target_ui::TargetMode::ThrowBlind : target_ui::TargetMode::Throw;
    ui.relevant = &relevant;
    ui.range = you.throw_range( relevant );

    restore_on_out_of_scope<tripoint_rel_ms> view_offset_prev( you.view_offset );
    return ui.run();
}

target_handler::trajectory target_handler::mode_reach( avatar& you, item& weapon )
{
    target_ui ui = target_ui();
    ui.you = &you;
    ui.mode = target_ui::TargetMode::Reach;
    ui.relevant = &weapon;
    ui.range = weapon.reach_range( you );

    restore_on_out_of_scope<tripoint_rel_ms> view_offset_prev( you.view_offset );
    return ui.run();
}

target_handler::trajectory target_handler::mode_turret_manual( avatar& you, turret_data& turret )
{
    target_ui ui = target_ui();
    ui.you = &you;
    ui.mode = target_ui::TargetMode::TurretManual;
    ui.turret = &turret;
    ui.relevant = &turret.base();

    restore_on_out_of_scope<tripoint_rel_ms> view_offset_prev( you.view_offset );
    return ui.run();
}

target_handler::trajectory target_handler::mode_turrets(
    avatar& you, vehicle& veh, const std::vector<vehicle_part *> &turrets )
{
    // Find radius of a circle centered at u encompassing all points turrets can aim at
    // FIXME: this calculation is fine for square distances, but results in an underestimation
    //        when used with real circles
    int range_total = 0;
    for( vehicle_part * t : turrets ) {
        int range = veh.turret_query( *t ).range();
        tripoint_bub_ms pos = veh.bub_part_location( *t );

        int res = 0;
        res = std::max( res, rl_dist( you.bub_pos(), pos + point_rel_ms( range, 0 ) ) );
        res = std::max( res, rl_dist( you.bub_pos(), pos + point_rel_ms( -range, 0 ) ) );
        res = std::max( res, rl_dist( you.bub_pos(), pos + point_rel_ms( 0, range ) ) );
        res = std::max( res, rl_dist( you.bub_pos(), pos + point_rel_ms( 0, -range ) ) );
        range_total = std::max( range_total, res );
    }

    target_ui ui = target_ui();
    ui.you = &you;
    ui.mode = target_ui::TargetMode::Turrets;
    ui.veh = &veh;
    ui.vturrets = &turrets;
    ui.range = range_total;

    restore_on_out_of_scope<tripoint_rel_ms> view_offset_prev( you.view_offset );
    return ui.run();
}

target_handler::trajectory target_handler::mode_spell(
    avatar& you, spell& casting, bool no_fail, bool no_mana )
{
    target_ui ui = target_ui();
    ui.you = &you;
    ui.mode = target_ui::TargetMode::Spell;
    ui.casting = &casting;
    ui.range = casting.range();
    ui.no_fail = no_fail;
    ui.no_mana = no_mana;

    restore_on_out_of_scope<tripoint_rel_ms> view_offset_prev( you.view_offset );
    return ui.run();
}

target_handler::trajectory target_handler::mode_shaped(
    avatar& you, const shape_factory& shape_fac, aim_activity_actor& activity )
{
    target_ui ui = target_ui();
    ui.you = &you;
    ui.mode = target_ui::TargetMode::Shape;
    ui.shape_gen = shape_fac;
    ui.range = shape_fac.get_range();
    ui.activity = &activity;
    ui.relevant = activity.get_weapon();

    return ui.run();
}

double occupied_tile_fraction( creature_size target_size )
{
    switch( target_size ) {
        case creature_size::tiny:
            return 0.1;
        case creature_size::small:
            return 0.25;
        case creature_size::medium:
            return 0.5;
        case creature_size::large:
            return 0.75;
        case creature_size::huge:
            return 1.0;
        default:
            break;
    }

    return 0.5;
}

double Creature::ranged_target_size() const
{
    if( const_cast<Creature &>( *this ).uncanny_dodge() ) {
        return 0.0;
    }
    return effective_target_size();
}

auto Creature::effective_target_size() const -> double
{
    auto is_crouched = false;
    if( const auto *ch = as_character() ) {
        is_crouched = ch->is_crouching();
    }
    if( has_flag( MF_HARDTOSHOOT ) || is_crouched ) {
        switch( get_size() ) {
            case creature_size::tiny:
                return 0.05;
            case creature_size::small:
                return occupied_tile_fraction( creature_size::tiny );
            case creature_size::medium:
                return occupied_tile_fraction( creature_size::small );
            case creature_size::large:
                return occupied_tile_fraction( creature_size::medium );
            case creature_size::huge:
                return occupied_tile_fraction( creature_size::large );
            default:
                break;
        }
    }
    return occupied_tile_fraction( get_size() );
}

int range_with_even_chance_of_good_hit( int dispersion )
{
    int even_chance_range = 0;
    while(
        static_cast<unsigned>( even_chance_range )
        < Creature::dispersion_for_even_chance_of_good_hit.size()
        && dispersion < Creature::dispersion_for_even_chance_of_good_hit[even_chance_range] ) {
        even_chance_range++;
    }
    return even_chance_range;
}

int ranged::gun_engagement_moves( const Character& who, const item& gun, int target, int start )
{
    int mv = 0;
    double penalty = start;

    while( penalty > target ) {
        double adj = ranged::aim_per_move( who, gun, penalty );
        if( adj <= 0 ) { break; }
        penalty -= adj;
        mv++;
    }

    return mv;
}

bool ranged::handle_gun_damage( Character& shooter, item& it )
{
    // Below item (maximum dirt possible) should be greater than or equal to dirt range in
    // item_group.cpp. Also keep in mind that monster drops can have specific ranges and these
    // should be below the max!
    const double dirt_max_dbl = 10000;
    if( !it.is_gun() ) {
        debugmsg( "Tried to handle_gun_damage of a non-gun %s", it.tname() );
        return false;
    }

    int dirt = it.get_var( "dirt", 0 );
    int dirtadder = 0;
    double dirt_dbl = static_cast<double>( dirt );
    if( it.faults.contains( fault_gun_chamber_spent ) ) { return false; }

    const auto& curammo_effects = it.ammo_effects();
    // If using an aux magazine for a gun, make the parent gun accumulate and track fouling instead
    // of the gunmod that's doing the firing.
    item& parent =
        ( it.parent_item() != nullptr && it.has_flag( flag_USE_PARENT_GUN ) ) ? *it.parent_item() : it;
    const islot_gun& firing = *parent.type->gun;
    // misfire chance based on dirt accumulation. Formula is designed to make chance of jam highly
    // unlikely at low dirt levels, but levels increase geometrically as the dirt level reaches max
    // (10,000). The number used is just a figure I found reasonable after plugging the number into
    // excel and changing it until the probability made sense at high, medium, and low levels of
    // dirt.
    if( !parent.has_flag( flag_NEVER_JAMS )
        && x_in_y( dirt_dbl * dirt_dbl * dirt_dbl, 1000000000000.0 ) ) {
        shooter.add_msg_player_or_npc(
            _( "Your %s misfires with a muffled click!" ),
            _( "<npcname>'s %s misfires with a muffled click!" ), it.tname() );
        // at high dirt levels the chance to misfire gets to significant levels. 10,000 is max and
        // 7800 is quite high so above that the player gets some relief in the form of exchanging
        // time for some dirt reduction. Basically jiggling the parts loose to remove some dirt and
        // get a few more shots out.
        if( dirt_dbl > 7800 ) {
            shooter.add_msg_player_or_npc(
                _( "Perhaps taking the ammo out of your %s and reloading will help." ),
                _( "Perhaps taking the ammo out of <npcname>'s %s and reloading will help." ),
                it.tname() );
        }
        return false;
    }

    // Here we check if we're underwater and whether we should misfire.
    // As a result this causes no damage to the firearm, note that some guns are waterproof
    // and so are immune to this effect, note also that WATERPROOF_GUN status does not
    // mean the gun will actually be accurate underwater.
    int effective_durability = firing.durability;
    if( shooter.is_underwater() && !parent.has_flag( flag_WATERPROOF_GUN )
        && one_in( effective_durability ) ) {
        shooter.add_msg_player_or_npc(
            _( "Your %s misfires with a wet click!" ), _( "<npcname>'s %s misfires with a wet click!" ),
            it.tname() );
        return false;
        // Here we check for a chance for the weapon to suffer a mechanical malfunction.
        // Note that some weapons never jam up 'NEVER_JAMS' and thus are immune to this
        // effect as current guns have a durability between 5 and 9 this results in
        // a chance of mechanical failure between 1/(64*3) and 1/(1024*3) on any given shot.
        // the malfunction can't cause damage
    } else if( one_in( ( 2 << effective_durability ) * 3 ) && !it.has_flag( flag_NEVER_JAMS ) ) {
        shooter.add_msg_player_or_npc(
            _( "Your %s malfunctions!" ), _( "<npcname>'s %s malfunctions!" ), it.tname() );
        return false;
        // Here we check for a chance for the weapon to suffer a misfire due to
        // using player-made 'RECYCLED' bullets. Note that not all forms of
        // player-made ammunition have this effect.
    } else if( curammo_effects.contains( ammo_effect_RECYCLED ) && one_in( 256 ) ) {
        shooter.add_msg_player_or_npc(
            _( "Your %s misfires with a muffled click!" ),
            _( "<npcname>'s %s misfires with a muffled click!" ), it.tname() );
        return false;
        // Here we check for a chance for attached mods to get damaged if they are flagged as
        // 'CONSUMABLE'. This is mostly for crappy handmade expedient stuff  or things that rarely
        // receive damage during normal usage. Default chance is 1/10000 unless set via json, damage
        // is proportional to caliber(see below). Can be toned down with 'consume_divisor.'

    } else if( parent.has_flag( flag_CONSUMABLE ) && !curammo_effects.contains( ammo_effect_LASER )
               && !curammo_effects.contains( ammo_effect_PLASMA )
               && !curammo_effects.contains( ammo_effect_EMP ) ) {
        int uncork =
            ( ( 10 * it.ammo_data()->ammo->loudness ) + ( it.ammo_data()->ammo->recoil / 2 ) ) / 100;
        uncork = std::pow( uncork, 3 ) * 6.5;
        for( auto mod : parent.gunmods() ) {
            if( mod->has_flag( flag_CONSUMABLE ) ) {
                int dmgamt = uncork / mod->type->gunmod->consume_divisor;
                int modconsume = mod->type->gunmod->consume_chance;
                int initstate = parent.damage();
                // fuzz damage if it's small
                if( dmgamt < 1000 ) {
                    dmgamt = rng( dmgamt, dmgamt + 200 );
                    // ignore damage if inconsequential.
                }
                if( dmgamt < 800 ) { dmgamt = 0; }
                if( one_in( modconsume ) ) {
                    if( mod->mod_damage( dmgamt ) ) {
                        shooter.add_msg_player_or_npc(
                            m_bad, _( "Your attached %s is destroyed by your shot!" ),
                            _( "<npcname>'s attached %s is destroyed by their shot!" ), mod->tname() );
                        mod->detach();
                    } else if( parent.damage() > initstate ) {
                        shooter.add_msg_player_or_npc(
                            m_bad, _( "Your attached %s is damaged by your shot!" ),
                            _( "<npcname>'s %s is damaged by their shot!" ), mod->tname() );
                    }
                }
            }
        }
    }
    if( parent.has_fault( fault_gun_unlubricated ) && x_in_y( dirt_dbl, dirt_max_dbl ) ) {
        shooter.add_msg_player_or_npc(
            m_bad, _( "Your %s emits a grimace-inducing screech!" ),
            _( "<npcname>'s %s emits a grimace-inducing screech!" ), it.tname() );
        parent.inc_damage();
    }
    if( ( ( !curammo_effects.contains( ammo_effect_NON_FOULING ) &&
            !parent.has_flag( flag_NON_FOULING ) )
          || ( parent.has_fault( fault_gun_unlubricated ) ) )
        && !parent.has_flag( flag_PRIMITIVE_RANGED_WEAPON ) ) {
        if( curammo_effects.contains( ammo_effect_BLACKPOWDER )
            || parent.has_fault( fault_gun_unlubricated ) ) {
            if( ( ( it.ammo_data()->ammo->recoil < firing.min_cycle_recoil )
                  || ( parent.has_fault( fault_gun_unlubricated ) && one_in( 16 ) ) )
                && parent.faults_potential().contains( fault_gun_chamber_spent ) ) {
                shooter.add_msg_player_or_npc(
                    m_bad, _( "Your %s fails to cycle!" ), _( "<npcname>'s %s fails to cycle!" ),
                    it.tname() );
                parent.faults.insert( fault_gun_chamber_spent );
                // Don't return false in this case; this shot happens, follow-up ones won't.
            }
        }
        // These are the dirtying/fouling mechanics
        if( !curammo_effects.contains( ammo_effect_NON_FOULING )
            && !parent.has_flag( flag_NON_FOULING ) ) {
            if( dirt < static_cast<int>( dirt_max_dbl ) ) {
                dirtadder =
                    curammo_effects.count( ammo_effect_BLACKPOWDER )
                    * ( 200 - ( firing.blackpowder_tolerance * 2 ) );
                // dirtadder is the dirt-increasing number for shots fired with gunpowder-based
                // ammo. Usually dirt level increases by 1, unless it's blackpowder, in which case
                // it increases by a higher number, but there is a reduction for blackpowder
                // resistance of a weapon.
                if( dirtadder < 0 ) { dirtadder = 0; }
                // in addition to increasing dirt level faster, regular gunpowder fouling is also
                // capped at 7,150, not 10,000. So firing with regular gunpowder can never make the
                // gun quite as bad as firing it with black gunpowder. At 7,150 the chance to jam is
                // significantly lower (though still significant) than it is at 10,000, the absolute
                // cap.
                if( curammo_effects.contains( ammo_effect_BLACKPOWDER ) || dirt < 7150 ) {
                    parent.set_var(
                        "dirt", std::min( static_cast<int>( dirt_max_dbl ), dirt + dirtadder + 1 ) );
                }
            }
            dirt = parent.get_var( "dirt", 0 );
            dirt_dbl = static_cast<double>( dirt );
            if( dirt > 0 && !parent.faults.contains( fault_gun_blackpowder ) ) {
                parent.faults.insert( fault_gun_dirt );
            }
            if( dirt > 0 && curammo_effects.contains( ammo_effect_BLACKPOWDER ) ) {
                parent.faults.erase( fault_gun_dirt );
                parent.faults.insert( fault_gun_blackpowder );
            }
            // end fouling mechanics
        }
    }
    // chance to damage gun due to high levels of dirt. Very unlikely, especially at lower levels
    // and impossible below 5,000. Lower than the chance of a jam at the same levels. 555555... is
    // an arbitrary number that I came up with after playing with the formula in excel. It makes
    // sense at low, medium, and high levels of dirt.
    if( dirt_dbl > 5000 && x_in_y( dirt_dbl * dirt_dbl * dirt_dbl, 5555555555555 ) ) {
        shooter.add_msg_player_or_npc(
            m_bad, _( "Your %s is damaged by the high pressure!" ),
            _( "<npcname>'s %s is damaged by the high pressure!" ), it.tname() );
        // Don't increment until after the message
        parent.inc_damage();
    }
    return true;
}

void npc::pretend_fire( npc* source, int shots, item& gun )
{
    int curshot = 0;
    avatar& you = get_avatar();
    if( you.sees( *source ) && one_in( 50 ) ) {
        add_msg( m_info, _( "%s shoots something." ), source->disp_name() );
    }
    while( curshot != shots ) {
        if( gun.ammo_consume( gun.ammo_required(), bub_pos() ) != gun.ammo_required() ) {
            debugmsg( "Unexpected shortage of ammo whilst firing %s", gun.tname().c_str() );
            break;
        }

        item* weapon = &gun;
        const auto data = weapon->gun_noise( shots > 1 );

        if( you.sees( *source ) ) { add_msg( m_warning, _( "You hear %s." ), data.sound ); }
        curshot++;
        moves -= 100;
    }
}


namespace
{

auto is_mountable( const map& m, const tripoint_bub_ms& pos ) -> bool
{
    // usage of any attached bipod is dependent upon terrain
    // only allow mounting passable OR climable terrain
    // example: sandbag barricades are impassable but climbable
    if( ( m.climb_difficulty( pos ) <= 5 || m.passable( pos ) )
        && m.has_flag_ter_or_furn( "MOUNTABLE", pos ) ) {
        return true;
    }

    if( const optional_vpart_position vp = m.veh_at( pos ) ) {
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        return m.passable( pos ) && vp->vehicle().has_part( tripoint_bub_ms( pos ), "MOUNTABLE" );
    }
    return false;
}

auto is_mountable_nearby( const map& m, const tripoint_bub_ms& pos ) -> bool
{
    const auto& xs = closest_points_first( pos, 1 );
    return std::any_of( xs.begin(), xs.end(), [&m]( const tripoint_bub_ms & x ) -> bool {
        return is_mountable( m, x );
    } );
}

auto can_use_heavy_weapon( const Character& who, const map& m, const tripoint_bub_ms& pos ) -> bool
{
    if( who.is_mounted() && who.mounted_creature->has_flag( MF_RIDEABLE_MECH ) ) {
    return true;
}
return is_mountable_nearby( m, pos );
}

auto firing_vehicle( map& here, const Character& who ) -> vehicle* // *NOPAD*
{
    if( !who.in_vehicle && !who.has_effect( effect_on_roof ) ) { return nullptr; }

    const auto vp = here.veh_at( who.bub_pos() );
    if( !vp ) { return nullptr; }

    return &vp->vehicle();
}

auto has_only_rigid_wheels( const vehicle& veh ) -> bool
{
    auto has_wheels = false;
    for( auto index = 0; index < veh.part_count(); ++index ) {
        if( !veh.part_info( index ).has_flag( VPFLAG_WHEEL ) ) { continue; }

        has_wheels = true;
        if( std::abs( veh.part_info( index ).wheel_or_rating() - 0.1f ) > 0.001f ) { return false; }
    }

    return has_wheels;
}

auto apply_gun_recoil_to_vehicle(
    map& here, const Character& who, const tripoint_bub_ms& target,
    const tripoint_bub_ms& shot_origin, const int gun_recoil, const int shots ) -> void
{
    if( gun_recoil <= 0 || shots <= 0 ) { return; }

    auto* const veh = firing_vehicle( here, who );
    if( veh == nullptr ) { return; }

    const auto recoil_direction = rl_vec2d( shot_origin.xy() - target.xy() );
    if( recoil_direction.is_null() ) { return; }

    const auto vehicle_mass_kg =
        std::max( 1.0, static_cast<double>( units::to_kilogram( veh->total_mass() ) ) );
    const auto recoil_velocity = static_cast<int>( std::round(
                                     static_cast<double>( gun_recoil ) * shots * vehicle_recoil_velocity_scale
                                     * get_option<float>( "VEHICLE_GUN_RECOIL_FACTOR" ) / vehicle_mass_kg ) );
    if( recoil_velocity == 0 ) { return; }

    const auto face_velocity_vec = veh->face_vec();
    const auto lateral_velocity_vec = rl_vec2d( -face_velocity_vec.y, face_velocity_vec.x );
    const auto recoil_velocity_vec = recoil_direction.normalized() * recoil_velocity;
    if( has_only_rigid_wheels( *veh ) ) {
        const auto final_velocity = veh->velo_vec() + recoil_velocity_vec;
        const auto resulting_velocity = static_cast<int>( std::round( final_velocity.magnitude() ) );
        if( resulting_velocity == 0 ) {
            veh->velocity = 0;
            return;
        }

        veh->skidding = true;
        veh->move.init( point_rel_ms( final_velocity.normalized().as_point() ) );
        veh->velocity = resulting_velocity;
        return;
    }

    const auto final_velocity =
        veh->velo_vec() + face_velocity_vec * recoil_velocity_vec.dot_product( face_velocity_vec )
        + lateral_velocity_vec * recoil_velocity_vec.dot_product( lateral_velocity_vec )
        * vehicle_recoil_lateral_scale;
    const auto face_velocity = final_velocity.dot_product( veh->face_vec() );
    const auto lateral_velocity = final_velocity.dot_product( lateral_velocity_vec );
    const auto should_skid = veh->skidding || std::abs( lateral_velocity ) >= 1.0;
    const auto resulting_velocity = static_cast<int>(
                                        std::round( should_skid ? final_velocity.magnitude() : std::abs( face_velocity ) ) );
    if( resulting_velocity == 0 ) {
        veh->velocity = 0;
        return;
    }

    if( should_skid ) {
        veh->skidding = true;
        veh->move.init( point_rel_ms( final_velocity.normalized().as_point() ) );
    }
    veh->velocity = face_velocity < 0 ? -resulting_velocity : resulting_velocity;
}

} // namespace


dispersion_sources calculate_dispersion(
    const map& m, const Character& who, const item& gun, int at_recoil, bool burst )
{
    const bool bipod = can_use_heavy_weapon( who, m, who.bub_pos() );

    const int gun_recoil = gun.gun_recoil( bipod );
    const int eff_recoil = at_recoil + ( burst ? ranged::burst_penalty( who, gun, gun_recoil ) : 0 );
    dispersion_sources dispersion( ranged::get_weapon_dispersion( who, gun ) );
    dispersion.add_range( eff_recoil );
    return dispersion;
}

static int calc_gun_volume( const item& gun )
{
    // Inherit suppressor modifiers if relevant (e.g. KSG second mag) but still use current ammo
    const item& parent =
        ( gun.parent_item() != nullptr && gun.has_flag( flag_USE_PARENT_GUN ) )
        ? *gun.parent_item()
        : gun;
    int noise = parent.type->gun->loudness;
    for( const auto mod : parent.gunmods() ) { noise += mod->type->gunmod->loudness; }
    if( gun.ammo_data() ) { noise += gun.ammo_data()->ammo->loudness; }

    noise = std::max( noise, 0 );
    return noise;
}

int ranged::fire_gun( Character& who, const tripoint_bub_ms& target, int shots )
{
    return fire_gun( who, target, shots, who.primary_weapon(), nullptr, std::nullopt );
}

int ranged::fire_gun(
    Character& who, const tripoint_bub_ms& target, int max_shots, item& gun, item* ammo,
    const std::optional<tripoint_bub_ms> &shot_origin )
{
    int attack_moves = time_to_attack( who, gun, ammo );
    who.anim_on_attack( target, true ); // sprite recoil away from the target

    if( !gun.is_gun() ) {
        debugmsg( "%s tried to fire non-gun (%s).", who.name, gun.tname() );
        return 0;
    }

    if( gun.ammo_required() > 0 && !gun.ammo_remaining() && !ammo ) {
        debugmsg( "%s's gun %s is empty and has no ammo for reloading.", who.name, gun.tname() );
        return 0;
    }

    bool is_mech_weapon = false;
    if( who.is_mounted() && who.mounted_creature->has_flag( MF_RIDEABLE_MECH ) ) {
        is_mech_weapon = true;
    }

    int shots = max_shots;
    // Number of shots to fire is limited by the amount of remaining ammo
    if( gun.ammo_required() ) {
        const int ammo_left = ammo ? ammo->count() : gun.ammo_remaining();
        shots = std::min( shots, ammo_left / gun.ammo_required() );
    }

    // cap our maximum burst size by the amount of UPS power left
    if( !gun.has_flag( flag_VEHICLE ) && gun.get_gun_ups_drain() > 0 ) {
        shots = std::min( shots, ( who.charges_of( itype_UPS ) / gun.get_gun_ups_drain() ) );
    }

    if( shots <= 0 ) { debugmsg( "Attempted to fire zero or negative shots using %s", gun.tname() ); }

    // Lua iranged can_fire callback: blocks firing before any ammo is consumed
    if( const auto * iranged_cb = gun.type->iranged_callbacks ) {
        if( !iranged_cb->call_can_fire( who, gun ) ) { return 0; }
    }

    std::optional<shape_factory> shape = ranged::get_shape_factory( gun );

    map& here = get_map();
    // Shaped attacks don't allow aiming, so they don't suffer from lack of aim either
    int character_recoil = shape ? recoil_vehicle( who ) : recoil_total( who );
    // Penalty is (intentionally) based off mode shots, not ammo-limited.
    dispersion_sources dispersion =
        calculate_dispersion( here, who, gun, character_recoil, max_shots > 1 );

    bool aoe_attack = gun.gun_skill() == skill_launcher || shape;
    auto aim = target;
    const auto recoil_origin = shot_origin.value_or( who.bub_pos() );
    int curshot = 0;
    int hits = 0; // total shots on target
    const auto shot_count = get_shot_count( gun );
    const auto shot_half_angle = get_shot_half_angle( gun );
    const auto render_multishot =
        !shape && shot_count > 1 && get_option<bool>( "ANIMATION_PROJECTILES" );
    auto projectile_trajectories = std::vector<std::vector<tripoint_bub_ms>> {};
    auto grouped_shot_hits = std::vector<grouped_shot_hit> {};
    while( curshot != shots ) {
        if( !!ammo && !gun.ammo_remaining() ) { gun.reload( get_avatar(), *ammo, 1 ); }
        if( gun.faults.contains( fault_gun_chamber_spent ) && curshot == 0 ) {
            who.moves -= 50;
            gun.faults.erase( fault_gun_chamber_spent );
            who.add_msg_if_player( _( "You cycle your %s manually." ), gun.tname() );
        }

        if( !ranged::handle_gun_damage( who, gun ) ) { break; }

        // If this is a vehicle mounted turret, which vehicle is it mounted on?
        const vehicle* in_veh =
            who.has_effect( effect_on_roof )
            ? veh_pointer_or_null( here.veh_at( who.bub_pos() ) )
            : nullptr;
        projectile projectile = make_gun_projectile( gun );

        // Apply enchantment bonuses to projectile
        int base_bullet_damage = static_cast<int>( projectile.impact.type_damage( DT_BULLET ) );
        int ench_damage_bonus = who.bonus_from_enchantments(
                                    base_bullet_damage, enchant_vals::mod::RANGED_DAMAGE_BULLET, true );
        if( ench_damage_bonus != 0 ) { projectile.impact.add_damage( DT_BULLET, ench_damage_bonus ); }

        int ench_range_bonus =
            who.bonus_from_enchantments( projectile.range, enchant_vals::mod::RANGED_RANGE, true );
        // Ensure range doesn't go below 1
        projectile.range = std::max( 1, projectile.range + ench_range_bonus );

        // Slings use ammo damage or damage from throwing the ammo, whichever is higher
        if( gun.gun_skill() == skill_throw && !who.is_fake() && gun.ammo_data() ) {
            item& tmp = *item::spawn_temporary( item( gun.ammo_data() ) );
            if( throw_damage( tmp, who.get_skill_level( skill_throw ), who.get_str() )
                > gun.ammo_data()->ammo->damage.damage_units.front().amount ) {
                projectile.impact = gun.gun_damage( false );
                auto& impact = projectile.impact;
                impact.add_damage(
                    DT_BASH, throw_damage( tmp, who.get_skill_level( skill_throw ), who.get_str() ) );
            }
        }
        // Damage reduction from insufficient strength, if using a STR_DRAW weapon.
        projectile.impact.mult_damage( ranged::str_draw_damage_modifier( gun, who ) );

        if( who.has_trait( trait_NORANGEDCRIT ) ) { projectile.add_effect( ammo_effect_NO_CRIT ); }
        if( !shape ) {
            projectile_trajectories.clear();
            projectile_trajectories.reserve( shot_count );
            grouped_shot_hits.clear();
            auto animation_suppression = std::optional<scoped_projectile_animation_suppression> {};
            if( render_multishot ) { animation_suppression.emplace(); }
            const auto render_projectile = projectile;
            auto shell_hit = false;
            auto shell_headshot = false;
            const auto shell_target =
                shot_count > 1
            ? get_shot_target( {
                .source = who.bub_pos(),
                .target = aim,
                .proj = projectile,
                .dispersion = dispersion,
            } )
                : aim;
            const auto pellet_dispersion = shot_count > 1 ? dispersion_sources{} :
                                           dispersion;
            for( int projectile_index = 0; projectile_index < shot_count; projectile_index++ ) {
                const auto pellet_target =
                    shot_count > 1
                ? get_pellet_target( {
                    .source = who.bub_pos(),
                    .target = shell_target,
                    .half_angle = shot_half_angle,
                } )
                    : shell_target;
                auto shot = projectile_attack(
                                render_projectile, who.bub_pos(), pellet_target, pellet_dispersion, &who, &gun,
                                in_veh, shot_count > 1 );
                if( render_multishot ) { projectile_trajectories.push_back( shot.trajectory ); }
                if( shot_count > 1 ) { add_grouped_shot_hit( grouped_shot_hits, shot ); }
                shell_hit |= shot.hit_critter != nullptr;
                if( shot.missed_by > .1 || shell_headshot ) { continue; }

                // TODO: check head existence for headshot
                g->events().send<event_type::character_gets_headshot>( who.getID() );
                shell_headshot = true;
            }

            if( render_multishot ) {
                draw_bullet_trajectories( {
                    .trajectories = projectile_trajectories,
                    .bullet = get_projectile_animation_symbol( projectile ),
                    .draw_as_line = projectile_draws_as_line( projectile ),
                    .custom_sprite = {},
                } );
            }
            if( shot_count > 1 ) {
                print_grouped_shot_hit_messages( {
                    .source = who,
                    .gun = gun,
                    .grouped_hits = grouped_shot_hits,
                } );
            }

            if( shell_hit ) { hits++; }
        } else {
            // 30 degree cap, like for projectiles
            double angle_offset_arcmin = std::min( dispersion.roll(), 1800.0 ) * ( one_in( 2 ) ? 1 : -1 );
            double angle_offset = units::to_radians( units::from_arcmin( angle_offset_arcmin ) );
            double dx = aim.x() - who.bub_pos().x();
            double dy = aim.y() - who.bub_pos().y();
            double new_angle = atan2( dy, dx ) + angle_offset;
            // Always using trig here, rotations in maximum metric are weird
            double length = trig_dist( who.bub_pos(), aim );
            rl_vec3d vec_pos( who.bub_pos() );
            rl_vec3d new_aim = vec_pos + rl_vec3d( length, 0, 0 ).rotated( new_angle );
            ranged::execute_shaped_attack(
                *shape->create( vec_pos, new_aim ), projectile, who, &gun, in_veh );
        }
        curshot++;

        int noise = calc_gun_volume( gun );
        if( !who.is_deaf() && noise > 0 ) {
            who.add_msg_if_player(
                m_warning, _( "You fire your %s, %s" ), gun.tname(), gun.gun_noise( shots > 1 ).sound );
        } else {
            who.add_msg_if_player( m_warning, _( "You fire your %s!" ), gun.tname() );
        }
        ranged::make_gun_sound_effect( who, shots > 1, gun );

        cycle_action( gun, who.bub_pos() );

        if( who.has_trait( trait_PYROMANIA ) && !who.has_morale( MORALE_PYROMANIA_STARTFIRE )
            && gun.has_flag( flag_PYROMANIAC_WEAPON ) ) {
            who.add_msg_if_player(
                m_good, _( "You feel a surge of euphoria as flames roar out of the %s!" ),
                gun.tname() );
            who.add_morale( MORALE_PYROMANIA_STARTFIRE, 15, 15, 8_hours, 6_hours );
            who.rem_morale( MORALE_PYROMANIA_NOFIRE );
        }

        if( gun.ammo_consume( gun.ammo_required(), who.bub_pos() ) != gun.ammo_required() ) {
            debugmsg( "Unexpected shortage of ammo whilst firing %s", gun.tname() );
            break;
        }

        if( !gun.has_flag( flag_VEHICLE ) ) { who.use_charges( itype_UPS, gun.get_gun_ups_drain() ); }

        if( aoe_attack ) {
            continue; // skip retargeting for launchers
        }
    }

    if( gun.has_flag( flag_RELOAD_AND_SHOOT ) ) {
        // Reset aim for bows and other reload-and-shoot weapons.
        who.recoil = MAX_RECOIL;
    } else {
        // Hack alert: nearly every other use of can_use_heavy_weapon uses const character, except
        // this one and gunmode_checks_weapon.
        const Character& shooter = who;
        // Now actually apply recoil for the future shots
        // But only for one shot, because bursts kinda suck
        int gun_recoil = gun.gun_recoil( can_use_heavy_weapon( shooter, here, shooter.bub_pos() ) );

        // If user is currently able to fire a mounted gun freely, penalize dispersion
        // HEAVY_WEAPON_SUPPORT flag has highest penalty, Large mutants lower penalty, no penalty
        // for Huge mutants.
        if( gun.has_flag( flag_MOUNTED_GUN )
            && !can_use_heavy_weapon( shooter, here, shooter.bub_pos() ) ) {
            if( who.get_size() == creature_size::large ) {
                gun_recoil = gun_recoil * 2;
            } else if( who.worn_with_flag( flag_HEAVY_WEAPON_SUPPORT )
                       && ( who.get_size() <= creature_size::medium ) ) {
                gun_recoil = gun_recoil * 3;
            }
        }

        apply_gun_recoil_to_vehicle( here, who, target, recoil_origin, gun_recoil, curshot );

        who.recoil += gun_recoil;
        if( is_mech_weapon ) {
            // mechs can handle recoil far better. they are built around their main gun.
            who.recoil = who.recoil / 2;
        }
        who.recoil = std::min( MAX_RECOIL, who.recoil );
    }

    // Use different amounts of time depending on the type of gun and our skill
    who.moves -= attack_moves;

    // Practice the base gun skill proportionally to number of hits, but always by one.
    who.as_player()->practice( skill_gun, ( hits + 1 ) * 5 );
    // launchers train weapon skill for both hits and misses.
    int practice_units = aoe_attack ? curshot : hits;
    who.as_player()->practice( gun.gun_skill(), ( practice_units + 1 ) * 5 );

    // Lua iranged on_fire callback: returns false to zero out hits (force miss)
    if( const auto * iranged_cb = gun.type->iranged_callbacks ) {
        if( !iranged_cb->call_on_fire( who, gun, target, curshot ) ) { hits = 0; }
    }

    cata::run_hooks( "on_shoot", [&]( auto & params ) {
        params["shooter"] = &who;
        params["target_pos"] = cata::detail::lua_coords::to_lua( target );
        params["shots"] = curshot;
        params["gun"] = &gun;
        params["ammo"] = ammo;
    } );
    return curshot;
}

namespace ranged
{

int throw_cost( const Character& c, const item& to_throw )
{
    // Very similar to player::attack_cost
    // TODO: Extract into a function?
    // Differences:
    // Dex is more (2x) important for throwing speed
    // At 10 skill, the cost is down to 0.75%, not 0.66%
    const int base_move_cost = to_throw.attack_cost() / 2;
    const int throw_skill = std::min( MAX_SKILL, c.get_skill_level( skill_throw ) );
    ///\EFFECT_THROW increases throwing speed
    const int skill_cost = ( base_move_cost * ( 20 - throw_skill ) / 20 );
    ///\EFFECT_DEX increases throwing speed
    const int dexbonus = c.get_dex();
    const int encumbrance_penalty =
        c.encumb( body_part_torso ) + ( c.encumb( body_part_hand_l ) + c.encumb( body_part_hand_r ) ) / 2;
    const float stamina_ratio = static_cast<float>( c.get_stamina() ) / c.get_stamina_max();
    const float stamina_penalty = 1.0 + std::max( ( 0.25f - stamina_ratio ) * 4.0f, 0.0f );

    int move_cost = base_move_cost;
    // Stamina penalty only affects base/2 and encumbrance parts of the cost
    move_cost += encumbrance_penalty;
    move_cost *= stamina_penalty;
    move_cost += skill_cost;
    move_cost -= dexbonus;
    move_cost *= c.mutation_value( "attackcost_modifier" );

    return std::max( 25, move_cost );
}

float get_str_draw_penalty( const item& it, const Character& p )
{
    // Grab the archer's skill for purposes of strength requirment reduction, if we are using a
    // "gun" Improvement intervals at 4/8/10, max reduction of 3 strength
    const int charskill = p.get_skill_level( it.gun_skill() );
    int str_adjust = 0;
    if( charskill >= 4 ) {
        if( charskill >= 10 ) {
            str_adjust = 3;
        } else if( charskill >= 8 ) {
            str_adjust = 2;
        } else {
            str_adjust = 1;
        }
    }
    // We only care if weapon has STR_DRAW, and that the user is weaker than required strength.
    // Also avoid dividing by zero, and skip if we'd just get a result of 1 anyway.
    if( !it.has_flag( flag_STR_DRAW ) || p.get_str() + str_adjust >= it.get_min_str()
        || it.get_min_str() <= 1 ) {
        return 1.0f;
    }
    // We also don't want to actually reduce values to zero, even if user is debuffed to zero
    // strength.
    float archer_str = std::max( 1, p.get_str() + str_adjust );
    return ( archer_str / it.get_min_str() );
}

float str_draw_damage_modifier( const item& it, const Character& p )
{
    // Grab the archer's skill for purposes of strength requirment reduction, if we are using a
    // "gun" Improvement intervals at 4/8/10, max reduction of 3 strength
    const int charskill = p.get_skill_level( it.gun_skill() );
    int str_adjust = 0;
    if( charskill >= 4 ) {
        if( charskill >= 10 ) {
            str_adjust = 3;
        } else if( charskill >= 8 ) {
            str_adjust = 2;
        } else {
            str_adjust = 1;
        }
    }
    if( !it.has_flag( flag_STR_DRAW ) || p.get_str() + str_adjust >= it.get_min_str()
        || it.get_min_str() <= 1 ) {
        return 1.0f;
    }
    if( ranged::get_str_draw_penalty( it, p ) < 0.75f ) {
        return 0.5f;
    } else if( ranged::get_str_draw_penalty( it, p ) < 1.0f ) {
        return 0.75f;
    } else {
        return 1.0f;
    }
}

float str_draw_dispersion_modifier( const item& it, const Character& p )
{
    // Grab the archer's skill for purposes of strength requirment reduction, if we are using a
    // "gun" Improvement intervals at 4/8/10, max reduction of 3 strength
    const int charskill = p.get_skill_level( it.gun_skill() );
    int str_adjust = 0;
    if( charskill >= 4 ) {
        if( charskill >= 10 ) {
            str_adjust = 3;
        } else if( charskill >= 8 ) {
            str_adjust = 2;
        } else {
            str_adjust = 1;
        }
    }
    if( !it.has_flag( flag_STR_DRAW ) || p.get_str() + str_adjust >= it.get_min_str()
        || it.get_min_str() <= 1 ) {
        return 1.0f;
    }
    if( ranged::get_str_draw_penalty( it, p ) < 0.75f ) {
        return 0.5f;
    } else {
        return 1.0f;
    }
}

float str_draw_range_modifier( const item& it, const Character& p )
{
    // Grab the archer's skill for purposes of strength requirment reduction, if we are using a
    // "gun" Improvement intervals at 4/8/10, max reduction of 3 strength
    const int charskill = p.get_skill_level( it.gun_skill() );
    int str_adjust = 0;
    if( charskill >= 4 ) {
        if( charskill >= 10 ) {
            str_adjust = 3;
        } else if( charskill >= 8 ) {
            str_adjust = 2;
        } else {
            str_adjust = 1;
        }
    }
    if( !it.has_flag( flag_STR_DRAW ) || p.get_str() + str_adjust >= it.get_min_str()
        || it.get_min_str() <= 1 ) {
        return 1.0f;
    }
    if( ranged::get_str_draw_penalty( it, p ) < 0.75f ) {
        return 0.5f;
    } else if( ranged::get_str_draw_penalty( it, p ) < 1.0f ) {
        return 0.75f;
    } else {
        return 1.0f;
    }
}

int throw_dispersion_per_dodge( const Character& c, bool add_encumbrance )
{
    // +200 per dodge point at 0 dexterity
    // +100 at 8, +80 at 12, +66.6 at 16, +57 at 20, +50 at 24
    // Each 10 encumbrance on either hand is like -1 dex (can bring penalty to +400 per dodge)
    // Maybe TODO: Only use one hand
    const int encumbrance =
        add_encumbrance ? c.encumb( body_part_hand_l ) + c.encumb( body_part_hand_r ) : 0;
    ///\EFFECT_DEX increases throwing accuracy against targets with good dodge stat
    float effective_dex = 2 + c.get_dex() / 4.0f - ( encumbrance ) / 40.0f;
    return static_cast<int>( 100.0f / std::max( 1.0f, effective_dex ) );
}

// Perfect situation gives us 1000 dispersion at lvl 0
// This goes down linearly to 200  dispersion at lvl 10
int throwing_dispersion(
    const Character& c, const item& to_throw, Creature* critter, bool is_blind_throw )
{
    units::mass weight = to_throw.weight();
    units::volume volume = to_throw.volume();
    if( to_throw.count_by_charges() && to_throw.charges > 1 ) {
        weight /= to_throw.charges;
        volume /= to_throw.charges;
    }

    int throw_difficulty = 1000;
    // 1000 penalty for every liter after the first
    // TODO: Except javelin type items
    throw_difficulty += std::max<int>( 0, units::to_milliliter( volume - 1_liter ) );
    // 1 penalty for gram above str*100 grams (at 0 skill)
    ///\EFFECT_STR decreases throwing dispersion when throwing heavy objects
    const int weight_in_gram = units::to_gram( weight );
    throw_difficulty += std::max( 0, weight_in_gram - c.get_str() * 100 );

    // Dispersion from difficult throws goes from 100% at lvl 0 to 20% at lvl 10
    ///\EFFECT_THROW increases throwing accuracy
    const int throw_skill = std::min( MAX_SKILL, c.get_skill_level( skill_throw ) );
    int dispersion = 10 * throw_difficulty / ( 6 * throw_skill + 20 );
    // If the target is a creature, it moves around and ruins aim
    // TODO: Inform projectile functions if the attacker actually aims for the critter or just the
    // tile
    if( critter != nullptr ) {
        // It's easier to dodge at close range (thrower needs to adjust more)
        // Dodge x10 at point blank, x5 at 1 dist, then flat
        float effective_dodge =
            critter->get_dodge() * std::max( 1, 10 - 5 * rl_dist( c.bub_pos(), critter->bub_pos() ) );
        dispersion += throw_dispersion_per_dodge( c, true ) * effective_dodge;
    }
    // 1 perception per 1 eye encumbrance
    ///\EFFECT_PER decreases throwing accuracy penalty from eye encumbrance
    dispersion += std::max( 0, ( c.encumb( body_part_eyes ) - c.get_per() ) * 10 );

    // If throwing blind, we're assuming they mechanically can't achieve the
    // accuracy of a normal throw.
    if( is_blind_throw ) { dispersion *= 4; }

    return std::max( 0, dispersion );
}

namespace
{
auto throw_damage_projectile( const item& it, const int skill, const int str ) -> projectile
{
    const units::mass weight = it.weight();

    projectile proj;
    proj.impact = it.base_damage_thrown();
    proj.speed = std::log2( std::max( 1, skill ) * std::max( 1, str ) );

    const int damage = 0.5 * ( weight / 1_gram / 1000.0 ) * std::pow( proj.speed, 2 );

    proj.impact.add_damage( DT_BASH, damage );

    for( const auto& eff : it.ammo_effects() ) { proj.add_effect( eff ); }

    // add_msg( m_info, "skill_level is %s", skill );
    // add_msg( m_info, "effective_strength is %s", str );
    // add_msg( m_info, "Thrown item weight is %s grams", to_gram( weight ) );

    // add_msg( m_info, "Calculated damage is %s", damage );

    return proj;
}
} // namespace

auto throw_damage( const item& it, const int skill, const int str ) -> int
{
    return throw_damage_projectile( it, skill, str ).impact.total_damage();
}

dealt_projectile_attack throw_item(
    Character& who, const tripoint_bub_ms& target, detached_ptr<item>&& to_throw,
    std::optional<tripoint_bub_ms> blind_throw_from_pos )
{
    item& thrown = *to_throw;

    const int move_cost = ranged::throw_cost( who, thrown );
    who.mod_moves( -move_cost );

    const int throwing_skill = who.get_skill_level( skill_throw );
    const units::volume volume = thrown.volume();
    const units::mass weight = thrown.weight();

    // Previously calculated as 2_gram * std::max( 1, str_cur )
    // using 16_gram normalizes it to 8 str. Same effort expenditure
    // for being able to throw farther.
    const int weight_cost = weight / ( 16_gram );
    const int encumbrance_cost = roll_remainder(
                                     ( who.encumb( body_part_arm_l ) + who.encumb( body_part_arm_r ) ) * 2.0f );
    const int stamina_cost = ( weight_cost + encumbrance_cost - throwing_skill + 50 ) * -1;

    bool throw_assist = false;
    int throw_assist_str = 0;
    if( who.is_mounted() ) {
        monster* mons = who.mounted_creature.get();
        if( mons->mech_str_addition() != 0 ) {
            throw_assist = true;
            throw_assist_str = mons->mech_str_addition();
            mons->use_mech_power( -3 );
        }
    }
    if( !throw_assist ) { who.mod_stamina( stamina_cost ); }

    const skill_id& skill_used = skill_throw;
    int skill_level = std::min( MAX_SKILL, who.get_skill_level( skill_throw ) );
    // if you are lying on the floor, you can't really throw that well
    if( who.has_effect( effect_downed ) ) { skill_level = std::max( 0, skill_level - 5 ); }

    static const std::set<material_id> ferric = {material_id( "iron" ), material_id( "steel" )};
    const bool do_railgun =
        who.has_active_bionic( bio_railgun ) && thrown.made_of_any( ferric ) && !throw_assist;
    const int effective_strength =
        throw_assist ? throw_assist_str
        : do_railgun
        ? who.get_str() * 2
        : who.get_str();

    // We'll be constructing a projectile
    projectile proj = throw_damage_projectile( thrown, skill_level, effective_strength );

    if( thrown.has_flag( flag_ACT_ON_RANGED_HIT ) ) {
        proj.add_effect( ammo_effect_ACT_ON_RANGED_HIT );
        thrown.activate();
    }
    if( thrown.has_flag( flag_BLINDS_EYES_ON_HIT ) ) { proj.add_effect( ammo_effect_BLINDS_EYES ); }
    // This is just to indicate something is a thrown item
    // Checking with other methods downstream breaks other projectile attacks.
    proj.add_effect( ammo_effect_THROWN );

    // Item will shatter upon landing, destroying the item, dealing damage, and making noise
    /** @EFFECT_STR increases chance of shattering thrown glass items (NEGATIVE) */
    const bool shatter =
        !thrown.is_active() && thrown.can_shatter()
        && rng( 0, units::to_milliliter( 2_liter - volume ) ) < who.get_str() * 100;

    // Item will burst upon landing, destroying the item, and spilling its contents
    const bool burst =
        thrown.has_property( "burst_when_filled" ) && thrown.is_container()
        && thrown.get_property_int64_t( "burst_when_filled" )
        <= static_cast<double>( thrown.get_contained().volume().value() )
        / thrown.get_container_capacity().value() * 100;

    // Add some flags to the projectile
    if( weight > 500_gram ) { proj.add_effect( ammo_effect_HEAVY_HIT ); }

    proj.add_effect( ammo_effect_NO_ITEM_DAMAGE );

    if( thrown.is_active() ) {
        // Can't have Molotovs embed into monsters
        // Monsters don't have inventory processing
        proj.add_effect( ammo_effect_NO_EMBED );
    }

    if( do_railgun ) { proj.add_effect( ammo_effect_LIGHTNING ); }

    if( volume > 500_ml ) { proj.add_effect( ammo_effect_WIDE ); }

    // Deal extra cut damage if the item breaks
    if( shatter ) {
        proj.impact.add_damage( DT_CUT, units::to_milliliter( volume ) / 500.0f );
        proj.add_effect( ammo_effect_SHATTER_SELF );
    }

    // TODO: Add wet effect if other things care about that
    if( burst ) { proj.add_effect( ammo_effect_BURST ); }

    // Some minor (skill/2) armor piercing for skillful throws
    // Not as much as in melee, though
    for( damage_unit& du : proj.impact.damage_units ) { du.res_pen += skill_level / 2.0f; }
    // handling for tangling thrown items
    if( thrown.has_flag( flag_TANGLE ) ) { proj.add_effect( ammo_effect_TANGLE ); }
    if( thrown.has_flag( flag_NET_TANGLE ) ) { proj.add_effect( ammo_effect_NET_TANGLE ); }

    if( thrown.has_flag( flag_NO_DAMAGE ) ) { proj.add_effect( ammo_effect_NO_DAMAGE ); }

    Creature* critter = g->critter_at( target, true );
    const dispersion_sources dispersion(
        ranged::throwing_dispersion( who, thrown, critter, blind_throw_from_pos.has_value() ) );
    const itype* thrown_type = thrown.type;

    // Put the item into the projectile
    proj.set_drop( std::move( to_throw ) );
    if( thrown_type->has_flag( flag_CUSTOM_EXPLOSION ) ) {
        proj.set_custom_explosion( thrown_type->explosion );
    }

    // Throw from the player's position, unless we're blind throwing, in which case
    // throw from the the blind throw position instead.
    const auto throw_from = blind_throw_from_pos.value_or( who.bub_pos() );

    float range = rl_dist( throw_from, target );
    proj.range = range;
    int skill_lvl = who.get_skill_level( skill_used );
    // Avoid awarding tons of xp for lucky throws against hard to hit targets
    const float range_factor = std::min<float>( range, skill_lvl + 3 );
    // We're aiming to get a damaging hit, not just an accurate one - reward proper weapons
    const float damage_factor = 5.0f * std::sqrt( proj.impact.total_damage() / 5.0f );
    // This should generally have values below ~20*sqrt(skill_lvl)
    const float final_xp_mult = range_factor * damage_factor;

    auto dealt_attack = projectile_attack( proj, throw_from, target, dispersion, &who );

    const double missed_by = dealt_attack.missed_by;
    if( missed_by <= 0.1 && dealt_attack.hit_critter != nullptr ) {
        who.as_player()->practice( skill_used, final_xp_mult, MAX_SKILL );
        // TODO: Check target for existence of head
        g->events().send<event_type::character_gets_headshot>( who.getID() );
    } else if( dealt_attack.hit_critter != nullptr && missed_by > 0.0f ) {
        who.as_player()->practice( skill_used, final_xp_mult / ( 1.0f + missed_by ), MAX_SKILL );
    } else {
        // Pure grindy practice - cap gain at lvl 2
        who.as_player()->practice( skill_used, 5, 2 );
    }
    // Reset last target pos
    who.last_target_pos = std::nullopt;
    who.recoil = MAX_RECOIL;

    cata::run_hooks( "on_throw", [&]( auto & params ) {
        params["thrower"] = &who;
        params["target_pos"] = cata::detail::lua_coords::to_lua( target );
        params["throw_from_pos"] = cata::detail::lua_coords::to_lua( throw_from );
        params["thrown"] = &thrown;
    } );
    return dealt_attack;
}

} // namespace ranged

void do_aim( avatar& you, const item& relevant, const double min_recoil )
{
    const double aim_amount = ranged::aim_per_move( you, relevant, you.recoil );
    if( aim_amount > 0 && you.recoil > min_recoil ) {
        // Increase aim at the cost of moves
        you.mod_moves( -1 );
        you.recoil = std::max( min_recoil, you.recoil - aim_amount );
    }
}

struct confidence_rating {
    double aim_level;
    char symbol;
    std::string color;
    std::string label;
};

static double confidence_estimate(
    int range, double target_size, const dispersion_sources& dispersion )
{
    // This is a rough estimate of accuracy based on a linear distribution across min and max
    // dispersion.  It is highly inaccurate probability-wise.  The result gives the player
    // correct relative measures of chance to hit, and corresponds with the actual distribution at
    // min, max, and mean.
    if( range == 0 ) { return 2 * target_size; }
    const double max_lateral_offset = iso_tangent( range, units::from_arcmin( dispersion.max() ) );
    return 1 / ( max_lateral_offset / target_size );
}

static std::vector<ranged::aim_type> get_default_aim_type()
{
    std::vector<ranged::aim_type> aim_types;
    aim_types.push_back( ranged::aim_type{"", "", "", false, 0} ); // dummy aim type for unaimed shots
    return aim_types;
}

using RatingVector = std::vector<std::tuple<double, char, std::string>>;
static std::string get_colored_bar(
    const double val, const int width, const std::string& label, RatingVector::iterator begin,
    RatingVector::iterator end )
{
    std::string result;

    result.reserve( width );
    if( !label.empty() ) {
        result += label;
        result += ' ';
    }
    const int bar_width = width - utf8_width( result ) - 2; // - 2 for the brackets

    result += '[';
    if( bar_width > 0 ) {
        int used_width = 0;
        for( auto it( begin ); it != end; ++it ) {
            const double factor = std::min( 1.0, std::max( 0.0, std::get<0>( *it ) * val ) );
            const int seg_width = static_cast<int>( factor * bar_width ) - used_width;

            if( seg_width <= 0 ) { continue; }
            used_width += seg_width;
            result += string_format( "<color_%s>", std::get<2>( *it ) );
            result.insert( result.end(), seg_width, std::get<1>( *it ) );
            result += "</color>";
        }
        result.insert( result.end(), bar_width - used_width, ' ' );
    }
    result += ']';

    return result;
}

// Whether player character knows creature's position and can roughly track it with the aim cursor
auto outside_visible_z_range( const tripoint_bub_ms& from, const tripoint_bub_ms& to )
-> bool
{
    return get_map().has_zlevels() && fov_3d &&
    std::abs( from.z() - to.z() ) > fov_3d_z_range;
}

bool pl_sees( const Creature& cr )
{
    Character& u = get_player_character();
    if( outside_visible_z_range( u.bub_pos(), cr.bub_pos() ) ) { return false; }
    return u.sees( cr ) || u.sees_with_infrared( cr ) || u.sees_with_specials( cr );
}

// Handle capping aim level when the player cannot see the target tile or there is nothing to aim
// at.
double calculate_aim_cap( const Character& p, const tripoint_bub_ms& target )
{
    double min_recoil = 0.0;
    const Creature* victim = g->critter_at( target, true );
    // No p.sees_with_specials() here because special senses are not precise enough
    // to give creature's exact size & position, only which tile it occupies
    if( victim == nullptr || ( !p.sees( *victim ) && !p.sees_with_infrared( *victim ) ) ) {
        const int range = rl_dist( p.bub_pos(), target );
        // Get angle of triangle that spans the target square.
        const double angle = atan2( 1, range );
        // Convert from radians to arcmin.
        min_recoil = 60 * 180 * angle / M_PI;
    }
    return min_recoil;
}

// --- RmlUi slice 2b: lines-producers for the aim/hit-chance readout. ---
// The sole producers now: they RETURN colour-tagged lines for panel_text() instead
// of drawing into a curses window. (The old curses print_steadiness /
// print_ranged_chance / print_aim / draw_throw_aim were deleted once parity was
// confirmed.) Always the readable wide form (the compact insert_table positional
// layout is dropped) — the displayed NUMBERS match the historical curses readout,
// only the layout is semantic.
static std::string steadiness_line( int bar_width, double steadiness )
{
    if( get_option<std::string>( "ACCURACY_DISPLAY" ) == "numbers" ) {
        return string_format( "%s: %d%%", _( "Steadiness" ), static_cast<int>( 100.0 * steadiness ) );
    }
    return get_labeled_bar( steadiness, bar_width, _( "Steadiness" ), '*' );
}

static std::vector<std::string> ranged_chance_lines(
    input_context& ctxt, int bar_width, const std::vector<ranged::aim_type> &aim_types,
    const std::function<dispersion_sources( const ranged::aim_type & )> &dispersion_fun,
    const std::function<int( const ranged::aim_type & )> &cost_fun,
    const std::vector<confidence_rating> &confidence_config, double range, double target_size )
{
    std::vector<std::string> out;
    const std::string display_type = get_option<std::string>( "ACCURACY_DISPLAY" );

    const auto front_or = [&]( const std::string & s, const char fallback ) {
        const auto keys = ctxt.keys_bound_to( s );
        return keys.empty() ? fallback : keys.front();
    };

    // Symbols legend (bars mode only) — mirrors the wide "Symbols:" header line.
    if( display_type != "numbers" ) {
        std::string symbols = _( "Symbols:" );
        for( const confidence_rating& cr : confidence_config ) {
            symbols += string_format(
                           " <color_%s>%s</color> = %s", cr.color, cr.symbol,
                           pgettext( "aim_confidence", cr.label.c_str() ) );
        }
        out.push_back( symbols );
    }

    for( const ranged::aim_type& type : aim_types ) {
        dispersion_sources current_dispersion = dispersion_fun( type );
        std::string label = _( "Current" );
        std::string aim_l = _( "Aim" );
        if( type.has_threshold ) { label = type.name; }

        int moves_to_fire = cost_fun( type );
        auto hotkey = front_or( type.action.empty() ? "FIRE" : type.action, ' ' );

        out.push_back( string_format(
                           _( "<color_white>[%s]</color> %s %s: Moves to fire: "
                              "<color_light_blue>%d</color>" ),
                           hotkey, label, aim_l, moves_to_fire ) );

        double confidence = confidence_estimate( range, target_size, current_dispersion );

        if( display_type == "numbers" ) {
            int last_chance = 0;
            std::string confidence_s = enumerate_as_string(
                                           confidence_config.begin(), confidence_config.end(),
            [&]( const confidence_rating & config ) {
                int chance =
                    std::min<int>( 100, 100.0 * ( config.aim_level * confidence ) ) - last_chance;
                last_chance += chance;
                return string_format(
                           "%s: <color_%s>%3d%%</color>",
                           pgettext( "aim_confidence", config.label.c_str() ), config.color, chance );
            },
            enumeration_conjunction::none );
            out.push_back( confidence_s );
        } else {
            std::vector<std::tuple<double, char, std::string>> confidence_ratings;
            std::transform(
                confidence_config.begin(), confidence_config.end(),
            std::back_inserter( confidence_ratings ), [&]( const confidence_rating & config ) {
                return std::make_tuple( config.aim_level, config.symbol, config.color );
            } );
            out.push_back( get_colored_bar(
                               confidence, bar_width, "", confidence_ratings.begin(), confidence_ratings.end() ) );
        }
    }
    return out;
}

std::vector<std::string> aim_lines(
    const Character& p, int bar_width, input_context& ctxt, item& weapon, const double target_size,
    const tripoint_bub_ms& pos, double predicted_recoil, item* load_loc )
{
    dispersion_sources dispersion = ranged::get_weapon_dispersion( p, weapon );
    dispersion.add_range( ranged::recoil_vehicle( p ) );

    const double min_recoil = calculate_aim_cap( p, pos );
    const double effective_recoil =
        ranged::effective_dispersion( p, p.primary_weapon().sight_dispersion() );
    const double min_dispersion = std::max( min_recoil, effective_recoil );
    const double steadiness_range = MAX_RECOIL - min_dispersion;
    const double steady_score = std::max( 0.0, predicted_recoil - min_dispersion );
    const double steadiness = 1.0 - ( steady_score / steadiness_range );

    static const std::vector<confidence_rating> confidence_config = {
        {   {accuracy_critical, '*', "green", translate_marker_context( "aim_confidence", "Great" )},
            {
                accuracy_standard, '+', "light_gray",
                translate_marker_context( "aim_confidence", "Normal" )
            },
            {accuracy_grazing, '|', "magenta", translate_marker_context( "aim_confidence", "Graze" )}
        }
    };

    int shots = std::max( 1, weapon.gun_current_mode().qty );
    const auto dispersion_fun = [&]( const ranged::aim_type & at ) {
        int at_recoil = at.has_threshold ? at.threshold : static_cast<int>( predicted_recoil );
        return calculate_dispersion( get_map(), p, weapon, at_recoil, shots > 1 );
    };
    const auto cost_fun = [&]( const ranged::aim_type & at ) {
        int at_recoil = at.has_threshold ? at.threshold : static_cast<int>( predicted_recoil );
        return ranged::gun_engagement_moves( p, weapon, at_recoil, p.recoil )
               + ranged::time_to_attack( p, weapon, load_loc );
    };
    const double range = rl_dist( p.bub_pos(), pos );

    std::vector<std::string> out;
    out.push_back( steadiness_line( bar_width, steadiness ) );
    std::vector<std::string> chance = ranged_chance_lines( ctxt, bar_width,
                                      ranged::get_aim_types( p, weapon ), dispersion_fun, cost_fun,
                                      confidence_config, range, target_size );
    out.insert( out.end(), chance.begin(), chance.end() );
    return out;
}

std::vector<std::string> throw_aim_lines(
    const player& p, int bar_width, input_context& ctxt, const item& weapon,
    const tripoint_bub_ms& target_pos, bool is_blind_throw )
{
    Creature* target = g->critter_at( target_pos, true );
    if( target != nullptr && !p.sees( *target ) ) { target = nullptr; }

    const dispersion_sources dispersion(
        ranged::throwing_dispersion( p, weapon, target, is_blind_throw ) );
    const double range = rl_dist( p.bub_pos(), target_pos );
    const double target_size = target != nullptr ? target->ranged_target_size() : 1.0f;

    static const std::vector<confidence_rating> confidence_config_critter = {
        {   {accuracy_critical, '*', "green", translate_marker_context( "aim_confidence", "Great" )},
            {
                accuracy_standard, '+', "light_gray",
                translate_marker_context( "aim_confidence", "Normal" )
            },
            {accuracy_grazing, '|', "magenta", translate_marker_context( "aim_confidence", "Graze" )}
        }
    };
    static const std::vector<confidence_rating> confidence_config_object = {
        {{accuracy_grazing, '*', "white", translate_marker_context( "aim_confidence", "Hit" )}}
    };
    const auto& confidence_config =
        target != nullptr ? confidence_config_critter : confidence_config_object;

    const auto dispersion_fun = [&]( const ranged::aim_type & ) { return dispersion; };
    const auto cost_fun = [&]( const ranged::aim_type & ) { return ranged::throw_cost( p, weapon ); };
    return ranged_chance_lines(
               ctxt, bar_width, get_default_aim_type(), dispersion_fun, cost_fun, confidence_config, range,
               target_size );
}

std::vector<ranged::aim_type> ranged::get_aim_types( const Character& who, const item& gun )
{
    std::vector<aim_type> aim_types = get_default_aim_type();
    if( !gun.is_gun() ) { return aim_types; }
    int sight_dispersion = effective_dispersion( who, gun.sight_dispersion() );
    // Aiming thresholds are dependent on weapon sight dispersion, attempting to place thresholds
    // at 10%, 5% and 0% of the difference between MAX_RECOIL and sight dispersion.
    std::vector<int> thresholds = {
        static_cast<int>( ( ( MAX_RECOIL - sight_dispersion ) / 10.0 ) + sight_dispersion ),
        static_cast<int>( ( ( MAX_RECOIL - sight_dispersion ) / 20.0 ) + sight_dispersion ),
        sight_dispersion
    };
    // Remove duplicate thresholds.
    std::vector<int>::iterator thresholds_it = std::adjacent_find( thresholds.begin(),
        thresholds.end() );
    while( thresholds_it != thresholds.end() ) {
        thresholds.erase( thresholds_it );
        thresholds_it = std::adjacent_find( thresholds.begin(), thresholds.end() );
    }
    thresholds_it = thresholds.begin();
    aim_types.push_back(
        aim_type{_( "Regular" ), "AIMED_SHOT", _( "[%c] to aim and fire." ), true, *thresholds_it} );
    thresholds_it++;
    if( thresholds_it != thresholds.end() ) {
        aim_types.push_back(
            aim_type{_( "Careful" ), "CAREFUL_SHOT", _( "[%c] to take careful aim and fire." ), true,
                     *thresholds_it} );
        thresholds_it++;
    }
    if( thresholds_it != thresholds.end() ) {
        aim_types.push_back(
            aim_type{_( "Precise" ), "PRECISE_SHOT", _( "[%c] to take precise aim and fire." ), true,
                     *thresholds_it} );
    }
    return aim_types;
}

projectile make_gun_projectile( const item& gun )
{
    projectile proj;
    proj.speed = gun.gun_speed();
    proj.impact = gun.gun_damage();
    proj.range = gun.gun_range();
    proj.aimedcritbonus = gun.gun_aimed_crit_bonus();
    proj.aimedcritmaxbonus = gun.gun_aimed_crit_max_bonus();
    for( const ammo_effect_str_id& ae_id : gun.ammo_effects() ) { proj.add_effect( ae_id ); }

    auto& fx = proj;

    if( ( gun.ammo_data() && gun.ammo_data()->phase == LIQUID ) || fx.has_effect( ammo_effect_SHOT )
        || fx.has_effect( ammo_effect_BOUNCE ) ) {
        fx.add_effect( ammo_effect_WIDE );
    }

    if( gun.ammo_data() ) {
        assert( gun.ammo_data()->ammo );
        const islot_ammo& ammo = *gun.ammo_data()->ammo;
        if( gun.ammo_data()->has_flag( flag_BLINDS_EYES_ON_HIT ) ) {
            fx.add_effect( ammo_effect_BLINDS_EYES );
        }
        // Some projectiles have a chance of being recoverable
        bool recover = !one_in( ammo.dont_recover_one_in );
        // Some weapons can override this
        recover = recover && !gun.has_flag( flag_NO_RECOVER_AMMO );

        if( recover && !fx.has_effect( ammo_effect_IGNITE )
            && !fx.has_effect( ammo_effect_EXPLOSIVE ) ) {
            detached_ptr<item> drop = item::spawn( gun.ammo_current(), calendar::turn, 1 );
            if( fx.has_effect( ammo_effect_ACT_ON_RANGED_HIT ) ) { drop->activate(); }
            proj.set_drop( std::move( drop ) );
        }

        if( ammo.drop ) {
            detached_ptr<item> drop = item::spawn( ammo.drop, calendar::turn, ammo.drop_count );
            if( ammo.drop_active ) { drop->activate(); }
            proj.set_drop( std::move( drop ) );
        }

        if( fx.has_effect( ammo_effect_CUSTOM_EXPLOSION ) ) {
            proj.set_custom_explosion( gun.ammo_data()->explosion );
        }
    }

    return proj;
}

int ranged::time_to_attack( const Character& p, const item& firing, const item* loc )
{
    const skill_id& skill_used = firing.type->gun->skill_used;
    const time_info_t &info = skill_used->time_to_attack();
    int RAS_time = 0;
    if( !loc ) {
        RAS_time = 0;
    } else {
        // RAS Weapon, calculate move cost.
        // At low stamina levels, firing starts getting slow.
        const int sta_percent = ( 100 * p.get_stamina() ) / p.get_stamina_max();
        const int reload_stamina_penalty = ( sta_percent < 25 ) ? ( ( 25 - sta_percent ) * 2 ) : 0;
        item_reload_option opt = item_reload_option(
                                     p.as_player(), const_cast<item*>( &firing ), &firing, *const_cast<item*>( loc ) );
        RAS_time = opt.moves() + reload_stamina_penalty;
    }
    int base_time = std::max(
                        info.min_time,
                        info.base_time - info.time_reduction_per_level * p.get_skill_level( skill_used ) + RAS_time );
    // Apply enchantment bonus to reload time
    int ench_reload_bonus =
        p.bonus_from_enchantments( base_time, enchant_vals::mod::RANGED_RELOAD_TIME, true );
    // Ensure we don't go below minimum time even with enchantments
    return std::max( info.min_time, base_time + ench_reload_bonus );
}

static void cycle_action( item& weap, const tripoint_bub_ms& pos )
{
    map& here = get_map();
    // eject casings and linkages in random direction avoiding walls using player position as
    // fallback
    std::vector<tripoint_bub_ms> tiles = closest_points_first( pos, 1 );
    tiles.erase( tiles.begin() );
    tiles.erase(
        std::remove_if(
            tiles.begin(), tiles.end(),
    [&pos, &here]( const tripoint_bub_ms & e ) {
        return !here.passable( e ) || here.obstructed_by_vehicle_rotation( pos, e );
    } ),
    tiles.end() );
    auto eject = tiles.empty() ? pos : random_entry( tiles );

    // for turrets try and drop casings or linkages directly to any CARGO part on the same tile
    const optional_vpart_position vp = here.veh_at( pos );
    std::vector<vehicle_part *> cargo;
    if( vp && weap.has_flag( flag_VEHICLE ) ) {
        cargo = vp->vehicle().get_parts_at( tripoint_bub_ms( pos ), "CARGO", part_status_flag::any );
    }

    item& parent =
        ( weap.parent_item() != nullptr && weap.has_flag( flag_USE_PARENT_GUN ) )
        ? *weap.parent_item()
        : weap;
    if( weap.ammo_data() && weap.ammo_data()->ammo->casing ) {
        const itype_id casing = *weap.ammo_data()->ammo->casing;
        if( parent.has_flag( flag_RELOAD_EJECT ) || gunmod_find_with( parent, []( auto * e ) -> bool {
        return e->has_flag( flag_BRASS_CATCHER );
    } ) ) {
            detached_ptr<item> det = item::spawn( casing );
            det->set_flag( flag_CASING );
            parent.put_in( std::move( det ) );
        } else {
            if( cargo.empty() ) {
                here.add_item_or_charges( eject, item::spawn( casing ) );
            } else {
                vp->vehicle().add_item( *cargo.front(), item::spawn( casing ) );
            }

            sfx::play_variant_sound(
                "fire_gun", "brass_eject", sfx::get_heard_volume( eject ),
                sfx::get_heard_angle( eject ), sfx::get_heard_distance( eject ) );
        }
    }

    // some magazines also eject disintegrating linkages
    const auto mag = weap.magazine_current();
    if( mag && mag->type->magazine->linkage ) {
        detached_ptr<item> linkage = item::spawn( *mag->type->magazine->linkage, calendar::turn, 1 );
        if( gunmod_find_with( parent, []( auto * e ) -> bool {
        return e->has_flag( flag_BRASS_CATCHER );
    } ) ) {
            linkage->set_flag( flag_CASING );
            parent.put_in( std::move( linkage ) );
        } else if( cargo.empty() ) {
            here.add_item_or_charges( eject, std::move( linkage ) );
        } else {
            vp->vehicle().add_item( *cargo.front(), std::move( linkage ) );
        }
    }
}

void ranged::make_gun_sound_effect( const Character& who, bool burst, const item& gun )
{
    const item::sound_data data = gun.gun_noise( burst );
    if( data.volume > 0 ) {
        sounds::sound( who.bub_pos(), data.volume, sounds::sound_t::combat,
                       data.sound.empty() ? _( "Bang!" ) : data.sound );
    }
    sfx::generate_gun_sound( who.bub_pos(), gun );
    sfx::emit_sound_pulse( who.bub_pos(), 20.0f );
}

item::sound_data item::gun_noise( const bool burst ) const
{
    if( !is_gun() ) {
    return { 0, "" };
}

int noise = calc_gun_volume( *this );

if( type->weapon_category.contains( weapon_cat_WATER_CANNONS ) ) {
    return { noise, _( "Splash!" ) };

} else if( type->weapon_category.contains( weapon_cat_MAGNETIC ) ) {
    if( noise < 20 ) {
            return { noise, burst ? _( "tz-tz-tzk!" ) : _( "tzk!" ) };
        } else if( noise < 80 ) {
            return {noise, burst ? _( "Brzzip!" ) : _( "tz-Zing!" )};
        } else if( noise < 200 ) {
            return {noise, burst ? _( "tzz-CR-CR-CRAck!" ) : _( "tz-CRACKck!" )};
        } else {
            return {noise, burst ? _( "tzz-BOOOM!" ) : _( "tzk-BLAM!" )};
        }

    } else if( type->weapon_category.contains( weapon_cat_PNEUMATIC ) ) {
    if( noise < 10 ) {
            return { noise, burst ? _( "P-p-p-pft!" ) : _( "pft!" ) };
        } else if( noise < 20 ) {
            return {noise, burst ? _( "F-F-Foomp!" ) : _( "Foomp!" )};
        } else if( noise < 40 ) {
            return {noise, burst ? _( "Thk-Thk-Thunk!" ) : _( "Thunk!" )};
        } else {
            return {noise, burst ? _( "Chuk-chunk!" ) : _( "Chunk!" )};
        }

    } else if( type->weapon_category.contains( weapon_cat_ROCKET_LAUNCHERS ) ) {
    return { noise, _( "Fwsss!" ) };
} else if( type->weapon_category.contains( weapon_cat_GRENADE_LAUNCHERS ) ) {
    return { noise, _( "Thump!" ) };
} else if( type->weapon_category.contains( weapon_cat_FLAMETHROWERS ) ||
               type->weapon_category.contains( weapon_cat_SPRAY_GUNS ) ) {
    return { noise, _( "Fwoosh!" ) };
} else if( type->weapon_category.contains( weapon_cat_S_XBOWS ) ||
               type->weapon_category.contains( weapon_cat_M_XBOWS ) ) {
    return { noise, _( "thonk!" ) };
} else if( type->weapon_category.contains( weapon_cat_ELASTIC ) ) {
    return { noise, _( "whizz!" ) };
}

if( type->weapon_category.contains( weapon_cat_ENERGY_WEAPONS ) ) {
    // Lasers and plasma
    if( noise < 20 ) {
            return { noise, _( "Fzzt!" ) };
        } else if( noise < 40 ) {
            return {noise, _( "Pew!" )};
        } else if( noise < 60 ) {
            return {noise, _( "Tsewww!" )};
        } else {
            return {noise, _( "Kra-kow!" )};
        }

        // Default behavior for normal guns without sound class defined.
    } else if( noise > 0 ) {
    if( noise < 50 ) {
            return { noise, burst ? _( "Brrrip!" ) : _( "plink!" ) };
        } else if( noise < 150 ) {
            return {noise, burst ? _( "Brrrap!" ) : _( "bang!" )};
        } else if( noise < 175 ) {
            return {noise, burst ? _( "P-p-p-pow!" ) : _( "blam!" )};
        } else {
            return {noise, burst ? _( "Kaboom!" ) : _( "kerblam!" )};
        }
    }

    return {0, ""}; // silent weapons
}

static double dispersion_from_skill( double skill, double weapon_dispersion )
{
    if( skill >= MAX_SKILL ) { return 0.0; }
    double skill_shortfall = double( MAX_SKILL ) - skill;
    double dispersion_penalty = 3 * skill_shortfall;
    double skill_threshold = 5;
    if( skill >= skill_threshold ) {
        double post_threshold_skill_shortfall = double( MAX_SKILL ) - skill;
        // Lack of mastery multiplies the dispersion of the weapon.
        return dispersion_penalty
               + ( weapon_dispersion * post_threshold_skill_shortfall * 1.25 )
               / ( double( MAX_SKILL ) - skill_threshold );
    }
    // Unskilled shooters suffer greater penalties, still scaling with weapon penalties.
    double pre_threshold_skill_shortfall = skill_threshold - skill;
    dispersion_penalty +=
        weapon_dispersion * ( 1.25 + pre_threshold_skill_shortfall * 3.75 / skill_threshold );

    return dispersion_penalty;
}

// utility functions for projectile_attack
dispersion_sources ranged::get_weapon_dispersion( const Character& who, const item& obj )
{
    int weapon_dispersion = obj.gun_dispersion();
    dispersion_sources dispersion( weapon_dispersion );
    dispersion.add_range( who.ranged_dex_mod() );

    dispersion.add_range( ( who.encumb( body_part_arm_l ) + who.encumb( body_part_arm_r ) ) / 5.0 );

    if( character_funcs::is_driving( who ) ) {
        // get volume of gun (or for auxiliary gunmods the parent gun)
        const item* parent = who.has_item( obj ) ? who.find_parent( obj ) : nullptr;
        const int vol = ( parent ? parent->volume() : obj.volume() ) / 250_ml;

        /** @EFFECT_DRIVING reduces the inaccuracy penalty when using guns whilst driving */
        dispersion.add_range( std::max( vol - who.get_skill_level( skill_driving ), 1 ) * 20 );
    }

    /** @EFFECT_GUN improves usage of accurate weapons and sights */
    double avgSkill =
        static_cast<double>( who.get_skill_level( skill_gun ) + who.get_skill_level( obj.gun_skill() ) )
        / 2.0;
    avgSkill = std::min( avgSkill, static_cast<double>( MAX_SKILL ) );

    dispersion.add_range( dispersion_from_skill( avgSkill, weapon_dispersion ) );

    if( who.has_bionic( bio_targeting ) ) { dispersion.add_multiplier( 0.75 ); }
    // If we're crouched, it's easier to steady our aim.
    if( who.is_crouching() ) { dispersion.add_multiplier( 0.75 ); }

    // Remotely-fired turrets with installed laser designator
    if( who.has_trait( trait_LASER_GUIDED ) ) { dispersion.add_multiplier( 0.25 ); }
    // If using a bow you lack the strength for, increase based on how much weaker shooter is.
    dispersion.add_multiplier( 1 / ranged::str_draw_dispersion_modifier( obj, who ) );

    // Range is effectively four times longer when shooting unflagged/flagged guns underwater/out of
    // water.
    if( who.is_underwater() != obj.has_flag( flag_UNDERWATER_GUN ) ) {
        // Adding dispersion for additional debuff
        dispersion.add_range( 150 );
        dispersion.add_multiplier( 4 );
    }

    // If user is currently able to fire a mounted gun freely, penalize dispersion
    // HEAVY_WEAPON_SUPPORT flag has highest penalty, Large mutants lower penalty, no penalty for
    // Huge mutants.
    if( obj.has_flag( flag_MOUNTED_GUN ) && !can_use_heavy_weapon( who, get_map(), who.bub_pos() ) ) {
        if( who.get_size() == creature_size::large ) {
            dispersion.add_range( 500 );
        } else if( who.worn_with_flag( flag_HEAVY_WEAPON_SUPPORT )
                   && ( who.get_size() <= creature_size::medium ) ) {
            dispersion.add_range( 1000 );
        }
    }

    // Apply enchantment bonus to dispersion
    int base_dispersion = static_cast<int>( dispersion.max() );
    int ench_dispersion_bonus =
        who.bonus_from_enchantments( base_dispersion, enchant_vals::mod::RANGED_DISPERSION, true );
    dispersion.add_range( ench_dispersion_bonus );

    return dispersion;
}

auto npc_ai::best_mode_for_range( const Character& who, const item& firing, int dist )
-> std::pair<gun_mode_id, std::optional<gun_mode>>
{
    const int shots =
        who.is_wielding( firing )
    ? character_funcs::ammo_count_for( who, firing )
    : item_funcs::shots_remaining( who, firing );

    if( !firing.is_gun() || firing.is_gunmod() || shots == 0 ) {
    return std::make_pair( gun_mode_id(), std::nullopt );
    }
    int min_recoil = MAX_RECOIL;
    min_recoil = ranged::get_most_accurate_sight( who, firing );
    int range = static_cast<const npc *>( &who )->confident_shoot_range( firing, min_recoil );

    if( dist > range ) { return std::make_pair( gun_mode_id(), std::nullopt ); }

    const auto gun_mode_cmp =
        []( const std::pair<gun_mode_id, gun_mode> &lhs,
    const std::pair<gun_mode_id, gun_mode> &rhs ) { return lhs.second.qty < rhs.second.qty; };

    const std::map<gun_mode_id, gun_mode> &to_add = firing.gun_all_modes();
    std::vector<std::pair<gun_mode_id, gun_mode>> modes;
    std::copy( to_add.begin(), to_add.end(), std::back_inserter( modes ) );

    erase_if( modes, [&]( const std::pair<gun_mode_id, gun_mode> &e ) {
        const auto& m = e.second;
        return m.melee() || !who.can_use( *m.target ) || m.flags.contains( "NPC_AVOID" )
               || shots < m.qty
               || ( dist > 0
                    && static_cast<const npc *>( &who )->confident_gun_mode_range( m, min_recoil ) < dist );
    } );

    if( modes.empty() ) { return std::make_pair( gun_mode_id(), std::nullopt ); }

    const auto g_mode = std::max_element( modes.begin(), modes.end(), gun_mode_cmp );
    return *g_mode;
}

double npc_ai::gun_value( const Character& who, const item& weap, int ammo )
{
    // TODO: Mods
    // TODO: Allow using a specified type of ammo rather than default or current
    if( !weap.type->gun ) {
        add_msg( m_debug, "%s is not a gun, gun_value set to 0", weap.type->get_id().str() );
        return 0.0;
    }

    if( ammo <= 0 ) {
        add_msg( m_debug, "%s has no ammo, gun_value set to 0", weap.type->get_id().str() );
        return 0.0;
    }

    item& ideal_weapon = *item::spawn_temporary( weap );
    if( !ideal_weapon.ammo_default().is_null() ) {
        ideal_weapon.ammo_set( ideal_weapon.ammo_default(), -1 );
    }

    auto [mode_id, mode_] = npc_ai::best_mode_for_range( who, ideal_weapon, -1 );

    // Doesn't use calculate_dispersion because that requires a map
    // TODO: Turn this into a common function.
    dispersion_sources mode_disp = ranged::get_weapon_dispersion( who, ideal_weapon );
    double total_dispersion = mode_disp.max();
    if( ideal_weapon.ammo_current() ) {
        total_dispersion += ideal_weapon.ammo_current()->ammo->dispersion;
    }
    // Necessary so that ideal_ranged_dps will include the reload cost in calc.
    ideal_weapon.ammo_unset();
    double dps = ideal_weapon.ideal_ranged_dps( who, mode_ );

    // "Medium range" below means 9 tiles, "short range" means 4
    // Those are guarantees (assuming maximum time spent aiming)
    static const std::vector<std::pair<float, float>> dispersion_thresholds = {{
            // Headshots all the time
            {0.0f, 2.0f},
            // Critical at medium range
            {100.0f, 1.8f},
            // Critical at short range or good hit at medium
            {200.0f, 1.6f},
            // OK hits at medium
            {300.0f, 1.45f},
            // Point blank headshots
            {450.0f, 1.30f},
            // OK hits at short
            {700.0f, 1.15f},
            // Glances at medium, criticals at point blank
            {1000.0f, 1.0f},
            // Nothing guaranteed, pure gamble
            {2000.0f, 0.1f},
        }
    };

    float dispersion_factor = multi_lerp( dispersion_thresholds, total_dispersion );

    double gun_value = dps * dispersion_factor;

    add_msg( m_debug, "%s as gun: %.1f total, %.1f dispersion, %.1f dps", weap.type->get_id().str(),
             gun_value, dispersion_factor, dps );
    return std::max( 0.0, gun_value );
}

double ranged::recoil_vehicle( const Character& who )
{
    // TODO: vary penalty dependent upon vehicle part on which player is boarded

    if( who.in_vehicle ) {
        if( const optional_vpart_position vp = get_map().veh_at( who.bub_pos() ) ) {
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            return static_cast<double>( std::abs( vp->vehicle().velocity ) ) * 3 / 100;
        }
    }
    return 0;
}

double ranged::recoil_total( const Character& who )
{
    double base_recoil = who.recoil + recoil_vehicle( who );
    double ench_recoil_bonus =
        who.bonus_from_enchantments( base_recoil, enchant_vals::mod::RANGED_RECOIL );
    // Recoil cannot be negative
    return std::max( 0.0, base_recoil + ench_recoil_bonus );
}

namespace ranged
{

std::vector<Creature *> targetable_creatures( const Character& c, const int range )
{
    return targetable_creatures( c, range, turret_data() );
}

std::vector<Creature *> targetable_creatures(
    const Character& c, const int range, const turret_data& turret )
{
    const vehicle* veh_from_turret = turret ? turret.get_veh() : nullptr;
    return g->get_creatures_if( [&c, range, veh_from_turret]( const Creature & critter ) -> bool {
        const auto shooter_pos = c.bub_pos();
        const auto critter_pos = critter.bub_pos();
        if( std::round( rl_dist_exact( shooter_pos, critter_pos ) ) > range ) { return false; }

        if( outside_visible_z_range( shooter_pos, critter_pos ) ) { return false; }

        // Special case: if range is 1, it's a melee attack.
        // Melee attacks can only target on same z-level or directly up/down, not "z-diagonally".
        if( range <= 1 && shooter_pos.z() != critter_pos.z()
            && shooter_pos.xy() != critter_pos.xy() )
        {
            return false;
        }

        if( !c.sees( critter ) && !c.sees_with_infrared( critter ) ) { return false; }

        // TODO: get rid of fake npcs (pos() check)
        if( &c == &critter || c.bub_pos() == critter.bub_pos()
            || c.attitude_to( critter ) == Attitude::A_FRIENDLY )
        {
            return false;
        }

        map& here = get_map();

        // TODO: It should use projectile passability checks when finding path, not vision checks.
        std::vector<tripoint_bub_ms> path = here.find_clear_path( shooter_pos, critter_pos );
        auto prev_point = shooter_pos;
        for( const tripoint_bub_ms& point : path )
        {
            if( here.obstructed_by_vehicle_rotation( prev_point, point ) ) {
                // Blocked by a rotated vehicle's walls
                return false;
            }

            prev_point = point;

            if( here.passable( point ) ) {
                // If it's passable, it doesn't block bullets
                continue;
            }

            const vehicle* veh_at_point = veh_pointer_or_null( here.veh_at( point ) );
            if( veh_at_point && veh_at_point != veh_from_turret ) {
                // Vehicles don't have impassable-but-shootable-through parts
                return false;
            }
            if( !here.has_flag_ter( TFLAG_TRANSPARENT, point ) ) {
                // If it's transparent, it's either glass (fine) or reinforced glass (not fine)
                // Hack it with the more common case for now
                // TODO: Handle armored glass
                return false;
            }
        }

        return true;
    } );
}

int burst_penalty( const Character& p, const item& gun, int gun_recoil )
{
    ///\EFFECT_DEX reduces burst penalty by flat amount
    int dex_effect = p.get_dex() * 10;
    ///\EFFECT_STR reduces burst fire penalty
    float str_effect = p.get_str() * 0.5f;

    /** @EFFECT_PISTOL reduces burst fire penalty */
    /** @EFFECT_SMG reduces burst fire penalty */
    /** @EFFECT_RIFLE reduces burst fire penalty */
    /** @EFFECT_SHOTGUN reduces burst fire penalty */
    int skill_lvl = std::min( p.get_skill_level( gun.gun_skill() ), MAX_SKILL );

    return std::max<int>( 0, 3 * ( gun_recoil - dex_effect ) / std::max( 1.0f,
                          str_effect + skill_lvl ) );
}

} // namespace ranged

// ── RmlUi render path (full UI→RmlUi migration, ranged piece 2) ──────────────
// The w_target targeting panel, rendered as one colour-tagged `body_rml` string via
// target_ui::panel_text(): the shallow sections (title/cursor/gun/recoil/spell/
// target/turret/controls, slice 2a) plus the aim/hit-chance readout (slice 2b, via
// ranged_chance_lines / aim_lines / throw_aim_lines). Render-only: the keyboard
// aim/fire loop is unchanged, and the map aim overlay (draw_terrain_overlay) stays
// on the sprite path. The curses panel_* / print_* draw fns have been deleted.
bool &ranged_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

bool ranged::gunmode_checks_common(
    avatar& you, const map& m, std::vector<std::string> &messages, const gun_mode& gmode )
{
    bool result = true;

    // Check that passed gun mode is valid and we are able to use it
    if( !( gmode && you.can_use( *gmode ) ) ) {
        messages.push_back( string_format( _( "You can't currently fire your %s." ), gmode->tname() ) );
        result = false;
    }

    const optional_vpart_position vp = m.veh_at( you.bub_pos() );
    if( vp && vp->vehicle().player_in_control( you )
        && ( gmode->is_two_handed( you ) || gmode->has_flag( flag_FIRE_TWOHAND ) ) ) {

        const auto vp_control = vp->part_with_feature( "CONTROLS", true );
        const bool ctrl_handsfree = vp_control && vp_control->has_feature( "CONTROL_WITHOUT_HANDS" );
        const bool using_arms = vp->vehicle().has_part( "MUSCLE_ARMS", true );
        const bool single_tile_veh = vp->vehicle().all_parts_at_location( "structure" ).size() < 2;

        if( ctrl_handsfree ) { // check this vehicle is steerable and able to be controlled without
            // hands.
            if( single_tile_veh ) {
                if( using_arms ) {
                    messages.push_back( string_format(
                                            _( "You can't fire your %s while driving; this vehicle is hand-powered." ),
                                            gmode->tname() ) );
                    result = false;
                } else if( you.get_skill_level( skill_driving ) < 3 ) {
                    messages.push_back( string_format(
                                            _( "Your driving skill isn't high enough to fire your %s while driving." ),
                                            gmode->tname() ) );
                    result = false;
                } else {
                    result = true;
                }
            } else { // its ctrl is handsfree, but the vehicle is too big.
                messages.push_back( string_format(
                                        _( "You can't fire your %s while driving; this vehicle is too large to handle." ),
                                        gmode->tname() ) );
                result = false;
            }
        } else { // You are driving with your own hands!
            messages.push_back(
                string_format( _( "You can't fire your %s while driving." ), gmode->tname() ) );
            result = false;
        }
    }

    if( gmode->has_flag( flag_FIRE_TWOHAND )
        && ( !you.has_two_arms() || you.worn_with_flag( flag_RESTRICT_HANDS ) ) ) {
        messages.push_back(
            string_format( _( "You need two free hands to fire your %s." ), gmode->tname() ) );
        result = false;
    }

    if( ranged::get_str_draw_penalty( *gmode, you ) < 0.5f ) {
        messages.push_back(
            string_format( _( "You don't have enough strength to fire your %s." ), gmode->tname() ) );
        result = false;
    }

    return result;
}

auto ranged::gunmode_checks_weapon(
    avatar& you, const map& m, std::vector<std::string> &messages, const gun_mode& gmode ) -> bool
{
    bool result = true;

    if( !gmode->ammo_sufficient() && !gmode->has_flag( flag_RELOAD_AND_SHOOT ) ) {
        if( !gmode->ammo_remaining() ) {
            messages.push_back( string_format( _( "Your %s is empty!" ), gmode->tname() ) );
        } else {
            messages.push_back( string_format(
                                    _( "Your %s needs %i charges to fire!" ), gmode->tname(), gmode->ammo_required() ) );
        }
        result = false;
    }

    if( gmode->get_gun_ups_drain() > 0 ) {
        const int ups_drain = gmode->get_gun_ups_drain();
        const int adv_ups_drain = std::max( 1, ups_drain / 2 );
        bool is_mech_weapon = false;
        if( you.is_mounted() ) {
            monster* mons = get_player_character().mounted_creature.get();
            if( !mons->type->mech_weapon.is_empty() ) { is_mech_weapon = true; }
        }
        if( !is_mech_weapon ) {
            if( !( you.has_charges( itype_UPS, ups_drain )
                   || ( you.has_active_bionic( bio_ups )
                        && you.get_power_level() >= units::from_kilojoule( ups_drain ) ) ) ) {
                messages.push_back( string_format(
                                        _( "You need a UPS with at least %2$d charges or an advanced UPS with at least "
                                           "%3$d charges to fire the %1$s!" ),
                                        gmode->tname(), ups_drain, adv_ups_drain ) );
                result = false;
            }
        } else {
            if( !you.has_charges( itype_UPS, ups_drain ) ) {
                messages.push_back( string_format(
                                        _( "Your mech has an empty battery, its %s will not fire." ), gmode->tname() ) );
                result = false;
            }
        }
    }

    if( gmode->has_flag( flag_MOUNTED_GUN ) ) {
        const Character& shooter = you;
        if( !can_use_heavy_weapon( shooter, m, shooter.bub_pos() )
            && !( you.get_size() > creature_size::medium )
            && !you.worn_with_flag( flag_HEAVY_WEAPON_SUPPORT ) ) {
            messages.push_back( string_format(
                                    _( "You must stand near acceptable terrain or furniture to fire the %s.  A table, a "
                                       "mound of dirt, a broken window, etc." ),
                                    gmode->tname() ) );
            result = false;
        }
    }

    return result;
}

void ranged::prompt_select_default_ammo_for( avatar& u, item& w )
{
    item_reload_option opt = character_funcs::select_ammo( u, w, false, true, true );
    if( opt ) {
        if( u.ammo_location && opt.ammo == &*u.ammo_location ) {
            u.add_msg_if_player( _( "Cleared ammo preferences for %s." ), w.tname() );
            u.ammo_location = nullptr;
        } else if( u.has_item( *opt.ammo ) ) {
            u.add_msg_if_player(
                _( "Selected %s as default ammo for %s." ), opt.ammo->tname(), w.tname() );
            u.ammo_location = opt.ammo;
        } else {
            u.add_msg_if_player( _( "You don't have that ammo on you." ) );
        }
    }
}

int ranged::effective_dispersion( const Character& who, int dispersion )
{
    /** @EFFECT_PER penalizes sight dispersion when low. */
    dispersion += who.ranged_per_mod();

    dispersion += who.encumb( body_part_eyes ) / 2;

    return std::max( dispersion, 0 );
}

std::pair<int, int> ranged::get_fastest_sight(
    const Character& who, const item& gun, double recoil )
{
    // Get fastest sight that can be used to improve aim further below @ref recoil.
    int sight_speed_modifier = INT_MIN;
    int limit = 0;
    if( effective_dispersion( who, gun.type->gun->sight_dispersion ) < recoil ) {
        sight_speed_modifier = gun.has_flag( flag_DISABLE_SIGHTS ) ? 0 : 6;
        limit = effective_dispersion( who, gun.type->gun->sight_dispersion );
    }

    for( const auto e : gun.gunmods() ) {
        const islot_gunmod& mod = *e->type->gunmod;
        if( mod.sight_dispersion < 0 || mod.aim_speed < 0 ) {
            continue; // skip gunmods which don't provide a sight
        }
        if( effective_dispersion( who, mod.sight_dispersion ) < recoil
            && mod.aim_speed > sight_speed_modifier ) {
            sight_speed_modifier = mod.aim_speed;
            limit = effective_dispersion( who, mod.sight_dispersion );
        }
    }
    return std::make_pair( sight_speed_modifier, limit );
}

int ranged::get_most_accurate_sight( const Character& who, const item& gun )
{
    if( !gun.is_gun() ) { return 0; }

    int limit = effective_dispersion( who, gun.type->gun->sight_dispersion );
    for( const auto e : gun.gunmods() ) {
        const islot_gunmod& mod = *e->type->gunmod;
        if( mod.aim_speed >= 0 ) {
            limit = std::min( limit, effective_dispersion( who, mod.sight_dispersion ) );
        }
    }

    return limit;
}

double ranged::aim_speed_skill_modifier( const Character& who, const skill_id& gun_skill )
{
    double skill_mult = 1.0;
    if( gun_skill == skill_pistol ) {
        skill_mult = 2.0;
    } else if( gun_skill == skill_rifle ) {
        skill_mult = 0.9;
    }
    /** @EFFECT_PISTOL increases aiming speed for pistols */
    /** @EFFECT_SMG increases aiming speed for SMGs */
    /** @EFFECT_RIFLE increases aiming speed for rifles */
    /** @EFFECT_SHOTGUN increases aiming speed for shotguns */
    /** @EFFECT_LAUNCHER increases aiming speed for launchers */
    return skill_mult * std::min( MAX_SKILL, who.get_skill_level( gun_skill ) );
}

double ranged::aim_speed_dex_modifier( const Character& who ) { return who.get_dex() - 8; }

double ranged::aim_speed_encumbrance_modifier( const Character& who )
{
    return ( who.encumb( body_part_hand_l ) + who.encumb( body_part_hand_r ) ) / 10.0;
}

double ranged::aim_multiplier_from_volume( const item& gun )
{
    // Penalty scales linearly as volume increases
    // 1 liters = multiplier of 0.9
    // 5 liters = multiplier of 0.5
    // Return early with maximum penalty of 0.1 at 9 liters and above.
    if( gun.volume() >= 9_liter ) { return 0.1f; }
    double aim_mult = ( 10000.0 - static_cast<float>( gun.volume() / 1_ml ) ) / 10000.0;
    // TODO: also scale with skill level?
    return aim_mult;
}

double ranged::aim_per_move( const Character& who, const item& gun, double recoil )
{
    if( !gun.is_gun() ) { return 0.0; }

    std::pair<int, int> best_sight = get_fastest_sight( who, gun, recoil );
    int sight_speed_modifier = best_sight.first;
    int limit = best_sight.second;
    if( sight_speed_modifier == INT_MIN ) {
        // No suitable sights (already at maximum aim).
        return 0;
    }

    // Overall strategy for determining aim speed is to sum the factors that contribute to it,
    // then scale that speed by current recoil level.
    // Player capabilities make aiming faster, and aim speed slows down as it approaches 0.
    // Base speed is non-zero to prevent extreme rate changes as aim speed approaches 0.
    double aim_speed = 10.0;

    skill_id gun_skill = gun.gun_skill();
    // Ranges [0 - 10]
    aim_speed += aim_speed_skill_modifier( who, gun_skill );

    // Range [0 - 12]
    /** @EFFECT_DEX increases aiming speed */
    aim_speed += aim_speed_dex_modifier( who );

    // Range [0 - 10]
    aim_speed += sight_speed_modifier;

    // Each 5 points (combined) of hand encumbrance decreases aim speed by one unit.
    aim_speed -= aim_speed_encumbrance_modifier( who );

    aim_speed *= aim_multiplier_from_volume( gun );

    // Just a raw scaling factor.
    aim_speed *= 6.5;

    // Scale rate logistically as recoil goes from MAX_RECOIL to 0.
    aim_speed *= 1.0 - logarithmic_range( 0, MAX_RECOIL, recoil );

    // Minimum improvement is 5MoA.  This mostly puts a cap on how long aiming for sniping takes.
    aim_speed = std::max( aim_speed, 5.0 );
    // Apply enchantment bonus to aim speed

    double ench_aim_bonus =
        who.bonus_from_enchantments( aim_speed, enchant_vals::mod::RANGED_AIM_SPEED );

    // To prevent a bug where aiming does not proceed at all because the aiming speed drops below
    // the game's minimum limit (5.0) due to debuffs (such as Cursed Artifacts), so applying the max
    // value once more.
    aim_speed = std::max( 5.0, aim_speed + ench_aim_bonus );
    // Never improve by more than the currently used sights permit.
    return std::min( aim_speed, recoil - limit );
}

std::optional<shape_factory> ranged::get_shape_factory( const item& gun )
{
    if( gun.ammo_current() && gun.ammo_current()->ammo ) { return gun.ammo_current()->ammo->shape; }

    return {};
}

std::optional<shape_factory> ranged::get_target_shape_factory( const item& gun )
{
    const auto shape = get_shape_factory( gun );
    if( shape ) { return shape; }

    const auto* const shot_data = get_shot_data( gun );
    if( shot_data == nullptr || shot_data->count <= 1 || shot_data->half_angle <= 0.0 ) {
        return {};
    }

    const auto preview_shape_impl = std::shared_ptr<shape_factory_impl>(
                                        std::make_shared<cone_factory>( units::from_degrees( shot_data->half_angle ), gun.gun_range() ) );
    const auto preview_shape = shape_factory( preview_shape_impl );
    return std::optional<shape_factory>( preview_shape );
}
