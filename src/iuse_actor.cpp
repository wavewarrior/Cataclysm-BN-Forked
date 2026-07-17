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

void iuse_flowerpot_plant::load( const JsonObject& jo )
{
    jo.read( "stages", stages );
    growth_rate = jo.get_float( "growth_rate", 1.0 );
    fert_boost = jo.get_float( "fert_boost", 1.5 );
    harvest_mult = jo.get_float( "harvest_mult", 1 );

    if( jo.has_array( "seeds_per_use" ) ) {
        auto arr = jo.get_int_array( "seeds_per_use" );
        seeds_per_use = std::make_pair( arr[0], arr[1] );
    } else if( jo.has_int( "seeds_per_use" ) ) {
        auto val = jo.get_int( "seeds_per_use" );
        seeds_per_use = std::make_pair( val, val );
    } else {
        seeds_per_use = std::make_pair( 1, 1 );
    }

    if( jo.has_array( "fert_per_use" ) ) {
        auto arr = jo.get_int_array( "fert_per_use" );
        fert_per_use = std::make_pair( arr[0], arr[1] );
    } else if( jo.has_int( "fert_per_use" ) ) {
        auto val = jo.get_int( "fert_per_use" );
        fert_per_use = std::make_pair( val, val );
    } else {
        fert_per_use = std::make_pair( 0, 1 );
    }

    if( jo.has_array( "terrain" ) ) { terrain = jo.get_tags<std::string>( "terrain" ); }
}

auto iuse_flowerpot_plant::clone() const -> std::unique_ptr<iuse_actor>
{
    return std::make_unique<iuse_flowerpot_plant>( *this );
}

auto iuse_flowerpot_plant::growth_info::elapsed_time() const -> time_duration
{
    return calendar::turn - planted_time;
}

auto iuse_flowerpot_plant::growth_info::remaining_time() const -> time_duration
{
    return epoch - elapsed_time();
}

auto iuse_flowerpot_plant::growth_info::stage() const -> growth_stage
{
    if( epoch <= time_duration{} ) { return empty; }

    const auto stage = to_turns<int>( elapsed_time() ) * 3 / to_turns<int>( epoch );
    switch( std::clamp( stage, 0, 3 ) ) {
        case 0:
            return seed;
        case 1:
            return seedling;
        case 2:
            return mature;
        case 3:
            return harvest;
        default:
            return empty;
    }
}

auto iuse_flowerpot_plant::growth_info::plant_name() const -> std::string
{
    return seed_id.obj().seed->plant_name.translated();
}

auto iuse_flowerpot_plant::growth_info::progress() const -> double
{
    return elapsed_time() / epoch;
}

auto iuse_flowerpot_plant::use( player& who, item& i, bool tick, const tripoint_bub_ms& pos ) const
-> int
{
    if( tick ) { return on_tick( who, i, pos ); }

    const auto info = get_info( i );
    switch( info.stage() ) {
        case seed:
        case seedling:
        case mature:
            return on_use_add_fertilizer( who, i, pos );
        case harvest:
            return on_use_harvest( who, i, pos );
        default:
            return on_use_plant( who, i, pos );
    }
}

auto iuse_flowerpot_plant::can_use(
    const Character& who, const item& i, bool, const tripoint_bub_ms & ) const -> ret_val<bool>
{

    const auto info = get_info( i );
    switch( info.stage() ) {
        case seed:
        case seedling:
        case mature: {
            const bool can_add_fert = info.fert_amt < fert_per_use.second;
            const bool has_fert = i.charges > 0;
            if( !can_add_fert ) {
                return ret_val<bool>::make_failure( _( "You need to wait for it to grow." ) );
            }
            if( !has_fert ) {
                return ret_val<bool>::make_failure( _( "You don't have enough fertilizer." ) );
            }
            return ret_val<bool>::make_success();
        }
        case harvest:
            return ret_val<bool>::make_success();
        default: {
            if( !who.has_item_with( []( const item & itm ) { return itm.is_seed(); } ) ) {
                return ret_val<bool>::make_failure( _( "You have no seeds to plant." ) );
            }
            if( i.charges < fert_per_use.first ) {
                return ret_val<bool>::make_failure( _( "You don't have enough fertilizer." ) );
            }
            return ret_val<bool>::make_success();
        }
    }
}

void iuse_flowerpot_plant::info( const item& i, std::vector<iteminfo> &inf ) const
{
    const auto info = get_info( i );
    if( !info.seed_id.is_valid() ) { return; }

    const auto plant_name = info.plant_name();

    inf.emplace_back( "TOOL", string_format( _( "<bold>Growing</bold>: %s" ), plant_name ) );
    switch( info.stage() ) {
        case seed:
            inf.emplace_back( "TOOL", string_format( _( "<bold>Stage</bold>: %s" ), _( "seed" ) ) );
            break;
        case seedling:
            inf.emplace_back( "TOOL", string_format( _( "<bold>Stage</bold>: %s" ), _( "seedling" ) ) );
            break;
        case mature:
            inf.emplace_back( "TOOL", string_format( _( "<bold>Stage</bold>: %s" ), _( "mature" ) ) );
            break;
        case harvest:
            inf.emplace_back( "TOOL", string_format( _( "<bold>Stage</bold>: %s" ), _( "harvest" ) ) );
            break;
        default:
            break;
    }
    if( i.is_active() ) {
        inf.emplace_back(
            "TOOL",
            string_format(
                _( "<bold>Progress</bold>: %d%%" ), static_cast<int>( 100 * info.progress() ) ) );
        inf.emplace_back(
            "TOOL",
            string_format(
                _( "<bold>Harvestable in</bold>: %s" ), to_string_approx( info.remaining_time() ) ) );
    }
    if( info.seed_id.is_valid() ) {
        inf.emplace_back(
            "TOOL",
            string_format( _( "<bold>Seeds</bold>: %d/%d" ), info.seed_amt, seeds_per_use.second ) );
        inf.emplace_back(
            "TOOL",
            string_format( _( "<bold>Fertilizer</bold>: %d/%d" ), info.fert_amt, fert_per_use.second ) );
        inf.emplace_back(
            "TOOL",
            string_format( _( "<bold>Growth</bold>: %d%%" ),
                           static_cast<int>( ( growth_rate + ( info.fert_amt * fert_boost ) ) * 100 ) ) );
        inf.emplace_back(
            "TOOL",
            string_format( _( "<bold>Yield</bold>: %d%%" ), static_cast<int>( harvest_mult * 100 ) ) );
    }
}

auto iuse_flowerpot_plant::on_use_add_fertilizer( player &, item& i, const tripoint_bub_ms & ) const
-> int
{

    const auto info = get_info( i );
    const int fert_to_add = std::min( i.charges, fert_per_use.second - info.fert_amt );
    const auto new_fert_amt = info.fert_amt + fert_to_add;
    const auto old_prog = info.progress();
    const auto new_epoch = calculate_growth_time( info.seed_id, new_fert_amt );
    const auto new_age = new_epoch * old_prog;
    const auto new_date = calendar::turn - new_age;

    set_growing_plant( i, info.seed_id, new_date, info.seed_amt, new_fert_amt );

    return fert_to_add;
}


auto iuse_flowerpot_plant::on_use_plant( player& p, item& i, const tripoint_bub_ms & ) const -> int
{
    const std::vector<item *> seed_inv = p.items_with( []( const item & itm ) { return itm.is_seed(); } );

    const auto& [min_seed, max_seed] = seeds_per_use;
    const auto& [min_fert, max_fert] = fert_per_use;

    auto seed_entries = std::vector<seed_tuple> {};
    std::ranges::copy_if(
        iexamine::get_seed_entries( seed_inv ), std::back_inserter( seed_entries ),
    [&]( const seed_tuple & s ) {
        const auto& [type, name, cnt] = s;
        return terrain.contains( type->seed->required_terrain_flag );
    } );

    if( seed_entries.empty() ) {
        add_msg( _( "You don't have seeds to plant in this." ) );
        return 0;
    }

    const int seed_index = iexamine::query_seed( seed_entries, min_seed );

    if( seed_index < 0 || std::cmp_greater_equal( seed_index, seed_entries.size() ) ) {
        add_msg( _( "You saved your seeds for later." ) );
        return 0;
    }
    const auto& [seed_id, seed_name, seed_amt] = seed_entries[seed_index];

    const int used_fert = std::min( i.charges, max_fert );

    auto comps = p.use_charges( seed_id, max_seed );
    constexpr auto count_fn = []( const detached_ptr<item> &it ) {
        return it->count_by_charges() ? it->charges : 1;
    };
    const auto used_seeds =
        std::ranges::fold_left( comps | std::views::transform( count_fn ), 0, std::plus<int> {} );
    set_growing_plant( i, seed_id, calendar::turn, used_seeds, used_fert );
    update( i );

    return used_fert;
}

