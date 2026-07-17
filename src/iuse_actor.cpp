#include "iuse_actor.h"

#include "action.h"
#include "active_tile_data_def.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "addiction.h"
#include "ammo.h"
#include "animation.h"
#include "assign.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catalua_hooks.h"
#include "catalua_icallback_actor.h"
#include "catalua_sol.h"
#include "character.h"
#include "character_functions.h"
#include "character_id.h"
#include "cloning_utils.h"
#include "clothing_mod.h"
#include "crafting.h"
#include "creature.h"
#include "debug.h"
#include "dimension_info.h"
#include "effect.h"
#include "enum_conversions.h"
#include "enums.h"
#include "explosion.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "game_inventory.h"
#include "handle_liquid.h"
#include "hsv_color.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_reload_option.h"
#include "itype.h"
#include "json.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "map_utils.h"
#include "mapdata.h"
#include "material.h"
#include "memory_fast.h"
#include "messages.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_special.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "popup.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "rng.h"
#include "skill.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "submap_load_manager.h"
#include "text_snippets.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "ui.h"
#include "uistate.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_selector.h"
#include "visitable.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "world_type.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <ranges>
#include <ret_val.h>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static const activity_id ACT_FIRSTAID( "ACT_FIRSTAID" );
static const activity_id ACT_MAKE_ZLAVE( "ACT_MAKE_ZLAVE" );
static const activity_id ACT_RELOAD( "ACT_RELOAD" );
static const activity_id ACT_REPAIR_ITEM( "ACT_REPAIR_ITEM" );
static const activity_id ACT_SPELLCASTING( "ACT_SPELLCASTING" );
static const activity_id ACT_STUDY_SPELL( "ACT_STUDY_SPELL" );
static const activity_id ACT_START_FIRE( "ACT_START_FIRE" );
static const activity_id ACT_VIBE( "ACT_VIBE" );