auto iuse_flowerpot_plant::on_use_harvest( player& p, item& i,
        const tripoint_bub_ms & ) const -> int
{
    const auto info = get_info( i );
    clear_growing_plant( i );
    update( i );

    const int skillLevel = p.get_skill_level( skill_survival );
    const int max_harvest_count = get_option<int>( "MAX_HARVEST_COUNT" );

    // since a modded item could consume 10 seeds to produce 10 times the fruit
    // we roll n times the harvest
    std::vector<detached_ptr<item>> harvest;
    int practice = 0;

    for( int j = 0; j < info.seed_amt; j++ ) {
        int fruit_count = rng_float( skillLevel / 2.0, skillLevel ) * info.harvest_mult;
        fruit_count = std::clamp( fruit_count, 1, max_harvest_count );
        const int seed_count = std::max( 1, rng( fruit_count / 4, fruit_count / 2 ) );
        practice += fruit_count;

        auto tmp = iexamine::get_harvest_items( info.seed_id.obj(), fruit_count, seed_count, true );
        std::ranges::move( tmp, std::back_inserter( harvest ) );
    }

    for( auto& j : harvest ) {
        put_into_vehicle_or_drop( p, item_drop_reason::deliberate, std::move( j ), p.bub_pos() );
    }

    p.moves -= to_moves<int>( 10_seconds * info.harvest_mult );
    p.practice( skill_survival, rng( 1, practice ) );
    return 0;
}

auto iuse_flowerpot_plant::on_tick( player &, item& i, const tripoint_bub_ms & ) const -> int
{
    if( i.get_counter() != 0 ) { return 0; }

    update( i );
    return 0;
}

void iuse_flowerpot_plant::update( item& i ) const
{
    const auto info = get_info( i );
    if( !info.seed_id.is_valid() ) {
        clear_growing_plant( i );
        i.set_counter( 0 );
        i.convert( stages[0] );
        i.erase_var( "item_label" );
        i.deactivate();
        return;
    }

    i.convert( stages[info.stage()] );
    i.set_var( "item_label", string_format( "%s (%s)", stages[0]->nname( 1 ), info.plant_name() ) );
    switch( info.stage() ) {
        case 0:
            i.deactivate();
            i.set_counter( 0 );
            break;
        case 4:
            i.deactivate();
            i.set_counter( 0 );
            break;
        default:
            i.activate();
            i.set_counter( to_turns<int>( std::min( info.remaining_time(), 1_hours ) ) );
            break;
    }
}

void iuse_flowerpot_plant::set_growing_plant(
    item& i, const itype_id seed, const time_point planted_time, const int seeds,
    const int fertilizer )
{
    if( seed.is_valid() ) {
        i.set_var( VAR_SEED_TYPE, seed.str() );
        i.set_var( VAR_PLANTED_DATE, to_turn<int>( planted_time ) );
        i.set_var( VAR_SEED_AMT, seeds );
        i.set_var( VAR_FERT_AMT, fertilizer );
    } else {
        clear_growing_plant( i );
    }
}

void iuse_flowerpot_plant::clear_growing_plant( item& i )
{
    i.erase_var( VAR_SEED_TYPE );
    i.erase_var( VAR_PLANTED_DATE );
    i.erase_var( VAR_SEED_AMT );
    i.erase_var( VAR_FERT_AMT );
}

auto iuse_flowerpot_plant::query_adjacent_pot( const player& who, bool empty )
-> std::optional<item *>
{
    const auto selector_fn = empty ? empty_pot_selector : full_pot_selector;
    const auto p_selector_fn = [&]( const item * it ) { return selector_fn( *it ); };

    auto& map = get_map();
    const auto has_inv_pots = who.has_item_with( selector_fn );
    const auto has_map_pots = map.has_adjacent_item_with( who.bub_pos(), selector_fn );

    if( !has_inv_pots && !has_map_pots ) { return std::nullopt; }

    std::optional<tripoint_bub_ms> pot_pos;
    if( has_map_pots ) {
        const auto fn = [&]( const tripoint_bub_ms & p ) {
            bool ok = false;
            ok |= map.has_item_with( p, selector_fn );
            ok |= ( who.bub_pos() == p ) && who.has_item_with( selector_fn );
            return ok;
        };

        pot_pos = choose_adjacent_highlight( _( "Which planter?" ), _( "Never mind." ), fn );
    } else if( has_inv_pots ) {
        pot_pos = who.bub_pos();
    }

    if( !pot_pos.has_value() ) { return std::nullopt; }


    std::vector<item *> choices{};
    const auto map_stack = map.i_at( pot_pos.value() );
    std::ranges::copy_if( map_stack, std::back_inserter( choices ), p_selector_fn );
    if( pot_pos.value() == who.bub_pos() ) {
        std::ranges::copy( who.items_with( selector_fn ), std::back_inserter( choices ) );
    }

    if( choices.empty() ) { return std::nullopt; }

    if( choices.size() > 1 ) {
        uilist lst;
        for( const auto i : choices ) { lst.addentry( i->display_name() ); }
        lst.query();

        if( lst.ret < 0 ) { return std::nullopt; }

        return choices[lst.ret];
    }

    return choices[0];
}

auto iuse_flowerpot_plant::get_info( const item& i ) const -> growth_info
{
    const auto seed_id = itype_id( i.get_var( VAR_SEED_TYPE, "" ) );
    if( !seed_id.is_valid() ) { return growth_info{}; }

    const int num_seeds = i.get_var( VAR_SEED_AMT, 1 );
    const int fert_amt = i.get_var( VAR_FERT_AMT, 1 );
    const auto planted_time = time_point::from_turn(
                                  i.get_var( VAR_PLANTED_DATE, to_turn<int>( calendar::turn ) ) );

    const auto growth_time = calculate_growth_time( seed_id, fert_amt );

    return growth_info{seed_id, planted_time, growth_time, harvest_mult, fert_amt, num_seeds};
}

auto iuse_flowerpot_plant::calculate_growth_time( const itype_id& seed_id,
        const int used_fert ) const
-> time_duration
{
    const auto epoch = seed_id->seed->get_plant_epoch() * 3;
    const auto rate = growth_rate + ( used_fert * fert_boost );
    const auto growth_time = epoch / rate;

    return growth_time;
}

auto iuse_flowerpot_plant::full_pot_selector( const item& it ) -> bool
{
    if( !it.type->can_use( IUSE_ACTOR ) ) { return false; }

const auto actor = dynamic_cast<const iuse_flowerpot_plant *>(
                       it.get_use( IUSE_ACTOR )->get_actor_ptr() );
if( actor == nullptr ) { return false; }

const auto info = actor->get_info( it );
return info.stage() != empty;
}

auto iuse_flowerpot_plant::empty_pot_selector( const item& it ) -> bool
{
    if( !it.type->can_use( IUSE_ACTOR ) ) { return false; }

const auto actor = dynamic_cast<const iuse_flowerpot_plant *>(
                       it.get_use( IUSE_ACTOR )->get_actor_ptr() );
if( actor == nullptr ) { return false; }

const auto info = actor->get_info( it );
return info.stage() == empty;
}

void iuse_flowerpot_collect::load( const JsonObject & ) {}

auto iuse_flowerpot_collect::use( player& who, item &, bool, const tripoint_bub_ms & ) const -> int
{
    constexpr auto get_harvestable_furn = []( const tripoint_bub_ms & here ) {
        const auto& map = get_map();
        return map.has_flag( "PLANT", here );
    };

    const auto source_pos_opt = choose_adjacent_highlight(
                                    _( "Transplant what?" ), _( "There is nothing that can be collected nearby." ),
                                    get_harvestable_furn, false );

    if( !source_pos_opt.has_value() ) { return 0; }

    const auto source_pos = source_pos_opt.value();
    if( !source_pos_opt.has_value() ) { return 0; }

    const auto target_pot = iuse_flowerpot_plant::query_adjacent_pot( who, true );
    if( !target_pot.has_value() ) { return 0; }

    const auto actor = dynamic_cast<const iuse_flowerpot_plant *>(
                           target_pot.value()->get_use( iuse_flowerpot_plant::IUSE_ACTOR )->get_actor_ptr() );
    if( !actor ) {
        debugmsg( "Invalid iuse_actor" );
        return 0;
    }

    auto stack = get_map().i_at( source_pos );

    constexpr auto is_seed = []( const item * it ) { return it->is_seed(); };
    const auto seed_it = std::ranges::find_if( stack, is_seed );
    if( seed_it == stack.end() ) {
        debugmsg( "Missing seed" );
        return 0;
    }

    const item* seed = *seed_it;
    if( !actor->terrain.contains( seed->type->seed->required_terrain_flag ) ) {
        add_msg( "You can't collect that into this planter." );
        return 0;
    }

    // TODO: make an activity actor?
    who.moves -= to_turns<int>( 30_seconds );
    transfer_map_to_flowerpot( source_pos, *target_pot.value(), actor, seed->typeId() );

    return 0;
}