static const efftype_id effect_accumulated_mutagen( "accumulated_mutagen" );
static const efftype_id effect_asthma( "asthma" );
static const efftype_id effect_bandaged( "bandaged" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_cig( "cig" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_hallu( "hallu" );
static const efftype_id effect_music( "music" );
static const efftype_id effect_playing_instrument( "playing_instrument" );
static const efftype_id effect_recover( "recover" );
static const efftype_id effect_run( "run" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_visuals( "visuals" );

static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const bionic_id bio_syringe( "bio_syringe" );

static const itype_id itype_barrel_small( "barrel_small" );
static const itype_id itype_brazier( "brazier" );
static const itype_id itype_char_smoker( "char_smoker" );
static const itype_id itype_fire( "fire" );
static const itype_id itype_stock_small( "stock_small" );
static const itype_id itype_syringe( "syringe" );
static const itype_id itype_fertilizer( "fertilizer" );
static const itype_id itype_genome_drive( "genome_drive" );
static const itype_id itype_usb_drive( "usb_drive" );
static const flag_id flag_genome_drive( "GENOME_DRIVE" );
static const itype_id itype_mutagen( "mutagen" );
static const itype_id itype_biomaterial( "biomaterial" );

static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_survival( "survival" );

static const species_id HUMAN( "HUMAN" );
static const species_id ZOMBIE( "ZOMBIE" );

static const trait_id trait_CENOBITE( "CENOBITE" );
static const trait_id trait_DEBUG_BIONICS( "DEBUG_BIONICS" );
static const trait_id trait_TOLERANCE( "TOLERANCE" );
static const trait_id trait_INFRESIST( "INFRESIST" );
static const trait_id trait_LIGHTWEIGHT( "LIGHTWEIGHT" );
static const trait_id trait_PACIFIST( "PACIFIST" );
static const trait_id trait_PSYCHOPATH( "PSYCHOPATH" );
static const trait_id trait_PYROMANIA( "PYROMANIA" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_MASOCHIST( "MASOCHIST" );
static const trait_id trait_MASOCHIST_MED( "MASOCHIST_MED" );
static const trait_id trait_MUT_JUNKIE( "MUT_JUNKIE" );
static const trait_id trait_SAPIOVORE( "SAPIOVORE" );

static const trait_flag_str_id trait_flag_PRED1( "PRED1" );
static const trait_flag_str_id trait_flag_PRED2( "PRED2" );
static const trait_flag_str_id trait_flag_PRED3( "PRED3" );
static const trait_flag_str_id trait_flag_PRED4( "PRED4" );

static const itype_id itype_UPS( "UPS" );

static const mtype_id mon_hallu_multicooker( "mon_hallu_multicooker" );


static const species_id species_HALLUCINATION( "HALLUCINATION" );
static const species_id species_ROBOT( "ROBOT" );
static const species_id species_ZOMBIE( "ZOMBIE" );
static const species_id species_NETHER( "NETHER" );
static const species_id species_SKELETON( "SKELETON" );

static const flag_id flag_NO_PAINT( "NO_PAINT" );

class npc;

std::unique_ptr<iuse_actor> explosion_iuse::clone() const
{
    return std::make_unique<explosion_iuse>( *this );
}

// For an explosion (which releases some kind of gas), this function
// calculates the points around that explosion where to create those
// gas fields.
// Those points must have a clear line of sight and a clear path to
// the center of the explosion.
// They must also be passable.
static std::vector<tripoint_bub_ms> points_for_gas_cloud(
    const tripoint_bub_ms& center, int radius )
{
    map& here = get_map();
    std::vector<tripoint_bub_ms> result;
    for( const auto& p : closest_points_first( center, radius ) ) {
        if( here.impassable( p ) ) { continue; }
        if( p != center ) {
            if( !here.clear_path( center, p, radius, 1, 100 ) ) {
                // Can not splatter gas from center to that point, something is in the way
                continue;
            }
        }
        result.push_back( p );
    }
    return result;
}

void explosion_iuse::load( const JsonObject& obj )
{
    if( obj.has_object( "explosion" ) ) {
        auto expl = obj.get_object( "explosion" );
        explosion = load_explosion_data( expl );
    }

    obj.read( "draw_explosion_radius", draw_explosion_radius );
    if( obj.has_member( "draw_explosion_color" ) ) {
        draw_explosion_color = color_from_string( obj.get_string( "draw_explosion_color" ) );
    }
    obj.read( "do_flashbang", do_flashbang );
    obj.read( "flashbang_player_immune", flashbang_player_immune );
    obj.read( "fields_radius", fields_radius );
    if( obj.has_member( "fields_type" ) || fields_radius > 0 ) {
        fields_type = field_type_id( obj.get_string( "fields_type" ) );
    }
    obj.read( "fields_min_intensity", fields_min_intensity );
    obj.read( "fields_max_intensity", fields_max_intensity );
    if( fields_max_intensity == 0 ) { fields_max_intensity = fields_type.obj().get_max_intensity(); }
    obj.read( "emp_blast_radius", emp_blast_radius );
    obj.read( "scrambler_blast_radius", scrambler_blast_radius );
    obj.read( "sound_volume", sound_volume );
    obj.read( "sound_msg", sound_msg );
    obj.read( "no_deactivate_msg", no_deactivate_msg );
}

int explosion_iuse::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( t ) {
    if( sound_volume >= 0 ) {
            sounds::sound(
                pos, sound_volume, sounds::sound_t::alarm,
                sound_msg.empty() ? _( "Tick." ) : _( sound_msg ), true, "misc", "bomb_ticking" );
        }
    } else if( it.charges > 0 ) {
    if( p.has_item( it ) ) {
            if( no_deactivate_msg.empty() ) {
                p.add_msg_if_player(
                    m_warning,
                    _( "You've already set the %s's timer you might want to get away from it." ),
                    it.tname() );
            } else {
                p.add_msg_if_player( m_info, _( no_deactivate_msg ), it.tname() );
            }
        }
        return 0;
    }
    if( it.charges == 0 ) { trigger_explosion( pos, it.activated_by ); }
    return 1;
}

void explosion_iuse::trigger_explosion( const tripoint_bub_ms& pos, Creature* source ) const
{
    if( explosion ) { explosion_handler::explosion( pos, explosion, source ); }

    if( draw_explosion_radius >= 0 ) {
    explosion_handler::
    draw_explosion( pos, draw_explosion_radius, draw_explosion_color, "explosion" );
    }
    if( do_flashbang ) { explosion_handler::flashbang( pos, flashbang_player_immune, "explosion" ); }
    map& here = get_map();
    if( fields_radius >= 0 && fields_type.id() ) {
        std::vector<tripoint_bub_ms> gas_sources = points_for_gas_cloud( pos, fields_radius );
        for( auto& gas_source : gas_sources ) {
            const int field_intensity = rng( fields_min_intensity, fields_max_intensity );
            here.add_field( gas_source, fields_type, field_intensity, 1_turns );
        }
        if( source == &get_player_character() && source->has_trait( trait_PYROMANIA ) ) {
            if( fields_type == fd_fire || fd_incendiary ) {
                Character& p = get_player_character();
                p.add_morale( MORALE_PYROMANIA_STARTFIRE, 15, 15, 8_hours, 6_hours );
                p.rem_morale( MORALE_PYROMANIA_NOFIRE );
                p.add_msg_if_player( m_good, _( "Fire…  Good…" ) );
            }
        }
    }
    if( scrambler_blast_radius >= 0 ) {
    for( const tripoint_bub_ms& dest : here.points_in_radius( pos, scrambler_blast_radius ) ) {
            explosion_handler::scrambler_blast( dest );
        }
    }
    if( emp_blast_radius >= 0 ) {
    for( const tripoint_bub_ms& dest : here.points_in_radius( pos, emp_blast_radius ) ) {
            explosion_handler::emp_blast( dest );
        }
    }
}

void explosion_iuse::info( const item &, std::vector<iteminfo> &dump ) const
{
    if( explosion.damage > 0 ) {
    dump.emplace_back( "TOOL", _( "Blast damage at epicenter: " ), explosion.damage );
        dump.emplace_back( "TOOL", _( "Blast radius: " ), static_cast<int>( explosion.radius ) );
    }
    const auto& sd = explosion.fragment;
    if( sd ) {
    dump.emplace_back(
        "TOOL", _( "Shrapnel damage: " ), static_cast<int>( sd->impact.total_damage() ) );
        dump.emplace_back( "TOOL", _( "Shrapnel range: " ), sd->range );
    }

    // TODO: List other effects, like EMP and clouds
}

std::unique_ptr<iuse_actor> unfold_vehicle_iuse::clone() const
{
    return std::make_unique<unfold_vehicle_iuse>( *this );
}

void unfold_vehicle_iuse::load( const JsonObject& obj )
{
    vehicle_id = vproto_id( obj.get_string( "vehicle_name" ) );
    obj.read( "unfold_msg", unfold_msg );
    obj.read( "moves", moves );
    obj.read( "tools_needed", tools_needed );
}

int unfold_vehicle_iuse::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    if( p.is_underwater() ) {
    p.add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( p.is_mounted() ) {
    p.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
for( const auto& tool : tools_needed ) {
    // Amount == -1 means need one, but don't consume it.
    if( !p.has_amount( tool.first, 1 ) ) {
            p.add_msg_if_player( _( "You need %s to do it!" ), item::nname( tool.first ) );
            return 0;
        }
    }

    vehicle* veh =
        get_map().add_vehicle( vehicle_id, p.bub_pos(), 0_degrees, 0, 0, false, false, true );
    if( veh == nullptr ) {
    p.add_msg_if_player( m_info, _( "There's no room to unfold the %s." ), it.tname() );
        return 0;
    }
    veh->set_owner( p );

    if( !veh->is_foldable() ) {
    // Mark the vehicle as foldable.
    veh->tags.insert( "convertible" );
        // Store the id of the item the vehicle is made of.
        veh->tags.insert( std::string( "convertible:" ) + it.typeId().str() );
    }
    if( !unfold_msg.empty() ) { p.add_msg_if_player( _( unfold_msg ), it.tname() ); }
    p.moves -= moves;
    // Restore HP of parts if we stashed them previously.
    if( it.has_var( "folding_bicycle_parts" ) ) {
    // Brand new, no HP stored
    return 1;
}
std::istringstream veh_data;
const auto data = it.get_var( "folding_bicycle_parts" );
veh_data.str( data );
if( !data.empty() && data[0] >= '0' && data[0] <= '9' ) {
    // starts with a digit -> old format
    for( const vpart_reference& vpr : veh->get_all_parts() ) {
            int tmp;
            veh_data >> tmp;
            veh->set_hp( vpr.part(), tmp );
        }
    } else {
        try {
            JsonIn json( veh_data );
            // Load parts into a temporary vector to not override
            // cached values (like precalc, passenger_id, ...)
            std::vector<vehicle_part> parts;
            json.read( parts );
            for( size_t i = 0; i < parts.size() && i < static_cast<size_t>( veh->part_count() );
                 i++ ) {
                const vehicle_part& src = parts[i];
                vehicle_part& dst = veh->part( i );
                // and now only copy values, that are
                // expected to be consistent.
                veh->set_hp( dst, src.hp() );
                dst.blood = src.blood;
                // door state/amount of fuel/direction of headlight
                dst.ammo_set( src.ammo_current(), src.ammo_remaining() );
                dst.flags = src.flags;
            }
        } catch( const JsonError& e ) { debugmsg( "Error restoring vehicle: %s", e.c_str() ); }
    }
    if( g->m.veh_at( p.bub_pos() ).part_with_feature( "BOARDABLE", true ) ) {
        g->m.board_vehicle( p.bub_pos(), &p );
    }
    return 1;
}

std::unique_ptr<iuse_actor> consume_drug_iuse::clone() const
{
    return std::make_unique<consume_drug_iuse>( *this );
}

static effect_data load_effect_data( const JsonObject& e )
{
    time_duration time;
    if( e.has_string( "duration" ) ) {
        time = read_from_json_string<time_duration>( *e.get_raw( "duration" ), time_duration::units );
    } else {
        time = time_duration::from_turns( e.get_int( "duration", 0 ) );
    }
    if( e.get_bool( "permanent", false ) ) {
        effect_data
        ret( efftype_id( e.get_string( "id" ) ), time,
             get_body_part_token( e.get_string( "bp", "NUM_BP" ) ) );
        ret.permanent = true;
        if( json_report_strict ) {
            try {
                e.throw_error( "Effect permanence has been moved to effect_type.  Set permanence "
                               "there.",
                               "permanent" );
            } catch( const JsonError& ex ) { debugmsg( "\n%s", ex.what() ); }
        }
        return ret;
    } else {
        return effect_data(
                   efftype_id( e.get_string( "id" ) ), time,
                   get_body_part_token( e.get_string( "bp", "NUM_BP" ) ) );
    }
}

void consume_drug_iuse::load( const JsonObject& obj )
{
    obj.read( "activation_message", activation_message );
    obj.read( "charges_needed", charges_needed );
    obj.read( "tools_needed", tools_needed );

    if( obj.has_array( "effects" ) ) {
        for( const JsonObject e : obj.get_array( "effects" ) ) {
            effects.push_back( load_effect_data( e ) );
        }
    }
    obj.read( "stat_adjustments", stat_adjustments );
    obj.read( "fields_produced", fields_produced );
    obj.read( "moves", moves );
    obj.read( "fake_item", fake_item );
    obj.read( "lightweight_mod", lightweight_mod );
    obj.read( "tolerance_mod", tolerance_mod );
    obj.read( "tolerance_lightweight_effected", tolerance_lightweight_effected ); // default true
    lit_item = obj.get_string( "lit_item", lit_item );
    obj.read( "smoking_duration", smoking_duration );
    obj.read( "too_much_threshold", too_much_threshold );
    obj.read( "snippet_category", snippet_category );
    obj.read( "snippet_chance", snippet_chance );
    obj.read( "do_weed_msg",
              do_weed_msg ); // i wish i didn't have to do this, but the weed_msg function can't
    // really be easily JSONified

    if( obj.has_array( "addiction_type_too_much" ) ) {
        for( const JsonArray pair : obj.get_array( "addiction_type_too_much" ) ) {
            if( pair.size() >= 2 ) {
                addiction_type_too_much.emplace_back( pair.get_string( 0 ), pair.get_string( 1 ) );
            }
        }
    }

    for( JsonArray vit : obj.get_array( "vitamins" ) ) {
        auto lo = vit.get_int( 1 );
        auto hi = vit.size() >= 3 ? vit.get_int( 2 ) : lo;
        vitamins.emplace( vitamin_id( vit.get_string( 0 ) ), std::make_pair( lo, hi ) );
    }

    used_up_item = obj.get_string( "used_up_item", used_up_item );
}

void consume_drug_iuse::info( const item &, std::vector<iteminfo> &dump ) const
{
    const std::string vits = enumerate_as_string(
    vitamins.begin(), vitamins.end(), []( const decltype( vitamins )::value_type & v ) {
        const time_duration rate = get_player_character().vitamin_rate( v.first );
        if( rate <= 0_turns ) { return std::string(); }
        const int lo = static_cast<int>( v.second.first * rate / 1_days * 100 );
        const int hi = static_cast<int>( v.second.second * rate / 1_days * 100 );

        return string_format(
                   lo == hi ? "%s (%i%%)" : "%s (%i-%i%%)", v.first.obj().name(), lo, hi );
    } );

    if( !vits.empty() ) { dump.emplace_back( "TOOL", _( "Vitamins (RDA): " ), vits ); }

    if( tools_needed.contains( itype_syringe ) ) {
        dump.emplace_back( "TOOL", _( "You need a <info>syringe</info> to inject this drug." ) );
    }
}

int consume_drug_iuse::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    auto need_these = tools_needed;
    if( need_these.contains( itype_syringe ) && p.has_bionic( bio_syringe ) ) {
        need_these.erase( itype_syringe ); // no need for a syringe with bionics like these!
    }

    // Check prerequisites first.
    for( const auto& tool : need_these ) {
        // Amount == -1 means need one, but don't consume it.
        if( !p.has_amount( tool.first, 1 ) ) {
            p.add_msg_player_or_say(
                _( "You need %1$s to consume %2$s!" ), _( "I need a %1$s to consume %2$s!" ),
                item::nname( tool.first ), it.type_name( 1 ) );
            return 0;
        }
    }
    for( const auto& consumable : charges_needed ) {
        // Amount == -1 means need one, but don't consume it.
        if( !p.has_charges( consumable.first, ( consumable.second == -1 ) ? 1 : consumable.second ) ) {
            p.add_msg_player_or_say(
                _( "You need %1$s to consume %2$s!" ), _( "I need a %1$s to consume %2$s!" ),
                item::nname( consumable.first ), it.type_name( 1 ) );
            return 0;
        }
    }

    // this is a smokeable item, we need to make sure player isnt already smoking (ripped from
    // iuse::smoking)
    if( !lit_item.empty() ) {
        // make sure we're not already smoking something
        auto cigs = p.items_with( []( const item & it ) {
            return it.is_active() && it.has_flag( flag_LITCIG );
        } );
        if( !cigs.empty() ) {
            p.add_msg_if_player( m_info, _( "You're already smoking a %s!" ), cigs[0]->tname() );
            return 0;
        }
    }

    // Output message.
    p.add_msg_if_player( _( activation_message ), it.type_name( 1 ) );

    if( smoking_duration ) {
        detached_ptr<item> cig;
        cig = item::spawn( lit_item, calendar::turn );
        time_duration converted_time = time_duration::from_minutes( smoking_duration );

        cig->activate();
        cig->set_counter( to_turns<int>( converted_time ) );

        p.i_add( std::move( cig ) );
    }

    if( do_weed_msg ) {
        if( one_in( snippet_chance ) ) { weed_msg( p ); }
    }

    // item used to "fake" addiction (ripped from old ecig iuse)
    if( !fake_item.empty() ) {
        item* dummy_item = item::spawn_temporary( fake_item, calendar::turn );
        p.consume_effects( *dummy_item );
    }

    // Apply the various effects.
    for( const auto& eff : effects ) {
        time_duration dur = eff.duration;
        if( tolerance_lightweight_effected ) {
            if( p.has_trait( trait_TOLERANCE ) ) {
                dur *= tolerance_mod;
            } else if( p.has_trait( trait_LIGHTWEIGHT ) ) {
                dur *= lightweight_mod;
            }
        }

        // only way i could figure out how to do this
        std::unordered_map<std::string, efftype_id> effect_map = {
            {"cig", effect_cig}
            // Add other mappings as needed. I think cigs are the only thing this applies to at the
            // moment.
        };

        // check if effect were applying is connected to an addiction type
        for( const auto& entry : addiction_type_too_much ) {
            const std::string& attm_effect = entry.first;
            const std::string& attm_addiction_type = entry.second;

            auto it = effect_map.find( attm_effect );
            if( it != effect_map.end() ) {
                const efftype_id& id = it->second;
                if( id.obj() == eff.id.obj() ) {
                    if( p.get_effect_dur( id )
                        > time_duration::from_minutes( too_much_threshold )
                        * ( p.addiction_level( addiction_type( attm_addiction_type ) ) + 1 ) ) {
                        p.add_msg_if_player(
                            m_bad, _( "Ugh, too much %s… you feel nasty." ), attm_addiction_type );
                        break;
                    }
                }
            }
        }

        p.add_effect( eff.id, eff.duration, convert_bp( eff.bp ) );
        if( eff.permanent ) { p.get_effect( eff.id, convert_bp( eff.bp ) ).set_permanent(); }
    }

    for( const auto& stat_adjustment : stat_adjustments ) {
        p.mod_stat( stat_adjustment.first, stat_adjustment.second );
    }
    map& here = get_map();
    for( const auto& field : fields_produced ) {
        const field_type_id fid = field_type_id( field.first );
        for( int i = 0; i < 3; i++ ) {
            here.add_field(
            {p.bub_pos().x() + rng( -2, 2 ), p.bub_pos().y() + rng( -2, 2 ), p.bub_pos().z()}, fid,
            field.second );
        }
    }

    // for vitamins that accumulate (max > 0) multivitamins risk causing hypervitaminosis
    for( const auto& v : vitamins ) {
        // players with mutations that remove the requirement for a vitamin cannot suffer
        // accumulation of it
        p.vitamin_mod(
            v.first, rng( v.second.first, v.second.second ), p.vitamin_rate( v.first ) <= 0_turns );
    }

    if( !snippet_category.empty() ) {
        std::string snippet_string = "";
        snippet_string =
            SNIPPET.random_from_category( snippet_category ).value_or( translation() ).translated();
        if( one_in( snippet_chance ) ) { p.add_msg_if_player( _( "%s" ), snippet_string ); }
    }

    // Consume charges.
    for( const auto& consumable : charges_needed ) {
        if( consumable.second != -1 ) { p.use_charges( consumable.first, consumable.second ); }
    }

    if( !used_up_item.empty() ) { p.i_add_or_drop( item::spawn( used_up_item, it.birthday() ) ); }

    p.moves -= moves;
    return it.type->charges_to_use();
}

std::unique_ptr<iuse_actor> delayed_transform_iuse::clone() const
{
    return std::make_unique<delayed_transform_iuse>( *this );
}

void delayed_transform_iuse::load( const JsonObject& obj )
{
    iuse_transform::load( obj );
    not_ready_msg = obj.get_string( "not_ready_msg" );
    transform_age = obj.get_int( "transform_age" );
}

int delayed_transform_iuse::time_to_do( const item& it ) const
{
    // TODO: change return type to time_duration
    return transform_age - to_turns<int>( it.age() );
}

int delayed_transform_iuse::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( time_to_do( it ) > 0 ) {
    p.add_msg_if_player( m_info, _( not_ready_msg ) );
        return 0;
    }
    return iuse_transform::use( p, it, t, pos );
}

std::unique_ptr<iuse_actor> set_transform_iuse::clone() const
{
    return std::make_unique<set_transform_iuse>( *this );
}

void set_transform_iuse::load( const JsonObject& obj )
{
    iuse_transform::load( obj );
    obj.read( "turn_off", turn_off );
    obj.read( "flag", flag );
    if( !obj.read( "set_charges_msg", set_charges_msg ) ) {
        set_charges_msg = to_translation( "The %s is empty!" );
    }

    if( !obj.read( "set_charges", set_charges ) ) { set_charges = 0; }
    set_charges = std::max( set_charges, 0 );
}

int set_transform_iuse::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( t ) {
    return 0; // invoked from active item processing, do nothing.
}

const bool possess =
    p.has_item( it )
    || ( it.has_flag( flag_ALLOWS_REMOTE_USE ) && square_dist( p.bub_pos(), pos ) == 1 );

    if( set_charges ) {
    if( it.is_power_armor() && character_funcs::can_interface_armor( p ) ) {
            if( !p.has_power() ) {
                if( possess ) { p.add_msg_if_player( m_info, set_charges_msg, it.tname() ); }
                return 0;
            }
        } else if( it.units_remaining( p ) < set_charges ) {
            if( possess ) { p.add_msg_if_player( m_info, set_charges_msg, it.tname() ); }
            return 0;
        }
    }

    iuse_transform::use( p, it, t, pos );

    const flag_id f( flag );
for( auto& elem : p.worn ) {
    if( elem->has_flag( f ) && elem->is_active() == turn_off ) {
            if( elem->type->can_use( "set_transformed" ) ) {
                const set_transformed_iuse* actor = dynamic_cast<const set_transformed_iuse *>(
                                                        elem->get_use( "set_transformed" )->get_actor_ptr() );
                if( actor == nullptr ) {
                    debugmsg( "iuse_actor type descriptor and actual type mismatch" );
                } else {
                    actor->bypass( p, *elem, t, pos );
                }
            } else {
                debugmsg( "Expected set_transformed function" );
            }
        }
    }
    return 0;
}

std::unique_ptr<iuse_actor> set_transformed_iuse::clone() const
{
    return std::make_unique<set_transformed_iuse>( *this );
}

void set_transformed_iuse::load( const JsonObject& obj )
{
    iuse_transform::load( obj );
    obj.read( "restricted", restricted );
    obj.read( "dependencies", dependencies );
}

int set_transformed_iuse::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( t ) {
    return 0; // invoked from active item processing, do nothing.
}

iuse_transform::use( p, it, t, pos );

return 0;
}