void iuse_flowerpot_collect::transfer_map_to_flowerpot(
    const tripoint_bub_ms& map_pos, item& flowerpot, const iuse_flowerpot_plant* actor,
    const itype_id& seed_type )
{
    auto& m = get_map();

    const auto furn_id = m.furn( map_pos );

    if( !furn_id->plant ) {
        debugmsg( "Invalid plant_data" );
        return;
    }

    auto stack = m.i_at( map_pos );

    const auto is_seed = [&]( const item * it ) { return it->typeId() == seed_type; };
    const auto seed_it = std::ranges::find_if( stack, is_seed );
    if( seed_it == stack.end() ) {
        debugmsg( "Missing seed" );
        return;
    }
    item* seed = *seed_it;

    auto max_seeds = actor->seeds_per_use.second;
    auto max_fert = actor->fert_per_use.second;

    std::vector<detached_ptr<item>> comps;
    stack.remove_top_items_with( [&]( detached_ptr<item>&& it ) {
        if( max_seeds > 0 && it->typeId() == seed_type ) {
            // Move the seeds
            return item::use_charges( std::move( it ), seed_type, max_seeds, comps, map_pos );
        }
        if( max_fert > 0 && it->typeId() == itype_fertilizer ) {
            // Clone the fertilizer
            auto tmp = item::spawn( *it );
            item::use_charges( std::move( tmp ), itype_fertilizer, max_fert, comps, map_pos );
        }
        return std::move( it );
    } );

    // Erase fertilizer and reset furniture if no more seeds
    if( std::ranges::find_if( stack, is_seed ) == stack.end() ) {
        m.furn_set( map_pos, furn_id->plant->base );
        stack.remove_top_items_with( []( detached_ptr<item>&& it ) {
            if( it->typeId() == itype_fertilizer ) { return detached_ptr<item> {}; }
            return std::move( it );
        } );
    }

    const auto fert_amt = actor->fert_per_use.second - max_fert;
    const auto seed_amt = actor->seeds_per_use.second - max_seeds;

    const auto old_epoch = seed->get_plant_epoch() * 3 * furn_id->plant->growth_multiplier;
    const auto old_pct = seed->age() / old_epoch;

    const auto new_epoch = actor->calculate_growth_time( seed->typeId(), fert_amt );
    const auto new_age = new_epoch * old_pct;
    seed->set_age( new_age );

    actor->set_growing_plant( flowerpot, seed->typeId(), seed->birthday(), seed_amt, fert_amt );
    actor->update( flowerpot );
}

auto iuse_flowerpot_collect::can_use(
    const Character& who, const item &, bool, const tripoint_bub_ms& pos ) const -> ret_val<bool>
{
    const bool has_empty_pot_inv = who.has_item_with( iuse_flowerpot_plant::empty_pot_selector );
    const bool has_empty_pot_near =
        get_map().has_adjacent_item_with( pos, iuse_flowerpot_plant::empty_pot_selector );
    const bool has_plant_furn = get_map().has_adjacent_furniture_with( pos, []( const furn_t &f ) {
        return f.has_flag( "PLANT" );
    } );

    if( ( has_empty_pot_inv || has_empty_pot_near ) && has_plant_furn ) {
        return ret_val<bool>::make_success();
    }

    /*
    const bool has_full_pot = who.has_item_with( iuse_flowerpot_plant::full_pot_selector );
    const bool has_empty_furn = get_map().has_adjacent_furniture_with( pos, []( const furn_t &f ) {
        return f.has_flag( "PLANTABLE" );
    } );
    const bool has_empty_ter = get_map().has_adjacent_terrain_with( pos, []( const ter_t & t ) {
        return t.has_flag( "PLANTABLE" );
    } );

    if( has_full_pot && ( has_empty_furn || has_empty_ter ) ) {
        return ret_val<bool>::make_success();
    }
    */

    return ret_val<bool>::make_failure();
}

auto iuse_flowerpot_collect::clone() const -> std::unique_ptr<iuse_actor>
{
    return std::make_unique<iuse_flowerpot_collect>( *this );
}

std::unique_ptr<iuse_actor> iuse_dimension_travel::clone() const
{
    return std::make_unique<iuse_dimension_travel>( *this );
}

void iuse_dimension_travel::load( const JsonObject& obj )
{
    if( obj.has_string( "destination" ) ) {
        destination = world_type_id( obj.get_string( "destination" ) );
    }
    obj.read( "travel_radius", travel_radius );
    obj.read( "need_charges", need_charges );
    obj.read( "fail_message", fail_message );
    obj.read( "success_message", success_message );
    if( travel_radius < 1 ) {
        obj.throw_error( "dimension_travel actor specified travel_radius less than 1", "travel_"
                         "radius" );
    }
}

int iuse_dimension_travel::use( player& p, item& it, bool, const tripoint_bub_ms& pos ) const
{
    dimension_travel( p, it, pos );
    return need_charges;
}

ret_val<bool> iuse_dimension_travel::can_use(
    const Character &, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( it.ammo_remaining() < need_charges ) {
    return ret_val<bool>::make_failure( _( "The %s doesn't have enough charges." ), it.tname() );
    }
    return ret_val<bool>::make_success();
}

void iuse_dimension_travel::dimension_travel( player& p, item &, const tripoint_bub_ms& pos ) const
{
    if( destination.is_empty() ) {
    p.add_msg_if_player( m_bad, _( "This item has no destination configured." ) );
        return;
    }
    if( !destination.is_valid() ) {
    debugmsg( "iuse_dimension_travel: destination '%s' is not a valid world_type",
              destination.str() );
        return;
    }

    // Debug: Show current and target dimensions
    add_msg( m_debug, "[DIM_TRAVEL] Current region_type: %s",
             get_overmapbuffer( p.get_dimension() ).current_region_type );
    add_msg( m_debug, "[DIM_TRAVEL] Current dim_id: '%s'", g->get_current_dimension_id() );
    add_msg( m_debug, "[DIM_TRAVEL] Target destination: %s", destination.str() );

    // The "default" world_type_id is the base overworld; its canonical dim_id is ""
    // (empty string) for backward-compat save paths.  Normalize here so callers
    // that specify destination="default" correctly reach the overworld slot.
    const auto target_dim_id = destination.str() == "default" ? std::string{} :
                               destination.str();

    // Check if already in target dimension
    if( g->get_current_dimension_id() == target_dim_id ) {
    p.add_msg_if_player( m_info, _( "You are already in that dimension." ) );
        add_msg( m_debug, "[DIM_TRAVEL] Already in target dimension" );
        return;
    }

    avatar& u = get_avatar();

    // Check if avatar is within travel radius
    const int dist_to_avatar = rl_dist( pos, u.bub_pos() );
    if( dist_to_avatar > travel_radius ) {
    if( fail_message.empty() ) {
            p.add_msg_if_player( m_bad, _( "You are too far from the portal!" ) );
        } else {
            p.add_msg_if_player( m_bad, "%s", _( fail_message ) );
        }
        return;
    }

    if( success_message.empty() ) {
    p.add_msg_if_player( m_good, _( "You travel to another dimension!" ) );
    } else {
        p.add_msg_if_player( m_good, "%s", _( success_message ) );
    }

    // Travel to the destination world type.
    // NPCs and vehicles do not travel between dimensions.
    std::optional<tripoint_abs_sm> load_pos;
    // Handled in this way so we can move the player to the appropriate location when
    // non-pocket dimension item travel is done.
    std::optional<tripoint_abs_ms> abs_pos;

    if( const dimension_info * info = g->get_current_dimension_info();
    info && info->pocket_info.has_value() ) {
    // Bounded pocket: restore the saved overworld origin position.
    load_pos = info->pocket_info.value().get_preload_point();
    } else {
        // Scaled dimension: remap player coordinates through the overworld ("") as the
        // common reference frame.  scale_num:scale_den describes each dimension relative
        // to a 1:1 overworld baseline, so the two-step conversion is:
        //   current → overworld: pos * src_num / src_den
        //   overworld → target:  pos * dst_den / dst_num
        // Combined:              pos * src_num * dst_den / (src_den * dst_num)
        int src_num = 1;
        int src_den = 1;
        if( const dimension_info * info = g->get_current_dimension_info();
            info && info->world_type.is_valid() ) {
            src_num = info->world_type.obj().scale_num;
            src_den = info->world_type.obj().scale_den;
        }

        // Only set load_pos when at least one side has a non-trivial scale.
        // Cross-multiply to compare ratios without floating point.
        if( src_num * destination.obj().scale_den != src_den * destination.obj().scale_num ) {
            const int scalar =
                src_num * destination.obj().scale_den / ( src_den * destination.obj().scale_num );
            abs_pos = tripoint_abs_ms( p.abs_pos().raw() * scalar );
            load_pos = project_to<coords::sm>( abs_pos.value() )
                       - tripoint_rel_sm( g_half_mapsize, g_half_mapsize, 0 );
        }
    }

    g->travel_to_dimension( target_dim_id, destination, std::nullopt, load_pos );

    if( abs_pos.has_value() ) { p.setpos( abs_to_bub( abs_pos.value() ) ); }
}

std::unique_ptr<iuse_actor> iuse_pocket_dimension::clone() const
{
    return std::make_unique<iuse_pocket_dimension>( *this );
}

void iuse_pocket_dimension::load( const JsonObject& obj )
{
    if( obj.has_string( "pocket_type" ) ) {
        pocket_type = world_type_id( obj.get_string( "pocket_type" ) );
    }
    obj.read( "entry_mapgen", entry_mapgen );
    obj.read( "persistent", persistent );
    obj.read( "need_charges", need_charges );
    obj.read( "pocket_name", pocket_name );
    if( obj.has_string( "boundary_terrain" ) ) {
        boundary_terrain = ter_str_id( obj.get_string( "boundary_terrain" ) );
    }
    if( obj.has_float( "lifetime_hours" ) ) {
        lifetime = time_duration::from_hours( obj.get_float( "lifetime_hours" ) );
    }
}

int iuse_pocket_dimension::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    // If pocket is not initialized, initialize it on first use
    if( !it.pocket_dim.has_value() || !it.pocket_dim->pocket_info.has_value()
    || !it.pocket_dim->pocket_info->is_initialized ) {
    initialize_pocket( it );
        if( !it.pocket_dim.has_value() || !it.pocket_dim->pocket_info.has_value()
            || !it.pocket_dim->pocket_info->is_initialized ) {
            p.add_msg_if_player( m_bad, _( "Failed to initialize the pocket dimension." ) );
            return 0;
        }
    }
    auto& dim_info = *it.pocket_dim;
    auto& pd = *dim_info.pocket_info;

    // Determine if we're inside this pocket or outside
    const auto& current_dim_id = g->get_current_dimension_id();

    // Check if we're inside THIS pocket dimension
    if( current_dim_id == dim_info.dimension_id ) {
    // We're inside - exit to return point
    exit_pocket( p, it );
    } else if( current_dim_id == pd.return_dimension_id ) {
    // We're in the dimension we last entered from - re-enter (ignoring last position)
    enter_pocket( p, it );
    } else {
        p.add_msg_if_player( m_info, _( "You can only use this to return from or re-enter this "
                                        "pocket." ) );
        return 0;
    }

    return need_charges;
}

ret_val<bool> iuse_pocket_dimension::can_use(
    const Character &, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( it.ammo_remaining() < need_charges ) {
    return ret_val<bool>::make_failure( _( "The %s doesn't have enough charges." ), it.tname() );
    }
    // Temporary pocket: refuse entry if the pocket has expired.
    if( it.pocket_dim.has_value() && it.pocket_dim->pocket_info.has_value() ) {
    const auto& pd = *it.pocket_dim->pocket_info;
    if( pd.lifetime.has_value() && pd.last_player_exit.has_value() ) {
            if( *pd.last_player_exit + *pd.lifetime < calendar::turn ) {
                return ret_val<bool>::make_failure(
                           _( "The %s is cold and inert — the pocket dimension has collapsed." ),
                           it.tname() );
            }
        }
    }
    return ret_val<bool>::make_success();
}

void iuse_pocket_dimension::initialize_pocket( item& it ) const
{
    if( !pocket_type.is_valid() ) {
    debugmsg( "iuse_pocket_dimension: invalid pocket_type %s", pocket_type.str() );
        return;
    }

    auto pd = dimension_info{};

    // Build a fully-qualified dimension_id from the pocket_type's save_prefix + a unique suffix.
    const auto instance_suffix =
        string_format( "%d_%d", to_turn<int>( calendar::turn ), rng( 0, 99999 ) );
    pd.dimension_id = pocket_type.obj().save_prefix + instance_suffix + "_";
    pd.world_type = pocket_type;
    pd.display_name = pocket_name.empty() ? pocket_type.obj().name.translated() : pocket_name;
    pd.pocket_info = pocket_dimension_data{};
    auto& pocket_data = *pd.pocket_info;
    pocket_data.is_initialized = true;

    // Record the dimension the pocket returns to when exiting.
    pocket_data.return_dimension_id = g->get_current_dimension_id();
    if( const auto * info = g->get_current_dimension_info() ) {
    pocket_data.return_world_type = info->world_type;
} else {
    // Currently in the overworld; no explicit world_type needed.
    pocket_data.return_world_type = world_type_id{};
}

// The return point will be set when entering

// Calculate bounds from entry_mapgen (overmap_special)
overmap_special_id special_id( entry_mapgen );
if( special_id.is_valid() ) {
    const auto& special = special_id.obj();
        auto locations = special.required_locations();

        if( !locations.empty() ) {
            // Find min and max coordinates across all locations
            auto min_pos = locations[0].p;
            auto max_pos = locations[0].p;

            std::ranges::for_each( locations, [&]( const auto & loc ) {
                min_pos.x() = std::min( min_pos.x(), loc.p.x() );
                min_pos.y() = std::min( min_pos.y(), loc.p.y() );
                min_pos.z() = std::min( min_pos.z(), loc.p.z() );
                max_pos.x() = std::max( max_pos.x(), loc.p.x() );
                max_pos.y() = std::max( max_pos.y(), loc.p.y() );
                max_pos.z() = std::max( max_pos.z(), loc.p.z() );
            } );

            // Set bounds based on the special's extent
            // The special's coordinates are relative, so we use them directly
            pocket_data.bounds.min_bound =
                tripoint_abs_sm::zero() + project_to<coords::sm>( min_pos );
            pocket_data.bounds.max_bound =
                tripoint_abs_sm::south_east() + project_to<coords::sm>( max_pos );

        } else {
            debugmsg( "iuse_pocket_dimension: overmap_special '%s' has no locations", entry_mapgen );
        }
    } else {
        debugmsg( "iuse_pocket_dimension: invalid entry_mapgen '%s'", entry_mapgen );
    }

    // Propagate lifetime from actor definition to the item's persistent data.
    if( lifetime.has_value() ) { pocket_data.lifetime = *lifetime; }

// Priority: actor-level override > world_type > hardcoded default
if( boundary_terrain && boundary_terrain->is_valid() ) {
    pocket_data.bounds.boundary_terrain = *boundary_terrain;
} else {
    pocket_data.bounds.boundary_terrain = pocket_type.obj().boundary_terrain.value_or(
                ter_str_id( "t_pd_border" ) );
    }
    pocket_data.bounds.boundary_overmap_terrain = oter_str_id( "pd_border" );

    it.pocket_dim = pd;
}

// Helper function to find a safe, passable position near the target
static tripoint_bub_ms find_safe_spawn( const tripoint_bub_ms& target )
{
    map& here = get_map();

    // First check if the target itself is passable
    if( here.passable( target ) && !g->critter_at( target ) ) { return target; }

    // Search in expanding radius for a passable spot
    for( int radius = 1; radius <= 10; radius++ ) {
        for( const tripoint_bub_ms& p : here.points_in_radius( target, radius ) ) {
            if( here.passable( p ) && !g->critter_at( p ) ) { return p; }
        }
    }

    // If no safe spot found, return target anyway (will be handled by game)
    return target;
}