int set_transformed_iuse::bypass( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    return iuse_transform::use( p, it, t, pos );
}

ret_val<bool> set_transformed_iuse::can_use(
    const Character &, const item &, bool, const tripoint_bub_ms & ) const
{
    if( restricted ) { return ret_val<bool>::make_failure( _( "Activate via main piece." ) ); }
    return ret_val<bool>::make_success();
}

std::unique_ptr<iuse_actor> place_monster_iuse::clone() const
{
    return std::make_unique<place_monster_iuse>( *this );
}

void place_monster_iuse::load( const JsonObject& obj )
{
    mtypeid = mtype_id( obj.get_string( "monster_id" ) );
    obj.read( "friendly_msg", friendly_msg );
    obj.read( "hostile_msg", hostile_msg );
    obj.read( "difficulty", difficulty );
    obj.read( "moves", moves );
    obj.read( "place_randomly", place_randomly );
    obj.read( "is_pet", is_pet );
    if( obj.has_array( "skills" ) ) {
        JsonArray skills_ja = obj.get_array( "skills" );
        for( JsonValue s : skills_ja ) { skills.emplace( s.get_string() ); }
    }
}

int place_monster_iuse::use( player& p, item& it, bool, const tripoint_bub_ms& pos ) const
{
    mtype_id spawn_id = mtypeid;

    int diff_mod = 1;
    bool place_random = place_randomly;
    // ugly hack, sorry
    if( it.has_var( "place_monster_override" ) ) {
        spawn_id = mtype_id( it.get_var( "place_monster_override" ) );
        // currently cant use this to tame an otherwise untameable animal
        diff_mod = 999;
    }

    if( it.has_flag( flag_RADIO_MOD ) ) {
        place_random = true;
        it.activate();
    }

    shared_ptr_fast<monster> newmon_ptr = make_shared_fast<monster>( spawn_id );
    monster& newmon = *newmon_ptr;
    newmon.init_from_item( it );

    const tripoint_bub_ms& pnt = it.is_active() ? pos : p.bub_pos();

    if( it.has_var( "place_monster_override" ) ) {
        newmon.no_extra_death_drops = true;
        it.deactivate();
    }
    cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) { params["creature"] = &newmon; } );
    cata::run_hooks( "on_monster_spawn", [&]( sol::table & params ) { params["monster"] = &newmon; } );
    if( place_random ) {
        // place_critter_around returns the same pointer as its parameter (or null)
        // Allow position to be different from the player for tossed or launched items
        if( !g->place_critter_around( newmon_ptr, pnt, 1 ) ) {
            p.add_msg_if_player(
                m_info, _( "There is no adjacent square to release the %s in!" ), newmon.name() );
            // If remotely triggered due to ACT_ON_RANGED_HIT, set it back to being inactive so it
            // won't spawn infinitely
            it.deactivate();
            return 0;
        }
    } else {
        const std::string query = string_format( _( "Place the %s where?" ), newmon.name() );
        const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( query );
        if( !pnt_ ) { return 0; }
        // place_critter_at returns the same pointer as its parameter (or null)
        if( !g->place_critter_at( newmon_ptr, *pnt_ ) ) {
            p.add_msg_if_player( m_info, _( "You cannot place a %s there." ), newmon.name() );
            return 0;
        }
    }
    // If it's active then we know it was triggered by ACT_ON_RANGED_HIT and did not deactivate from
    // lack of room earlier If so, don't drain moves from remote deployment since it would trigger
    // after the throw
    if( !it.is_active() ) { p.moves -= moves; }
    if( !newmon.has_flag( MF_INTERIOR_AMMO ) ) {
        for( auto& amdef : newmon.ammo ) {
            item& ammo_item = *item::spawn_temporary( amdef.first, calendar::start_of_cataclysm );
            const int available = p.charges_of( amdef.first );
            if( available == 0 ) {
                amdef.second = 0;
                p.add_msg_if_player(
                    m_info,
                    _( "If you had standard factory-built %1$s bullets, you could load the %2$s." ),
                    ammo_item.type_name( 2 ), newmon.name() );
                continue;
            }
            // Don't load more than the default from the monster definition.
            ammo_item.charges = std::min( available, amdef.second );
            p.use_charges( amdef.first, ammo_item.charges );
            //~ First %s is the ammo item (with plural form and count included), second is the
            //monster name
            p.add_msg_if_player(
                vgettext( "You load %1$d x %2$s round into the %3$s.",
                          "You load %1$d x %2$s rounds into the %3$s.", ammo_item.charges ),
                ammo_item.charges, ammo_item.type_name( ammo_item.charges ), newmon.name() );
            amdef.second = ammo_item.charges;
        }
    }
    int skill_offset = 0;
    for( const skill_id& sk : skills ) { skill_offset += p.get_skill_level( sk ); }
    /** @EFFECT_INT increases chance of a placed turret being friendly */
    /** Full-on pets also auto-succeed if we've already succeeded before deactivating it */
    if( ( rng( 0, p.int_cur ) + skill_offset < rng( 0, 2 * ( difficulty * diff_mod ) )
          && !it.has_flag( flag_SPAWN_FRIENDLY ) )
        || it.has_flag( flag_SPAWN_HOSTILE ) ) {
        if( hostile_msg.empty() ) {
            p.add_msg_if_player(
                m_bad, _( "The %s scans you and makes angry beeping noises!" ), newmon.name() );
        } else {
            p.add_msg_if_player( m_bad, "%s", _( hostile_msg ) );
        }
    } else {
        if( friendly_msg.empty() ) {
            p.add_msg_if_player(
                m_warning, _( "The %s emits an IFF beep as it scans you." ), newmon.name() );
        } else {
            p.add_msg_if_player( m_warning, "%s", _( friendly_msg ) );
        }
        newmon.friendly = -1;
        if( is_pet ) { newmon.add_effect( effect_pet, 1_turns ); }
    }
    // mark artifical womb as dirty, and convert it
    if( it.has_var( "place_monster_override" ) ) {
        it.convert( itype_id( "embryo_empty" ) );
        it.clear_vars();
        it.faults.emplace( fault_bionic_nonsterile );
    }
    // Transfer label from the item to monster nickname
    if( it.has_var( "item_label" ) ) { newmon.unique_name = it.get_var( "item_label" ); }
    // TODO: add a flag instead of monster id or something?
    if( newmon.type->id == mtype_id( "mon_laserturret" ) && !g->is_in_sunlight( newmon.bub_pos() ) ) {
        p.add_msg_if_player( _( "A flashing LED on the laser turret appears to indicate low light." ) );
    }
    return 1;
}