void iuse_pocket_dimension::enter_pocket( player& p, item& it ) const
{
    if( !it.pocket_dim.has_value() || !it.pocket_dim->pocket_info.has_value() ) { return; }
auto& dim_info = *it.pocket_dim;
auto& pd = *dim_info.pocket_info;

// Store return information
pd.return_dimension_id = g->get_current_dimension_id();
if( const auto * info = g->get_current_dimension_info() ) {
    pd.return_world_type = info->world_type;
} else {
    pd.return_world_type = world_type_id{};
}
pd.return_point = p.abs_pos();

// Player is now inside; clear the exit timestamp.
pd.last_player_exit = std::nullopt;

p.add_msg_if_player( m_good, _( "You enter the pocket dimension." ) );

// Compute the map top-left corner so the entry point ends up near the grid center.
// load_map() treats pos_sm as the top-left corner; the grid center is at
// pos_sm + (g_half_mapsize, g_half_mapsize).  Placing the entry submap there
// avoids a large multi-submap shift in update_map() which can trigger
// use-after-free via stale grid[] pointers during submap_loader eviction.
const auto entry_sm = project_to<coords::sm>( pd.entry_point );
const auto dest_sm = entry_sm - tripoint_rel_sm( g_half_mapsize, g_half_mapsize, 0 );
const auto new_pd = !pd.terrain_generated && !entry_mapgen.empty();

// Build a pre-load callback to place the overmap special BEFORE submaps are generated.
// This ensures submap generation uses the correct overmap terrain types (e.g. "Cave")
// instead of the default oter_id(0) which generates field/grass.
std::function<void()> pre_load;
if( new_pd ) {
    pre_load = [&]() {
            overmap_special_id special_id( entry_mapgen );
            if( special_id.is_valid() ) {
                auto& pd_omb = get_overmapbuffer( dim_info.dimension_id );
                const auto proj = project_remain<coords::om>( pd.entry_point );
                auto& om = pd_omb.get( proj.quotient );
                om.place_special_forced(
                    special_id, project_to<coords::omt>( proj.remainder_tripoint ),
                    om_direction::type::north );
                pd.terrain_generated = true;
            }
        };
    }

    g->travel_to_dimension( dim_info.dimension_id, dim_info.world_type, pd, dest_sm, pre_load );

    // Only make the first entrance safe. If the player makes it dangerous later, that's on them.
    // No sneaky teleporting shenaneigans.
    if( new_pd ) {
    const auto safe = find_safe_spawn( get_map().abs_to_bub( pd.entry_point ) );
        pd.entry_point = get_map().bub_to_abs( safe );
    }

    // The map is already loaded centered on the destination (via load_pos parameter),
    // so local coordinates are valid without needing a map shift first.
    p.setpos( abs_to_bub( pd.entry_point ) );

    // Single update_map call at the final position
    g->update_map( p );
}

// ---- iuse_portal_link -------------------------------------------------------

std::unique_ptr<iuse_actor> iuse_portal_link::clone() const
{
    return std::make_unique<iuse_portal_link>( *this );
}

void iuse_portal_link::load( const JsonObject& obj )
{
    obj.read( "required_portal_flag", required_portal_flag );
    obj.read( "can_return", can_return );
    obj.read( "charges_per_use", charges_per_use );
}

auto iuse_portal_link::can_use( const Character &, const item& it, bool,
                                const tripoint_bub_ms & ) const
-> ret_val<bool>
{
    if( charges_per_use > 0 && it.ammo_remaining() < charges_per_use ) {
    return ret_val<bool>::make_failure( _( "The %s doesn't have enough charges." ), it.tname() );
    }
    return ret_val<bool>::make_success();
}

auto iuse_portal_link::use( player& p, item& it, bool, const tripoint_bub_ms & ) const -> int
{
    const auto player_abs = p.abs_pos();
    const auto& cur_dim = g->get_current_dimension_id();

    // --- Mode 1: Link to a nearby portal with a matching flag ---
    if( !required_portal_flag.empty() ) {
        portal_tile* nearby_portal = nullptr;
        for( const tripoint_bub_ms& adj : get_map().points_in_radius( p.bub_pos(), 1 ) ) {
            auto abs = tripoint_abs_ms( get_map().bub_to_abs( adj ) );
            auto* candidate = active_tiles::furn_at<portal_tile>( abs );
            if( candidate && candidate->linkable_item_flag == required_portal_flag
                && candidate->linked ) {
                nearby_portal = candidate;
                break;
            }
        }
        if( nearby_portal != nullptr && !it.get_var( "portal_linked", false ) ) {
            if( query_yn( _( "Link %s to this portal?" ), it.tname() ) ) {
                it.set_var( "portal_linked", true );
                it.set_var( "linked_dim_id", nearby_portal->target_dim_id );
                it.set_var( "linked_pos_x", nearby_portal->target_pos.x() );
                it.set_var( "linked_pos_y", nearby_portal->target_pos.y() );
                it.set_var( "linked_pos_z", nearby_portal->target_pos.z() );
                add_msg( m_good, _( "The %s locks onto the portal." ), it.tname() );
            }
            return 0;
        }
    }

    // --- Mode 2: Teleport to linked portal ---
    if( !it.get_var( "portal_linked", false ) ) {
        p.add_msg_if_player( m_info, _( "The %s isn't linked to any portal." ), it.tname() );
        return 0;
    }

    const auto linked_dim = it.get_var( "linked_dim_id" );
    const tripoint_abs_ms linked_pos(
        it.get_var( "linked_pos_x", 0 ), it.get_var( "linked_pos_y", 0 ),
        it.get_var( "linked_pos_z", 0 ) );

    // Return mode: if at the linked portal and origin is stored, offer return.
    if( can_return && it.get_var( "origin_stored", false ) && cur_dim == linked_dim
        && rl_dist( player_abs, linked_pos ) <= 5 ) {
        if( query_yn( _( "Return to your origin point?" ) ) ) {
            const auto origin_dim = it.get_var( "origin_dim_id" );
            const tripoint_abs_ms origin_pos(
                it.get_var( "origin_pos_x", 0 ), it.get_var( "origin_pos_y", 0 ),
                it.get_var( "origin_pos_z", 0 ) );
            auto wt_id = world_type_id( origin_dim );
            const auto preload_point =
                project_to<coords::sm>( origin_pos ) - point_rel_sm( g_half_mapsize, g_half_mapsize );
            g->travel_to_dimension( origin_dim, wt_id, std::nullopt, preload_point );
            p.setpos( get_map().abs_to_bub( origin_pos ) );
            g->update_map( p );
            it.erase_var( "origin_stored" );
            return charges_per_use;
        }
        return 0;
    }

    // Store origin before teleporting if can_return.
    if( can_return && !it.get_var( "origin_stored", false ) ) {
        it.set_var( "origin_dim_id", cur_dim );
        it.set_var( "origin_pos_x", player_abs.x() );
        it.set_var( "origin_pos_y", player_abs.y() );
        it.set_var( "origin_pos_z", player_abs.z() );
        it.set_var( "origin_stored", true );
    }

    p.add_msg_if_player( m_good, _( "The %s tears a path through dimensional space." ), it.tname() );

    auto wt_id = world_type_id( linked_dim );
    if( linked_dim.empty() ) { wt_id = world_types::get_default(); }
    const auto dest_sm =
        project_to<coords::sm>( linked_pos ) - tripoint_rel_sm( g_half_mapsize, g_half_mapsize, 0 );
    g->travel_to_dimension( linked_dim, wt_id, std::nullopt, dest_sm );
    p.setpos( get_map().abs_to_bub( linked_pos ) );
    g->update_map( p );
    return charges_per_use;
}

void iuse_pocket_dimension::exit_pocket( player& p, item& it ) const
{
    if( !it.pocket_dim.has_value() || !it.pocket_dim->pocket_info.has_value() ) { return; }
auto& pd = *it.pocket_dim->pocket_info;

p.add_msg_if_player( m_good, _( "You exit the pocket dimension." ) );

const auto return_dimension_id = pd.return_dimension_id;
const auto return_world_type = pd.return_world_type;
const auto return_point = pd.return_point;
const auto return_preload_point = pd.get_preload_point();

// Reset to fresh state: clears the entry-dimension lock so the key can be used
// from whatever dimension the player is now in after returning.
pd.return_dimension_id.clear();
pd.return_world_type = world_type_id{};

// Record when the player exited so the lifetime countdown can start.
if( pd.lifetime.has_value() ) { pd.last_player_exit = calendar::turn; }

// Travel back to the return dimension (no bounds = infinite dimension).
// travel_to_dimension clears stale bounds before loading the map.
g->travel_to_dimension(
    return_dimension_id, return_world_type, std::nullopt, return_preload_point );

p.setpos( find_safe_spawn( get_map().abs_to_bub( return_point ) ) );

// Single update_map call at the final position
g->update_map( p );
}