std::unique_ptr<iuse_actor> place_npc_iuse::clone() const
{
    return std::make_unique<place_npc_iuse>( *this );
}

void place_npc_iuse::load( const JsonObject& obj )
{
    npc_class_id = string_id<npc_template>( obj.get_string( "npc_class_id" ) );
    obj.read( "summon_msg", summon_msg );
    obj.read( "moves", moves );
    obj.read( "place_randomly", place_randomly );
}

int place_npc_iuse::use( player& p, item &, bool, const tripoint_bub_ms & ) const
{
    map& here = get_map();
    std::optional<tripoint_bub_ms> target_pos;
    if( place_randomly ) {
        const tripoint_range<tripoint_bub_ms> target_range = points_in_radius( p.bub_pos(), 1 );
        target_pos = random_point( target_range, []( const tripoint_bub_ms & t ) {
            return !get_map().passable( t );
        } );
    } else {
        const std::string query = _( "Place npc where?" );
        target_pos = choose_adjacent( _( "Place npc where?" ) );
    }
    if( !target_pos ) { return 0; }
    if( !here.passable( target_pos.value() ) ) {
        p.add_msg_if_player( m_info, _( "There is no square to spawn npc in!" ) );
        return 0;
    }

    here.place_npc( target_pos.value().xy(), npc_class_id );
    p.mod_moves( -moves );
    p.add_msg_if_player( m_info, "%s", _( summon_msg ) );
    return 1;
}


void change_scent_iuse::load( const JsonObject& obj )
{
    scenttypeid = scenttype_id( obj.get_string( "scent_typeid" ) );
    if( !scenttypeid.is_valid() ) { obj.throw_error( "Invalid scent type id.", "scent_typeid" ); }
    if( obj.has_array( "effects" ) ) {
        for( JsonObject e : obj.get_array( "effects" ) ) { effects.push_back( load_effect_data( e ) ); }
    }
    assign( obj, "moves", moves );
    assign( obj, "charges_to_use", charges_to_use );
    assign( obj, "scent_mod", scent_mod );
    assign( obj, "duration", duration );
    assign( obj, "waterproof", waterproof );
}

int change_scent_iuse::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    p.set_value( "prev_scent", p.get_type_of_scent().c_str() );
    if( waterproof ) { p.set_value( "waterproof_scent", "true" ); }
    p.add_effect( efftype_id( "masked_scent" ), duration, bodypart_str_id::NULL_ID(), scent_mod );
    p.set_type_of_scent( scenttypeid );
    p.mod_moves( -moves );
    add_msg( m_info, _( "You use the %s to mask your scent" ), it.tname() );

    // Apply the various effects.
for( const auto& eff : effects ) {
    p.add_effect( eff.id, eff.duration, convert_bp( eff.bp ) );
        if( eff.permanent ) { p.get_effect( eff.id, convert_bp( eff.bp ) ).set_permanent(); }
    }
    return charges_to_use;
}

std::unique_ptr<iuse_actor> change_scent_iuse::clone() const
{
    return std::make_unique<change_scent_iuse>( *this );
}

void cloning_syringe_iuse::load( const JsonObject& obj )
{
    assign( obj, "moves", moves );
    assign( obj, "charges_to_use", charges_to_use );
}

int cloning_syringe_iuse::use( player& p, item& it, bool, const tripoint_bub_ms& pos ) const
{
    const auto is_empty_usb = []( const item & drive ) { return drive.contents.empty(); };

    if( !it.units_sufficient( p, charges_to_use ) ) {
        add_msg( m_info, _( "There's not enough charge left in the %s." ), it.display_name() );
        return 0;
    }
    if( !p.has_amount( itype_usb_drive, 1, true, is_empty_usb ) ) {
        add_msg( m_bad, "You need an empty USB drive to store genetic data." );
        return 0;
    }

    const std::string query = string_format( _( "Select which creature?" ) );
    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( query );

    if( !pnt_ ) {
        // No valid point was chosen — handle this case, maybe just return
        return 0;
    }

    // Extract the tripoint from the optional
    const tripoint_bub_ms& pnt = *pnt_;
    const Creature* const critter = g->critter_at( pnt );
    if( !critter ) {
        add_msg( m_info, _( "There's no creature there." ) );
        return 0;
    }

    monster* const m = const_cast<monster *>( critter->as_monster() );
    if( !m ) {
        add_msg( m_info, _( "There's no creature there." ) );
        return 0;
    }

    const int fa_skill = p.get_skill_level( skill_firstaid );
    // Convert first aid skill into success chance.
    // Each skill level = +15% chance, but we clamp between 15–95%
    // so there is always a small chance to succeed (even unskilled)
    // and a small chance to fail (even at max skill).
    const int chance = clamp( fa_skill * 10, 15, 95 );

    // use moves and damage mon
    p.mod_moves( -moves );
    m->apply_damage( &p, bodypart_id( "torso" ), 1 );

    if( !x_in_y( chance, 100 ) ) {
        add_msg( m_bad,
                 _( "The %s emits a loud error beep!  You failed to gather a sufficient sample." ),
                 it.display_name() );
        sounds::sound( pos, 8, sounds::sound_t::alarm, _( "beep!" ), true, "misc", "beep" );
        // add actual noise here
        return charges_to_use;
    }

    // we can only grow organic matter, and some species are invalid
    bool in_bad_species =
        m->in_species( species_HALLUCINATION ) || m->in_species( species_ROBOT )
        || m->in_species( species_ZOMBIE ) || m->in_species( species_NETHER )
        || m->in_species( species_SKELETON );
    if( m->has_flag( MF_CANT_CLONE ) || in_bad_species ) {
        add_msg( m_info, _( "The %s emits two error beeps.  This creature can't provide a valid "
                            "sample." ) );
        return 0;
    }

    // technically you can't use the same creature for two different scans, but you should be able
    // to copy USB so doesn't matter
    if( m->get_value( "genome_scanned" ) == "true" ) {
        add_msg( m_info, _( "That creature's genome has already been scanned." ) );
        return 0;
    }

    m->set_value( "genome_scanned", "true" );

    const mtype_id& id = m->type->id;
    const std::string id_str = id.str();

    add_msg( m_good, _( "The %s beeps softly.  You successfully gathered a sample from the %s!" ),
             it.display_name(), m->name() );


    auto drives = p.all_items_with_flag( flag_genome_drive );

    for( size_t z = 0; z < drives.size(); z++ ) {
        if( drives[z]->get_var( "specimen_sample" ) == id_str ) {
            int progress = drives[z]->get_var( "specimen_sample_progress", 0 );
            const auto size =
                std::max( 1, cloning_utils::specimen_required_sample_size( m->type->id ) );

            // Increment progress, but don't exceed size
            if( progress < size ) {
                progress++;
                drives[z]->set_var( "specimen_sample_progress", std::to_string( progress ) );
                add_msg( m_info, "Progress: %d/%d for genome sample.", progress, size );
                if( progress == size ) { add_msg( m_good, "Sample is complete." ); }
            } else {
                add_msg( "Sample is already complete." );
            }

            return charges_to_use;
        }
    }

    // Create new genome drive
    p.use_amount( itype_usb_drive, 1, is_empty_usb );
    detached_ptr<item> drive = item::spawn( itype_genome_drive, calendar::turn );
    const auto size = std::max( 1, cloning_utils::specimen_required_sample_size( m->type->id ) );

    drive->set_var( "specimen_sample", id_str );
    drive->set_var( "specimen_sample_progress", "1" ); // First increment
    drive->set_var( "specimen_name", m->name() );

    if( size > 1 ) {
        add_msg( m_info, "Progress: 1/%d for genome sample.", size );
    } else {
        add_msg( m_good, "Sample is complete." );
    }
    p.i_add( std::move( drive ) );

    return charges_to_use;
}

std::unique_ptr<iuse_actor> cloning_syringe_iuse::clone() const
{
    return std::make_unique<cloning_syringe_iuse>( *this );
}

void dna_editor_iuse::load( const JsonObject& obj )
{
    assign( obj, "moves", moves );
    assign( obj, "charges_to_use", charges_to_use );
}

int dna_editor_iuse::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    const auto is_empty_usb = []( const item & drive ) { return drive.contents.empty(); };

    if( !it.units_sufficient( p, charges_to_use ) ) {
        add_msg( m_info, _( "There's not enough charge left in the %s." ), it.display_name() );
        return 0;
    }
    auto genome_drives = p.all_items_with_flag( flag_genome_drive );
    if( genome_drives.size() == 0 ) {
        popup( "You have no valid genome drives." );
        return 0;
    }

    uilist specimen_menu;
    specimen_menu.text = _( "Select specimen sample:" );
    bool has_complete_sample = false;
    for( size_t z = 0; z < genome_drives.size(); z++ ) {
        const int progress = genome_drives[z]->get_var( "specimen_sample_progress", 0 );
        const auto specimen_id = mtype_id( genome_drives[z]->get_var( "specimen_sample" ) );
        const auto size = cloning_utils::specimen_required_sample_size( specimen_id );
        if( size > 0 && progress >= size ) {
            has_complete_sample = true;
            specimen_menu.addentry(
                z, true, MENU_AUTOASSIGN, string_format( "%s", genome_drives[z]->display_name() ) );
        }
    }
    if( !has_complete_sample ) {
        popup( "You have no valid genome drives." );
        return 0;
    }
    specimen_menu.query();
    const int choice = specimen_menu.ret;
    if( choice < 0 ) { return 0; }

    auto& selected_drive = genome_drives[choice]; // reference to the original detached_ptr

    uilist menu;
    menu.text = string_format( _( "What to do with the %s?" ), selected_drive->display_name() );
    menu.addentry( 0, true, 'e', "Examine sample" );
    menu.addentry( 1, p.has_charges( itype_mutagen, 1 ) && p.has_charges( itype_biomaterial, 1 ), 'i',
                   "Research upgrade" );
    menu.addentry( 2, p.has_amount( itype_usb_drive, 1, true, is_empty_usb ), 'c', "Clone drive" );
    menu.addentry( 3, p.has_charges( itype_biomaterial, 1 ), 'p', "Produce DNA" );
    menu.query();
    if( menu.ret < 0 ) { return 0; }

    if( menu.ret == 0 ) {
        // grab the monsters data from a fake copy
        const auto specimen_id = mtype_id( selected_drive->get_var( "specimen_sample" ) );
        const auto newmon_ptr = make_shared_fast<monster>( specimen_id );
        const monster& newmon = *newmon_ptr;

        const char *size_str = cloning_utils::specimen_size_class_string( specimen_id );

        popup( _( "Examination Results:\n\nSample Name: %s\nSize Class: %s\nWeight: %.0fkg\nVolume: "
                  "%.0fl" ),
               selected_drive->get_var( "specimen_name" ), size_str,
               static_cast<double>( to_kilogram( newmon.get_weight() ) ),
               static_cast<double>( to_liter( newmon.get_volume() ) ) );

        return 0;
    } else if( menu.ret == 1 ) {
        const mtype_id id( selected_drive->get_var( "specimen_sample" ) );
        const mtype& type = id.obj();

        mongroup_id upgrade_group = mongroup_id::NULL_ID();
        upgrade_group = type.upgrade_group;
        const auto mons = upgrade_group.obj().monsters;

        if( mons.empty() ) {
            popup( "A message pops up on the genome editor indicating there are no further "
                   "mutations possible for this sample." );
            return 0;
        }

        if( !query_yn( _( "This will use up 1 unit of mutagen and 1 unit of biomaterial.  Are you "
                          "sure?" ),
                       selected_drive->display_name() ) ) {
            return 0;
        }

        int total_freq = 0;
        for( const MonsterGroupEntry& entry : mons ) { total_freq += entry.frequency; }
        int roll = rng( 1, total_freq );
        const MonsterGroupEntry* chosen = nullptr;
        for( const MonsterGroupEntry& entry : mons ) {
            roll -= entry.frequency;
            if( roll <= 0 ) {
                chosen = &entry;
                break;
            }
        }
        if( !chosen ) { return 0; }

        const shared_ptr_fast<monster> newmon_ptr = make_shared_fast<monster>(
                mtype_id( chosen->name.str() ) );
        const monster& newmon = *newmon_ptr;

        p.use_charges( itype_mutagen, 1 );
        p.use_charges( itype_biomaterial, 1 );

        // chance of failure when converting DNA
        if( rng( 1, 100 ) < 80 ) {
            add_msg( m_bad, "The research produced no result." );
            return charges_to_use;
        }

        add_msg( m_info, _( "The research produced a viable %s sample!" ), newmon.name() );
        const auto specimen_id = mtype_id( chosen->name.str() );
        const auto size = cloning_utils::specimen_required_sample_size( specimen_id );
        selected_drive->set_var( "specimen_sample", specimen_id.str() );
        selected_drive->set_var( "specimen_sample_progress", std::to_string( size ) );
        selected_drive->set_var( "specimen_name", newmon.name() );
    } else if( menu.ret == 2 ) {
        add_msg( "You clone a copy of the drive onto another USB." );
        p.use_amount( itype_usb_drive, 1, is_empty_usb );
        detached_ptr<item> drive_copy = item::spawn( itype_genome_drive, calendar::turn );

        drive_copy->set_var( "specimen_sample", selected_drive->get_var( "specimen_sample" ) );
        drive_copy->set_var( "specimen_sample_progress", selected_drive->get_var( "specimen_sample_"
                             "progress" ) );
        drive_copy->set_var( "specimen_name", selected_drive->get_var( "specimen_name" ) );

        p.i_add( std::move( drive_copy ) );
    } else if( menu.ret == 3 ) {
        p.use_charges( itype_biomaterial, 1 );
        const std::string msg =
            string_format( _( "You produce a unit of %s DNA." ), selected_drive->get_var( "specimen_"
                          "name" ) );
        add_msg( msg );

        detached_ptr<item> dna = item::spawn( itype_id( "dna" ), calendar::turn, 1 );

        dna->set_var( "specimen_sample", selected_drive->get_var( "specimen_sample" ) );
        dna->set_var( "specimen_name", selected_drive->get_var( "specimen_name" ) );

        liquid_handler::handle_all_liquid( std::move( dna ), PICKUP_RANGE );
    }

    return charges_to_use;
}