// ---- iuse_paint_stuff -------------------------------------------------------

namespace
{
template <typename T, typename U>
concept is_painter = requires(
                         const T& painter, const U& thing, const tripoint_bub_ms& where, const RGBColorPair& color,
                         const iuse_paint_stuff_config::paint_layer layer )
{
    { painter.enumerate( where ) };
    { painter.get_cost( thing ) }
    -> std::same_as<float>;
    { painter.can_paint( thing ) }
    -> std::same_as<bool>;
    { painter.get_color( thing ) }
    -> std::same_as<RGBColorPair>;
    { painter.set_color( thing, color, layer ) }
    -> std::same_as<bool>;
    { painter.describe( thing ) }
    -> std::same_as<std::string>;
};

template <typename Painter, typename Thing = Painter::value_type>
requires is_painter<Painter, Thing>
auto iuse_paint_stuff_do_paint(
    player& who, item& it, const float charge_cost,
    const std::pair<tripoint_bub_ms, tripoint_bub_ms> &area, const Painter& painter )
{
    const auto target_color = iuse_paint_stuff::get_paint_color( it );
    const auto layer = iuse_paint_stuff_config::get_paint_layer( it );

    float charges_used = 0.0f;
    const float mod_cost = [&]() {
        switch( layer ) {
            default:
                return charge_cost;
            case iuse_paint_stuff_config::fg:
            case iuse_paint_stuff_config::bg:
                return charge_cost / 2;
        }
    }
    ();

    const auto col_selector = [&]( const RGBColorPair oldColor ) -> std::optional<RGBColorPair> {
        const auto [p_fg, p_bg] = oldColor;
        switch( layer )
        {
            default:
            case iuse_paint_stuff_config::both:
                if( p_fg != target_color || p_bg != target_color ) {
                    return RGBColorPair{.bg = target_color, .fg = target_color};
                }
                break;
            case iuse_paint_stuff_config::fg:
                if( p_fg != target_color ) { return RGBColorPair{.bg = p_bg, .fg = target_color}; }
                break;
            case iuse_paint_stuff_config::bg:
                if( p_bg != target_color ) { return RGBColorPair{.bg = target_color, .fg = p_fg}; }
                break;
        }
        return std::nullopt;
    };

    for( const auto& pos : tripoint_range( area.first, area.second ) ) {
        const auto things_at = painter.enumerate( pos );
        bool ammo_exhausted = false;

        for( const auto& thing : things_at ) {
            if( !painter.can_paint( thing ) ) { continue; }

            const float cost_at = painter.get_cost( thing );
            const float iter_cost = cost_at * mod_cost;

            if( ( charges_used + iter_cost ) > it.ammo_remaining() ) {
                const auto need = static_cast<int>( std::ceil( charges_used + iter_cost ) );
                const auto rem = static_cast<int>( it.ammo_remaining() - std::ceil( charges_used ) );
                who.add_msg_if_player(
                    m_info,
                    vgettext( "Your %s has %d charge but needs %d.",
                              "Your %s has %d charges but needs %d.", rem ),
                    it.tname(), rem, need );
                ammo_exhausted = true;
                break;
            }

            const auto prev_col = painter.get_color( thing );
            const auto n_col = col_selector( prev_col );

            if( !n_col.has_value() ) { continue; }

            if( painter.set_color( thing, n_col.value(), layer ) ) {
                who.add_msg_if_player(
                    m_info, _( "You paint the %s %s." ), painter.describe( thing ),
                    target_color.friendly_name() );
                charges_used += iter_cost;
                who.moves -= to_turns<int>( 30_seconds );
            }
        }

        if( ammo_exhausted ) { break; }
    }

    const auto final_cost = static_cast<int>( std::ceil( charges_used ) );
    return std::max( 0, final_cost );
}

RGBColorPair color_from_vars( const data_vars::data_set& vars )
{
    const auto p_c = vars.get<RGBColor>( TINT_COLOR_VAR_NAME, {} );
    const auto p_fg = vars.get<RGBColor>( TINT_COLOR_FG_VAR_NAME, p_c );
    const auto p_bg = vars.get<RGBColor>( TINT_COLOR_BG_VAR_NAME, p_c );
    return RGBColorPair{.bg = p_bg, .fg = p_fg};
}

void color_to_vars(
    data_vars::data_set& vars, const RGBColorPair& col,
    const iuse_paint_stuff_config::paint_layer layer )
{
    switch( layer ) {
        default:
        case iuse_paint_stuff_config::both:
            vars.set<RGBColor>( TINT_COLOR_VAR_NAME, col.fg );
            vars.erase( TINT_COLOR_FG_VAR_NAME );
            vars.erase( TINT_COLOR_BG_VAR_NAME );
            break;
        case iuse_paint_stuff_config::fg:
            vars.set<RGBColor>( TINT_COLOR_FG_VAR_NAME, col.fg );
            break;
        case iuse_paint_stuff_config::bg:
            vars.set<RGBColor>( TINT_COLOR_FG_VAR_NAME, col.bg );
            break;
    }
}

struct item_painter {
    using paint_layer = iuse_paint_stuff_config::paint_layer;
    using value_type = item*;

    itype_id target_type;

    auto enumerate( const tripoint_bub_ms& pos ) const {
        std::vector<item *> items;
        auto stack = get_map().i_at( pos );
        for( const auto& i : stack ) {
            if( target_type.is_null() || i->typeId() == target_type ) { items.push_back( i ); }
        }
        return items;
    }

    static std::string describe( const value_type& p ) { return p->type_name(); }

    static auto get_cost( const value_type it ) -> float { return it->count(); }

    static auto can_paint( const item* const it ) -> bool {
        if( it->type->phase != SOLID ) { return false; }
        if( it->type->has_flag( flag_NO_PAINT ) ) { return false; }
        if( it->is_corpse() ) { return false; }
        if( it->is_food() ) { return false; }
        return true;
    }

    static auto get_color( const value_type it ) -> RGBColorPair {
        return color_from_vars( it->item_vars() );
    }