std::unique_ptr<iuse_actor> dna_editor_iuse::clone() const
{
    return std::make_unique<dna_editor_iuse>( *this );
}

void multicooker_iuse::load( const JsonObject& obj )
{
    assign( obj, "do_hallu", do_hallu );
    assign( obj, "charges_to_start", charges_to_start );
    assign( obj, "charges_per_minute", charges_per_minute );
    assign( obj, "time_mult", time_mult );
    for( const std::string line : obj.get_array( "recipes" ) ) { recipes.emplace( line ); }
    for( const std::string line : obj.get_array( "subcategories" ) ) { subcategories.emplace( line ); }
    for( const std::string line : obj.get_array( "temporary_tools" ) ) {
        temporary_tools.emplace( line );
    }
}

static bool multicooker_hallu( player& p )
{
    p.moves -= to_moves<int>( 2_seconds );
    const int random_hallu = rng( 1, 7 );
    switch( random_hallu ) {

        case 1:
            add_msg( m_info, _( "And when you gaze long into a screen, the screen also gazes into "
                                "you." ) );
            return true;

        case 2:
            add_msg( m_bad, _( "The multi-cooker boiled your head!" ) );
            return true;

        case 3:
            add_msg( m_info, _( "The characters on the screen display an obscene joke.  Strange "
                                "humor." ) );
            return true;

        case 4:
            //~ Single-spaced & lowercase are intentional, conveying hurried speech-KA101
            add_msg( m_warning, _( "Are you sure?!  the multi-cooker wants to poison your food!" ) );
            return true;

        case 5:
            add_msg( m_info, _( "The multi-cooker argues with you about the taste preferences.  You "
                                "don't want to deal with it." ) );
            return true;

        case 6:
            if( !one_in( 5 ) ) {
                add_msg( m_warning, _( "The multi-cooker runs away!" ) );
                if( monster * const m =
                        g->place_critter_around( mon_hallu_multicooker, p.bub_pos(), 1 ) ) {
                    m->hallucination = true;
                    m->add_effect( effect_run, 100_turns );
                }
            } else {
                p.add_msg_if_player( m_info, _( "You're surrounded by aggressive multi-cookers!" ) );

                for( const tripoint_bub_ms& pn : g->m.points_in_radius( p.bub_pos(), 1 ) ) {
                    if( monster * const m = g->place_critter_at( mon_hallu_multicooker, pn ) ) {
                        m->hallucination = true;
                    }
                }
            }
            return true;

        default:
            return false;
    }
}

int multicooker_iuse::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( t ) {
    if( !it.units_sufficient( p, charges_per_minute ) ) {
            it.deactivate();
            return 0;
        }

        int cooktime = it.get_var( "COOKTIME", 0 );
        cooktime -= 100;

        if( cooktime <= 0 ) {
            it.deactivate();
            it.erase_var( "COOKTIME" );
            it.put_in(
                item::spawn( it.get_var( "RESULT" ), calendar::turn, it.get_var( "BATCHCOUNT", 1 ) ) );
            it.erase_var( "BATCHCOUNT" );
            it.erase_var( "RESULT" );

            sounds::sound( pos, 8, sounds::sound_t::alarm, _( "ding!" ), true, "misc", "ding" );

            return 0;
        } else {
            if( calendar::once_every( 1_minutes ) ) { it.ammo_consume( charges_per_minute, pos ); }
            it.set_var( "COOKTIME", cooktime );
            return 0;
        }
    } else {
        enum { mc_start, mc_stop, mc_take, mc_upgrade };

        if( p.is_underwater() ) {
            p.add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
            return 0;
        }

        if( do_hallu && ( p.has_effect( effect_hallu ) || p.has_effect( effect_visuals ) ) ) {
            if( multicooker_hallu( p ) ) { return 0; }
        }

        uilist menu;
        menu.text = _( "Choose option:" );

        item* dish_it = it.contents.get_item_with( []( const item & it ) {
            return !( it.is_toolmod() || it.is_magazine() );
        } );

        if( it.is_active() ) {
            menu.addentry( mc_stop, true, 's', _( "Stop crafting" ) );
        } else {

            if( dish_it == nullptr ) {
                if( it.ammo_remaining() < charges_to_start ) {
                    p.add_msg_if_player( _( "Batteries are low." ) );
                    return 0;
                }
                menu.addentry( mc_start, true, 's', _( "Start crafting " ) );
            } else {
                menu.addentry( mc_take, true, 't', _( "Remove Product" ) );
            }
        }

        menu.query();
        int choice = menu.ret;

        if( choice < 0 ) { return 0; }

        if( mc_stop == choice ) {
            if( query_yn( _( "Really stop?" ) ) ) {
                it.deactivate();
                it.erase_var( "RESULT" );
                it.erase_var( "COOKTIME" );
                it.erase_var( "BATCHCOUNT" );
                it.erase_var( "RECIPE" );
            }
            return 0;
        }

        if( mc_take == choice ) {

            detached_ptr<item> dish = it.remove_item( *dish_it );
            const std::string dish_name = dish->tname( dish->charges, false );
            if( dish->made_of( LIQUID ) ) {
                if( !p.check_eligible_containers_for_crafting( *recipe_id( it.get_var( "RECIPE" ) ), 1 ) ) {
                    p.add_msg_if_player(
                        m_info, _( "You don't have a suitable container to store your %s." ),
                        dish_name );

                    return 0;
                }
                liquid_handler::handle_all_liquid( std::move( dish ), PICKUP_RANGE );
            } else {
                p.i_add( std::move( dish ) );
            }

            it.erase_var( "RECIPE" );
            p.add_msg_if_player( m_good, _( "You got the %s from the %s." ), dish_name, it.tname() );

            return 0;
        }

        if( mc_start == choice ) {
            uilist dmenu;
            dmenu.text = _( "Choose desired recipe:" );

            std::vector<const recipe *> dishes;

            inventory crafting_inv = g->u.crafting_inventory();

            const time_point bday = calendar::start_of_cataclysm;
            for( const std::string& item : temporary_tools ) {
                crafting_inv.add_item( *item::spawn_temporary( item, bday ), false );
            }
            crafting_inv.update_quality_cache();

            int counter = 0;

            for( const auto& r : g->u.get_learned_recipes() ) {
                if( subcategories.contains( r->subcategory ) || recipes.contains( r->result() ) ) {
                    dishes.push_back( r );
                    const bool can_make = r->deduped_requirements().can_make_with_inventory(
                                              crafting_inv, r->get_component_filter() );
                    dmenu.addentry(
                        counter++, can_make, -1,
                        string_format(
                            _( "%s (%1.f charges)" ), r->result_name(),
                            r->time * time_mult / 6000 * charges_per_minute + charges_to_start ) );
                }
            }

            dmenu.query();

            int choice = dmenu.ret;

            if( choice < 0 ) {

                if( choice == -1024 ) {
                    p.add_msg_if_player( m_warning, _( "You don't know of anything you could craft "
                                                       "with this." ) );
                }

                return 0;
            } else {
                const recipe* meal = dishes[choice];

                uilist batchmenu;
                batchmenu.text = _( "Choose batch count:" );
                int counter = 0;

                for( int i = 1; i < 51; i++ ) {
                    const bool can_make = meal->deduped_requirements().can_make_with_inventory(
                                              crafting_inv, meal->get_component_filter(), i );
                    batchmenu.addentry(
                        counter++, can_make, -1,
                        string_format(
                            _( "%s batches (%1.f charges)" ), i,
                            meal->batch_time( i, 1, 0 ) * time_mult / 6000 * charges_per_minute
                            + charges_to_start ) );
                }

                batchmenu.query();

                int batchcount = batchmenu.ret;

                if( batchcount < 0 ) { return 0; }
                batchcount++;

                int mealtime = meal->batch_time( batchcount, 1, 0 ) * time_mult;
                int all_charges = mealtime / 6000 * charges_per_minute + charges_to_start;

                if( it.ammo_remaining() < all_charges ) {

                    p.add_msg_if_player(
                        m_warning, _( "The %s needs %d charges to create this." ), it.tname(),
                        all_charges );

                    return 0;
                }

                const auto filter = is_crafting_component;
                const requirement_data* reqs = meal->deduped_requirements().select_alternative(
                                                   p, crafting_inv, filter, batchcount );
                if( !reqs ) { return 0; }

                for( const auto& component : reqs->get_components() ) {
                    p.consume_items( component, batchcount, filter );
                }

                it.set_var( "RECIPE", meal->ident().str() );
                it.set_var( "RESULT", meal->result().str() );
                it.set_var( "COOKTIME", mealtime );
                it.set_var( "BATCHCOUNT", meal->makes_amount() * batchcount );

                p.add_msg_if_player( m_good, _( "The %s begins to hum." ), it.tname() );
                it.activate();

                return charges_to_start;
            }
        }
    }

    return 0;
}