    static auto set_color( const value_type it, const RGBColorPair& col, const paint_layer layer )
    -> bool {
        color_to_vars( it->item_vars(), col, layer );
        return true;
    }
};

template <bool Roof> struct veh_part_painter {
    using paint_layer = iuse_paint_stuff_config::paint_layer;
    using value_type = std::optional<vpart_reference>;

    const vehicle &target_veh;

    static std::string describe( const value_type& vp ) {
        return string_format( _( "%s's %s" ), vp->vehicle().name, vp->part().name( false ) );
    }

    auto enumerate( const tripoint_bub_ms& p ) const -> std::array<value_type, 1> {
        const auto vp = get_map().veh_at( p );
        if constexpr( Roof ) {
            const auto roof_part = [&]() -> std::optional<vpart_reference> {
                auto &veh = vp->vehicle();
                const bool has_obstacle_here =
                vp.part_with_feature( VPFLAG_OBSTACLE, false ).has_value();
                if( has_obstacle_here ) { return std::nullopt; }
                const auto part_idx = veh.roof_at_part( vp->part_index() );
                if( part_idx != -1 ) { return vpart_reference( veh, part_idx ); }
                return std::nullopt;
            }();
            return {roof_part};
        } else {
            const auto disp_part = vp.part_displayed();
            return {disp_part};
        }
    }

    static constexpr float get_cost( const value_type & ) { return 1; }

    bool can_paint( const value_type& vp ) const {
        if( !vp.has_value() ) { return false; }
    if( &vp->vehicle() != &target_veh ) { return false; }
    if( !item_painter::can_paint( &vp->part().get_base() ) ) { return false; }
        return true;
    }

    static RGBColorPair get_color( const value_type& vp ) {
        const auto& disp_part = vp->part();
        return disp_part.get_color();
    }

    static bool set_color( const value_type& vp, const RGBColorPair& col, const paint_layer ) {
        auto& disp_part = vp->part();
        disp_part.set_color( col );
        return true;
    }
};

template <bool Furn> struct ter_furn_painter {
    using value_type = tripoint_bub_ms;
    using paint_layer = iuse_paint_stuff_config::paint_layer;

    static data_vars::data_set *get_vars( const tripoint_bub_ms& p ) {
        if constexpr( Furn ) {
            return get_map().furn_vars( p );
        } else {
            return get_map().ter_vars( p );
        }
    }

    static std::string describe( const value_type& p ) {
        if constexpr( Furn ) {
            return get_map().furn( p )->name();
        } else {
            return get_map().ter( p )->name();
        }
    }

    static auto enumerate( const tripoint_bub_ms& p ) -> std::array<tripoint_bub_ms, 1> {
        return {p};
    }

    static float get_cost( const tripoint_bub_ms& p ) {
        if( get_map().has_flag_ter_or_furn( "TINY", p ) ) { return 0.25f; }
        if( get_map().has_flag_ter_or_furn( "SHORT", p ) ) { return 0.5f; }
        return 1;
    }

    static bool can_paint( const tripoint_bub_ms& p ) {
        const auto _vars = get_vars( p );
        if( _vars == nullptr ) { return false; }

        if constexpr( Furn ) {
            if( !get_map().has_furn( p ) ) { return false; }
            if( get_map().has_flag_furn( flag_NO_PAINT.str(), p ) ) { return false; }
        } else {
            // No Air
            if( get_map().has_flag_ter( TFLAG_NO_FLOOR, p ) ) { return false; }
            // No Liquids
            if( get_map().has_flag_ter( TFLAG_LIQUID, p )
                || get_map().has_flag_ter( TFLAG_SWIMMABLE, p ) ) {
                return false;
            }

            if( get_map().has_flag_ter( flag_NO_PAINT.str(), p ) ) { return false; }
        }
        return true;
    }

    static RGBColorPair get_color( const tripoint_bub_ms& p ) {
        return color_from_vars( *get_vars( p ) );
    }

    static bool set_color(
        const tripoint_bub_ms& p, const RGBColorPair& col, const paint_layer layer ) {
        color_to_vars( *get_vars( p ), col, layer );
        return true;
    }
};

template <bool Roof>
auto iuse_paint_stuff_vehicle(
    player& who, item& it, bool, const tripoint_bub_ms &, const float charge_cost ) -> int
{
    const auto& here = get_map();

    std::set<vehicle *> tmp{};
    const auto query_filter = [&]( const tripoint_bub_ms & p ) {
        const auto veh = here.veh_at( p );
        if( !veh.has_value() ) { return false; }
        const auto [_, ok] = tmp.emplace( &veh->vehicle() );
        return ok;
    };
    const auto query_name = [&]( const tripoint_bub_ms & p ) {
        return here.veh_at( p )->vehicle().name;
    };
    const auto veh_pos_opt = choose_adjacent_uilist(
                                 _( "Paint which vehicle?" ), _( "There is nothing to paint nearby." ), query_filter,
                                 query_name );

    if( !veh_pos_opt.has_value() ) {
        add_msg( _( "Never mind." ) );
        return 0;
    }

    const auto veh_pos = veh_pos_opt.value();

    const auto& target_veh = here.veh_at( veh_pos )->vehicle();

    const auto area = choose_area( _( "Paint Vehicle" ), veh_pos );
    if( !area.has_value() ) {
        add_msg( _( "Never mind." ) );
        return 0;
    }

    const auto painter = veh_part_painter<Roof> {target_veh};
    return iuse_paint_stuff_do_paint( who, it, charge_cost, area.value(), painter );
}

template <bool Furn>
auto iuse_paint_stuff_ter_furn(
    player& who, item& it, bool, const tripoint_bub_ms& pos, const float charge_cost ) -> int
{
    using painter_type = ter_furn_painter<Furn>;

    const auto area = choose_area( _( Furn ? "Paint Furniture" : "Paint Terrain" ), pos );
    if( !area.has_value() ) {
        add_msg( _( "Never mind." ) );
        return 0;
    }

    constexpr painter_type painter{};
    return iuse_paint_stuff_do_paint( who, it, charge_cost, area.value(), painter );
}

auto iuse_paint_stuff_item(
    player& who, item& it, bool, const tripoint_bub_ms &, const float charge_cost ) -> int
{
    using painter_type = item_painter;
    auto& here = get_map();

    const auto query_filter = [&]( const tripoint_bub_ms & p ) {
        return here.has_item_with( p, []( const item & x ) -> bool {
            return painter_type::can_paint( &x );
        } );
    };
    const auto item_pos = choose_adjacent_highlight(
                              _( "Paint which Items?" ), _( "There is nothing to paint nearby." ), query_filter );
    if( !item_pos.has_value() ) {
        add_msg( _( "Never mind." ) );
        return 0;
    }

    std::set<itype_id> types{};
    std::vector<itype_id> typesList{};
    for( const auto& i : here.i_at( item_pos.value() ) ) {
        if( !painter_type::can_paint( i ) ) { continue; }

        const auto [iter, ok] = types.emplace( i->typeId() );
        if( ok ) { typesList.push_back( i->typeId() ); }
    }

    itype_id target_type;
    if( typesList.empty() ) {
        add_msg( _( "Never mind." ) );
        return 0;
    }

    if( typesList.size() == 1 ) {
        target_type = typesList.at( 0 );
    } else {
        uilist lst;
        lst.title = _( "Paint Items" );
        typesList.insert( typesList.begin(), itype_id::NULL_ID() );
        for( const auto& i : typesList ) {
            lst.addentry( i.is_null() ? _( "Everything" ) : i->nname( 1 ) );
        }
        lst.query();

        if( lst.ret < 0 ) {
            add_msg( _( "Never mind." ) );
            return 0;
        }

        target_type = typesList.at( lst.ret );
    }

    const painter_type painter{target_type};
    const auto area = std::make_pair( item_pos.value(), item_pos.value() );
    return iuse_paint_stuff_do_paint( who, it, charge_cost, area, painter );
}

auto iuse_paint_stuff_graffiti(
    player& who, item &, bool, const tripoint_bub_ms &, const float charge_cost ) -> int
{
    auto& m = get_map();
    const std::optional<tripoint_bub_ms> pos_ = choose_adjacent( _( "Spray where?" ) );
    if( !pos_ ) {
        add_msg( _( "Never mind." ) );
        return 0;
    }

    const auto pos = pos_.value();
    string_input_popup popup;
    const std::string message =
        popup
        .description( string_format(
                          "%s %s", _( "Spray What?" ), _( "(To delete, clear the text and confirm)" ) ) )
        .text( m.has_graffiti_at( pos ) ? m.graffiti_at( pos ) : std::string() )
        .identifier( "graffiti" )
        .query_string();
    if( popup.canceled() ) {
        add_msg( _( "Never mind." ) );
        return 0;
    }

    const bool grave = m.ter( pos ) == t_grave_new;
    int move_cost;
    if( message.empty() ) {
        if( m.has_graffiti_at( pos ) ) {
            move_cost = 3 * m.graffiti_at( pos ).length();
            m.delete_graffiti( pos );
            if( grave ) {
                who.add_msg_if_player( m_info, _( "You blur the inscription on the grave." ) );
            } else {
                who.add_msg_if_player( m_info, _( "You manage to get rid of the message on the "
                                                  "surface." ) );
            }
        } else {
            add_msg( _( "Never mind." ) );
            return 0;
        }
    } else {
        m.set_graffiti( pos, message );
        if( grave ) {
            who.add_msg_if_player( m_info, _( "You carve an inscription on the grave." ) );
        } else {
            who.add_msg_if_player( m_info, _( "You write a message on the surface." ) );
        }
        move_cost = 2 * message.length();
    }
    who.moves -= move_cost;
    return std::ceil( charge_cost );
}

} // namespace


template <> struct enum_traits<iuse_paint_stuff_config::paint_layer> {
    static constexpr iuse_paint_stuff_config::paint_layer last =
        iuse_paint_stuff_config::paint_layer::num_layers;
};

namespace io
{
template <>
std::string enum_to_string<iuse_paint_stuff_config::paint_layer>(
    iuse_paint_stuff_config::paint_layer data )
{
    switch( data ) {
        case iuse_paint_stuff_config::paint_layer::both:
            return "both";
        case iuse_paint_stuff_config::paint_layer::fg:
            return "fg";
        case iuse_paint_stuff_config::paint_layer::bg:
            return "bg";
        case iuse_paint_stuff_config::paint_layer::num_layers:
            break;
        default:
            break;
    }
    debugmsg( "Invalid layer" );
    abort();
}
} // namespace io