std::unique_ptr<iuse_actor> multicooker_iuse::clone() const
{
    return std::make_unique<multicooker_iuse>( *this );
}

namespace
{
auto read_time_duration(
    const JsonObject& obj, const std::string& member, const time_duration& default_value )
-> time_duration
{
    if( !obj.has_member( member ) ) { return default_value; }
if( obj.has_string( member ) ) {
    return read_from_json_string<time_duration>( *obj.get_raw( member ), time_duration::units );
    }
    if( obj.has_int( member ) ) { return time_duration::from_turns( obj.get_int( member ) ); }
    obj.throw_error( "member must be a duration string or integer turns", member );
    return default_value;
}
} // namespace

auto hand_crank_actor::load( const JsonObject& obj ) -> void
{
    charge_interval = read_time_duration( obj, "charge_interval", charge_interval );
    obj.read( "charge_amount", charge_amount );
    obj.read( "fatigue_per_interval", fatigue_per_interval );
    obj.read( "ammo_type", ammo_type );
    obj.read( "activity_name", activity_name );
    obj.read( "start_message", start_message );
    obj.read( "already_charged_message", already_charged_message );
    obj.read( "need_battery_message", need_battery_message );
    obj.read( "underwater_message", underwater_message );
    obj.read( "exhausted_message", exhausted_message );
    obj.read( "fully_charged_message", fully_charged_message );
}

auto hand_crank_actor::can_use(
    const Character& who, const item& it, bool, const tripoint_bub_ms & ) const -> ret_val<bool>
{
    if( who.is_npc() ) { return ret_val<bool>::make_failure(); }
    if( who.is_underwater() ) { return ret_val<bool>::make_failure( _( underwater_message ) ); }
    if( who.get_fatigue() >= fatigue_levels::dead_tired ) {
    return ret_val<bool>::make_failure( _( exhausted_message ) );
    }
    const auto* magazine = it.magazine_current();
    if( !magazine || !magazine->has_flag( flag_RECHARGE ) ) {
    return ret_val<bool>::make_failure( _( need_battery_message ) );
    }
    return ret_val<bool>::make_success();
}

auto hand_crank_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const -> int
{
    if( p.is_npc() ) { return 0; }
if( p.is_underwater() ) {
    p.add_msg_if_player( m_info, _( underwater_message ) );
        return 0;
    }
    if( p.get_fatigue() >= fatigue_levels::dead_tired ) {
    p.add_msg_if_player( m_info, _( exhausted_message ) );
        return 0;
    }
    auto* magazine = it.magazine_current();
    if( !magazine || !magazine->has_flag( flag_RECHARGE ) ) {
    p.add_msg_if_player( m_info, _( need_battery_message ) );
        return 0;
    }
    if( it.ammo_capacity() > it.ammo_remaining() ) {
    p.add_msg_if_player( _( start_message ), it.tname(), magazine->tname() );
        auto resolved_charge_interval = charge_interval;
        if( resolved_charge_interval <= 0_turns ) { resolved_charge_interval = 144_seconds; }
        const auto safe_charge_amount = std::max( 1, charge_amount );
        const auto interval_turns = to_turns<int>( resolved_charge_interval );
        p.assign_activity( std::make_unique <
                           player_activity > ( std::make_unique<hand_crank_activity_actor>(
                                   safe_reference<item>( it ),
                                   std::vector<int> {interval_turns, safe_charge_amount, fatigue_per_interval},
                                   std::vector<std::string> {ammo_type.str(), fully_charged_message, exhausted_message} ) ) );
    } else {
        p.add_msg_if_player( _( already_charged_message ), it.tname(), magazine->tname() );
    }
    return 0;
}

auto hand_crank_actor::clone() const -> std::unique_ptr<iuse_actor>
{
    return std::make_unique<hand_crank_actor>( *this );
}

void sex_toy_actor::load( JsonObject const& obj )
{
    moves = obj.get_int( "moves", 60000 ); // default is 10 minutes
}

ret_val<bool> sex_toy_actor::can_use(
    const Character& c, const item& i, bool, const tripoint_bub_ms & ) const
{
    if( c.is_npc() ) {
    return ret_val<bool>::make_failure(); // Creepy, status quo
    }
    if( c.is_mounted() ) {
    return ret_val<bool>::make_failure( _( "You can't do *that* while mounted" ) );
    }
    if( ( c.is_underwater() )
            && ( !( ( c.has_trait( trait_id( "GILLS" ) ) ) || ( c.has_trait( trait_id( "GILLS_CEPH" ) ) )
                    || ( c.is_wearing( itype_id( "rebreather_on" ) ) )
                    || ( c.is_wearing( itype_id( "rebreather_xl_on" ) ) )
                    || ( c.is_wearing( itype_id( "mask_h20survivor_on" ) ) ) ) ) ) {
        return ret_val<bool>::make_failure( _( "Are you trying to drown yourself?" ) );
    }
    if( !i.units_sufficient( c ) ) {
    return ret_val<bool>::make_failure( _( "The %s's batteries are dead." ), i.tname() );
    }
    if( c.get_fatigue() >= fatigue_levels::dead_tired ) {
    return ret_val<bool>::make_failure( _( "*Your* batteries are dead." ) );
    }
    return ret_val<bool>::make_success();
}

std::unique_ptr<iuse_actor> sex_toy_actor::clone() const
{
    return std::make_unique<sex_toy_actor>( *this );
}

void train_skill_actor::load( JsonObject const& obj )
{
    training_skill = obj.get_string( "training_skill" );
    training_skill_min_level = obj.get_int( "training_skill_min_level", 0 );
    training_skill_xp = obj.get_int( "training_skill_xp", 0 );
    training_skill_xp_chance = obj.get_int( "training_skill_xp_chance", 0 );
    training_skill_max_level = obj.get_int( "training_skill_max_level", 0 );
    training_skill_fatigue = obj.get_int( "training_skill_fatigue", 0 );
    training_skill_interval = obj.get_int( "training_skill_interval", 0 );
    training_msg = obj.get_string( "training_msg" );
}

int train_skill_actor::use( player& p, item& i, bool, const tripoint_bub_ms & ) const
{
    if( i.ammo_remaining() < i.ammo_required() ) {
    p.add_msg_if_player( _( "This tool doesn't have enough charges." ) );
        return 0;
    }
    if( p.get_skill_level( skill_id( training_skill ) ) < training_skill_min_level ) {
    p.add_msg_if_player(
        _( "Your skill isn't high enough yet to train using that (requires %s %s)." ),
        training_skill_min_level, skill_id( training_skill )->name() );
        return 0;
    }
    if( p.get_skill_level( skill_id( training_skill ) ) >= training_skill_max_level ) {
    p.add_msg_if_player(
        _( "You can't train your %s beyond %s using that." ), skill_id( training_skill )->name(),
        training_skill_max_level );
        return 0;
    }

    int hours =
        string_input_popup()
        .title(
            string_format( _( "Train %s for how many hours?" ), skill_id( training_skill )->name() ) )
        .width( 3 )
        .text( "" )
        .only_digits( true )
        .query_int();

    if( hours <= 0 ) { return 0; }

p.add_msg_if_player( training_msg );
// using metadata is the easiest way to transfer this over to the activity handler and also
// allow it to function as furniture
p.set_value( "training_iuse_skill", training_skill );
p.set_value( "training_iuse_skill_xp", std::to_string( training_skill_xp ) );
p.set_value( "training_iuse_skill_xp_max_level", std::to_string( training_skill_max_level ) );
p.set_value( "training_iuse_skill_fatigue", std::to_string( training_skill_fatigue ) );
p.set_value( "training_iuse_skill_interval", std::to_string( training_skill_interval ) );
p.set_value( "training_iuse_skill_xp_chance", std::to_string( training_skill_xp_chance ) );
p.assign_activity(
    std::make_unique<player_activity>( std::make_unique<train_activity_actor>( "training", -1 ) ),
    hours * 360000 );

return 0;
}