void iuse_paint_stuff::load( const JsonObject& jo )
{
    if( jo.has_member( "charge_cost" ) ) { charge_cost = jo.get_float( "charge_cost" ); }
}

void iuse_paint_stuff_config::load( const JsonObject& jo )
{
    if( jo.has_member( "color_swap" ) ) { color_swap = jo.get_bool( "color_swap" ); }
}

auto iuse_paint_stuff_config::use( player &, item& it, bool, const tripoint_bub_ms & ) const -> int
{

    enum eMode { Abort = 0, Layer = 1, ColorSwap = 2 };

    std::vector<std::pair<std::string, eMode>> choices{};
    choices.push_back( {_( "Change Layer" ), Layer} );

    if( color_swap ) { choices.push_back( {_( "Change Color" ), ColorSwap} ); }

    eMode mode = Abort;
    if( choices.size() == 1 ) {
        mode = choices.back().second;
    } else if( choices.size() > 1 ) {
        uilist lst;
        lst.title = _( "Configure Painter" );
        for( const auto& [opt, res] : choices ) { lst.addentry( res, true, MENU_AUTOASSIGN, opt ); }
        lst.query();

        if( lst.ret >= 0 ) { mode = static_cast<eMode>( lst.ret ); }
    }

    switch( mode ) {
        case Abort:
        default:
            add_msg( _( "Never mind." ) );
            return 0;
        case Layer:
            get_paint_layer( it, true );
            return 0;
        case ColorSwap:
            set_color( it );
            return 0;
    }
} // namespace

auto iuse_paint_stuff::use( player& who, item& it, const bool b, const tripoint_bub_ms& pos ) const
-> int
{
    auto& here = get_map();

    enum eMode { Abort = 0, Vehicle, VehicleRoof, Furniture, Item, Terrain, Graffiti };

    std::vector<std::pair<std::string, eMode>> choices{};

    const bool has_item_near = here.has_nearby( pos, []( const map & m, const tripoint_bub_ms & p ) {
        return m.has_items( p );
    } );
    if( has_item_near ) { choices.push_back( {_( "Item" ), Item} ); }

    const bool has_veh_near = here.has_nearby( pos, []( const map & m, const tripoint_bub_ms & p ) {
        return m.veh_at( p ).has_value();
    } );
    if( has_veh_near ) {
        choices.push_back( {_( "Vehicle" ), Vehicle} );
        choices.push_back( {_( "Vehicle Roof" ), VehicleRoof} );
    }

    const bool has_furn_near = here.has_nearby( pos, []( const map & m, const tripoint_bub_ms & p ) {
        return m.has_furn( p ) && ter_furn_painter<true>::can_paint( p );
    } );
    if( has_furn_near ) { choices.push_back( {_( "Furniture" ), Furniture} ); }

    const bool has_terrain_near = here.has_nearby( pos, []( const map &, const tripoint_bub_ms & p ) {
        return ter_furn_painter<false>::can_paint( p );
    } );
    if( has_terrain_near ) {
        choices.push_back( {_( "Terrain" ), Terrain} );
        choices.push_back( {_( "Graffiti" ), Graffiti} );
    }

    eMode mode = Abort;
    if( choices.size() == 1 ) {
        mode = choices.back().second;
    } else if( choices.size() > 1 ) {
        uilist lst;
        lst.title = _( "Paint What?" );
        for( const auto& [opt, res] : choices ) { lst.addentry( res, true, MENU_AUTOASSIGN, opt ); }
        lst.query();

        if( lst.ret >= 0 ) { mode = static_cast<eMode>( lst.ret ); }
    }

    switch( mode ) {
        case Abort:
        default:
            add_msg( _( "Never mind." ) );
            return 0;
        case Terrain:
            return iuse_paint_stuff_ter_furn<false>( who, it, b, pos, charge_cost );
        case Furniture:
            return iuse_paint_stuff_ter_furn<true>( who, it, b, pos, charge_cost );
        case Vehicle:
            return iuse_paint_stuff_vehicle<false>( who, it, b, pos, charge_cost );
        case VehicleRoof:
            return iuse_paint_stuff_vehicle<true>( who, it, b, pos, charge_cost );
        case Graffiti:
            return iuse_paint_stuff_graffiti( who, it, b, pos, charge_cost );
        case Item:
            return iuse_paint_stuff_item( who, it, b, pos, charge_cost );
    };
}

void iuse_paint_stuff::info( const item& it, std::vector<iteminfo> &inf ) const
{
    const auto col = try_get_paint_color( it );
    if( !col.has_value() ) {
        inf.emplace_back( "TOOL", string_format( _( "<bold>Paint Color</bold>: %s" ), "Unknown" ) );
    } else {
        const auto rgb = col.value();
        if( rgb == RGBColor{} ) {
            inf.emplace_back( "TOOL", _( "<bold>Paint Solvent</bold>" ) );
        } else {
            auto name = rgb.friendly_name();
            inf.emplace_back( "TOOL", string_format( _( "<bold>Paint Color</bold>: %s" ), name ) );
        }
    }
}

void iuse_paint_stuff::on_placed( item& it, const map &, const tripoint_bub_ms & ) const
{
    get_paint_color( it );
}

void iuse_paint_stuff_config::on_placed( item& it, const map &, const tripoint_bub_ms & ) const
{
    get_paint_layer( it, false );
}

std::optional<RGBColor> iuse_paint_stuff::try_get_paint_color( const item& it )
{
    if( !it.has_var( PAINT_VAR ) ) { return std::nullopt; }
    return it.get_var<RGBColor>( PAINT_VAR, {} );
}

RGBColor iuse_paint_stuff::get_paint_color( item& it )
{
    if( !it.has_var( PAINT_VAR ) ) {
        const auto rng_col = RGBColor::random_named().first;
        it.set_var<RGBColor>( PAINT_VAR, rng_col );
        it.set_var<RGBColor>( TINT_COLOR_VAR_NAME, rng_col );
    }
    return it.get_var<RGBColor>( PAINT_VAR, {} );
}

iuse_paint_stuff_config::paint_layer iuse_paint_stuff_config::get_paint_layer(
    item& it, bool change )
{
    if( !it.has_var( LAYER_VAR ) ) { it.set_var<paint_layer>( LAYER_VAR, both ); }

    const auto prev = it.get_var<paint_layer>( LAYER_VAR, both );
    if( change ) {
        uilist lst;
        lst.title = _( "Paint Which Layer" );
        lst.addentry( 0, true, MENU_AUTOASSIGN,
                      string_format( "%s%s", _( "Both" ), prev == both ? "*" : "" ) );
        lst.addentry( 1, true, MENU_AUTOASSIGN,
                      string_format( "%s%s", _( "Foreground" ), prev == fg ? "*" : "" ) );
        lst.addentry( 2, true, MENU_AUTOASSIGN,
                      string_format( "%s%s", _( "Background" ), prev == bg ? "*" : "" ) );
        lst.query();

        switch( lst.ret ) {
            case 0:
                it.set_var<paint_layer>( LAYER_VAR, both );
                return both;
            case 1:
                it.set_var<paint_layer>( LAYER_VAR, fg );
                return fg;
            case 2:
                it.set_var<paint_layer>( LAYER_VAR, bg );
                return bg;
            default:
                break;
        }
    }
    return prev;
}

void iuse_paint_stuff_config::set_color( item& it )
{
    uilist lst;
    lst.title = _( "Choose Color" );
    lst.w_height_setup = TERMY / 2;
    for( const auto& [col, name] : RGBColor::get_all_named_colors() ) { lst.addentry( name ); }
    lst.query();

    if( lst.ret >= 0 ) {
        it.set_var <
        RGBColor > ( iuse_paint_stuff::PAINT_VAR, *RGBColor::try_parse( lst.entries[lst.ret].txt ) );
    }
}

ret_val<bool> iuse_paint_stuff::can_use(
    const Character &, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( it.ammo_remaining() < 1 ) {
    return ret_val<bool>::make_failure( _( "The %s doesn't have enough charges." ), it.tname() );
    }

    return ret_val<bool>::make_success();
}

ret_val<bool> iuse_paint_stuff_config::can_use(
    const Character &, const item &, bool, const tripoint_bub_ms & ) const
{
    return ret_val<bool>::make_success();
}

auto iuse_paint_stuff::clone() const -> std::unique_ptr<iuse_actor>
{
    return std::make_unique<iuse_paint_stuff>( *this );
}

auto iuse_paint_stuff_config::clone() const -> std::unique_ptr<iuse_actor>
{
    return std::make_unique<iuse_paint_stuff_config>( *this );
}