std::unique_ptr<iuse_actor> train_skill_actor::clone() const
{
    return std::make_unique<train_skill_actor>( *this );
}

int sex_toy_actor::use( player& p, item& i, bool, const tripoint_bub_ms & ) const
{
    if( i.ammo_remaining() > 0 ) {
    p.add_msg_if_player( _( "You fire up your %s and start getting the tension out." ), i.tname() );
    } else {
        p.add_msg_if_player( _( "You whip out your %s and start getting the tension out." ), i.tname() );
    }
    p.assign_activity( std::make_unique<player_activity>(
                           std::make_unique<vibe_activity_actor>( safe_reference<item>( &i ) ) ) );

    return i.type->charges_to_use();
}

std::unique_ptr<iuse_actor> iuse_music_player::clone() const
{
    return std::make_unique<iuse_music_player>( *this );
}

void iuse_music_player::load( const JsonObject& obj )
{
    obj.read( "target", target, true );

    obj.read( "msg", msg_transform );

    obj.read( "moves", moves );
    if( moves < 0 ) { obj.throw_error( "transform actor specified negative moves", "moves" ); }

    obj.read( "need_charges", need_charges );
    need_charges = std::max( need_charges, 0 );
    obj.read( "transform_charges", transform_charges );

    obj.read( "need_worn", need_worn );
    obj.read( "need_wielding", need_wielding );
}

int iuse_music_player::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( t ) {
    return 0; // invoked from active item processing, do nothing.
}

const bool possess =
    p.has_item( it )
    || ( it.has_flag( flag_ALLOWS_REMOTE_USE ) && square_dist( p.bub_pos(), pos ) == 1 );

    if( possess && need_worn && !p.is_worn( it ) ) {
        p.add_msg_if_player(
            m_info, _( "You need to wear the %1$s before activating it." ), it.tname() );
        return 0;
    }
    if( possess && need_wielding && !p.is_wielding( it ) ) {
        p.add_msg_if_player(
            m_info, _( "You need to wield the %1$s before activating it." ), it.tname() );
        return 0;
    }
    // No charge consumption at this point, there are still points of failure later.
    if( need_charges || transform_charges ) {
    if( it.has_flag( flag_POWERARMOR_MOD ) && character_funcs::can_interface_armor( p ) ) {
            if( possess ) {
                const int bio_power = units::to_kilojoule( p.get_power_level() );
                if( bio_power < need_charges || bio_power < transform_charges ) {
                    p.add_msg_if_player(
                        m_info, "Your %s doesn't have enough battery to do that", it.tname() );
                    return 0;
                }
            } else {
                return 0;
            }
        } else {
            const int item_charges = it.units_remaining( p );
            if( item_charges < need_charges || item_charges < transform_charges ) {
                p.add_msg_if_player(
                    m_info, "Your %s doesn't have enough battery to do that", it.tname() );
                return 0;
            }
        }
    }

    // All checks complete the damn thing can finally transform
    // Consume charges if necessary at this point.
    if( transform_charges ) { p.consume_charges( it, transform_charges ); }

    if( possess && !msg_transform.empty() ) {
        p.add_msg_if_player( m_neutral, msg_transform, it.tname() );
    }
    // We want this separate and not if/else because the preceding statement will always return true
    // if a transform message is defined.
    if( p.is_npc() && get_player_character().sees( p ) ) {
        if( !it.has_flag( flag_COMBAT_NPC_ON ) ) {
            add_msg( m_info, _( "%s activates their %s." ), p.disp_name(), it.display_name() );
        } else {
            add_msg( m_info, _( "%s deactivates their %s." ), p.disp_name(), it.display_name() );
        }
    }

    if( possess ) { p.moves -= moves; }

// Update Luminosity as object is "removed"
get_map().update_lum( it, false );

if( p.is_worn( it ) ) { p.on_item_takeoff( it ); }
    it.convert( target );
    if( p.is_worn( it ) ) {
        p.reset_encumbrance();
        // This is most likely wrong: it doubles temperature shift for the turn!
        p.update_bodytemp( get_map(), get_weather() );
        p.on_item_wear( it );
    }
    p.inv_update_invlet_cache_with_item( it );
    // Update luminosity as object is "added"
    get_map().update_lum( it, true );
    it.activate();

    return 0;
}

ret_val<bool> iuse_music_player::can_use(
    const Character& p, const item &, bool, const tripoint_bub_ms & ) const
{
    if( p.has_effect( efftype_id( "music" ) ) ) {
    return ret_val<bool>::make_failure( _( "You can't listen to multiple music players at "
                                           "once!" ) );
    } else {
        return ret_val<bool>::make_success();
    }
}


ret_val<bool> iuse_prospect_pick::can_use(
    const Character& p, const item &, bool, const tripoint_bub_ms & ) const
{
    if( p.is_npc() ) {
    // Long action
    return ret_val<bool>::make_failure( _( "Actually how. You're an NPC. You can't do long "
                                           "actions. No." ) );
    }
    if( p.is_mounted() ) {
    return ret_val<bool>::make_failure( _( "You can't prospect from a vehicle!" ) );
    }
    if( p.is_underwater() ) {
    return ret_val<bool>::make_failure( _( "You can't prospect underwater!" ) );
    }
    return ret_val<bool>::make_success();
}

void iuse_prospect_pick::load( const JsonObject& obj ) { range = obj.get_int( "radius", 3 ); }
// TODO: this should probably take some time to do when skill is implimented, for now though, it
// just does.
int iuse_prospect_pick::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( t ) {
    // we're doing it still hold on.
    return 0;
}
//* begin edited map code*/
omt_find_params params{};
params.search_range = {0, range};
params.search_layers = omt_find_all_layers; // TODO: Find all levels -> find BELOW levels.

params.types = {std::make_pair( "empty_rock", ot_match_type::type )};
    params.existing_only = false;
    params.popup = make_shared_fast<throbber_popup>( _( "Please wait…" ) );
    params.seen = false;

    const point_abs_om origin_om_pos = project_to<coords::om>( p.abs_omt_pos().xy() );

    // Generate a Square fitting the requested map radius
    const point_abs_omt omt_bb_min = p.abs_omt_pos().xy() - point_rel_omt{range, range};
    const point_abs_omt omt_bb_max = p.abs_omt_pos().xy() + point_rel_omt{range, range};

    // OM Corners of bounding box
    const point_abs_om om_bb_min = project_to<coords::om>( omt_bb_min );
    const point_abs_om om_bb_max = project_to<coords::om>( omt_bb_max );

    // Iterate through range [om_bb_min, om_bb_max] to get the OM we want, then sort by manhattan
    // distance
    std::map<int, std::vector<point_abs_om>> om_to_generate;
    for( int x = om_bb_min.x(); x <= om_bb_max.x(); ++x ) {
        for( int y = om_bb_min.y(); y <= om_bb_max.y(); ++y ) {
            auto dist = manhattan_dist( origin_om_pos, {x, y} );
            auto& vec = om_to_generate[dist]; // if the vector for this distance doesn't exist it
            // will be created empty
            vec.emplace_back( x, y );
        }
    }

for( const auto& [_, to_gen] : om_to_generate ) {
        get_overmapbuffer( p.get_dimension() ).generate( to_gen );
    }

    const auto places = get_overmapbuffer( p.get_dimension() ).find_all( p.abs_omt_pos(), params );
for( auto& place : places ) { get_overmapbuffer( p.get_dimension() ).reveal( place, 0 ); }
    //* end edited map code */
    p.add_msg_if_player(
        m_info,
        _( "You use the %s to gather a few samples and gauge where minerals may lie nearby." ),
        it.tname() );
    return 0;
}
std::unique_ptr<iuse_actor> iuse_prospect_pick::clone() const
{
    return std::make_unique<iuse_prospect_pick>( *this );
}

void iuse_reveal_contents::load( const JsonObject& obj )
{
    obj.read( "group", contents_group );
    if( obj.has_member( "open_message" ) ) { obj.read( "open_message", open_message ); }
}
int iuse_reveal_contents::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    std::vector<detached_ptr<item>> items = item_group::items_from( contents_group, calendar::turn );
    map& here = get_map();
    for( detached_ptr<item> &content : items ) {
        if( !open_message.empty() ) {
            p.add_msg_if_player( ( string_format( open_message, it.tname() ) + content->tname() + "!" ) );
        }
        here.add_item_or_charges( p.bub_pos(), std::move( content ) );
    }

    it.detach();

    return 0;
}
std::unique_ptr<iuse_actor> iuse_reveal_contents::clone() const
{
    return std::make_unique<iuse_reveal_contents>( *this );
}

