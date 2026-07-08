// Item processing methods: needs_processing, process_* handlers, process_internal
// — split out of item.cpp. .cpp-only, no API changes.

#include "item.h"
#include "item_cable.h"

#include <algorithm>
#include <numeric>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>

#include "active_tile_data_def.h"
#include "ammo.h"
#include "ascii_art.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_item_options.h"
#include "catalua_icallback_actor.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "character_stat.h"
#include "clothing_mod.h"
#include "clzones.h"
#include "color.h"
#include "craft_command.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "drop_token.h"
#include "effect.h" // for weed_msg
#include "enums.h"
#include "explosion.h"
#include "faction.h"
#include "fault.h"
#include "field_type.h"
#include "fire.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item_category.h"
#include "item_factory.h"
#include "item_group.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "map.h"
#include "martialarts.h"
#include "material.h"
#include "melee.h"
#include "messages.h"
#include "mod_manager.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "point.h"
#include "projectile.h"
#include "profile.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "rot.h"
#include "scores_ui.h"
#include "cloning_utils.h"
#include "skill.h"
#include "stomach.h"
#include "string_formatter.h"
#include "string_id_utils.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "units_energy.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "wheel_dimensions.h"

// File-scope id constants (moved with process methods; internal linkage).
static const species_id ROBOT( "ROBOT" );
static const efftype_id effect_cig( "cig" );
static const efftype_id effect_shakes( "shakes" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_weed_high( "weed_high" );
static const fault_id fault_gun_blackpowder( "fault_gun_blackpowder" );
static const std::string flag_DEEP_WATER( "DEEP_WATER" );
static const std::string flag_FLAMMABLE( "FLAMMABLE" );
static const std::string flag_FLAMMABLE_ASH( "FLAMMABLE_ASH" );
static const std::string flag_LIQUID( "LIQUID" );
static const flag_id flag_MARIJUANA( "MARIJUANA" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_bio_armor( "bio_armor" );
static const trait_id trait_LIGHTWEIGHT( "LIGHTWEIGHT" );
static const trait_id trait_TOLERANCE( "TOLERANCE" );

bool item::needs_processing() const
{
    return is_active() || has_flag( flag_RADIO_ACTIVATION ) || has_flag( flag_ETHEREAL_ITEM ) ||
    ( !contents.empty() && is_container() && contents.front().needs_processing() ) ||
    ( magazine_current() && magazine_current()->needs_processing() ) ||
    is_artifact() || is_relic() || goes_bad();
}

int item::processing_speed() const
{
    if( is_corpse() || is_food() || is_food_container() ) {
    return to_turns<int>( 10_minutes );
    }
    // Unless otherwise indicated, update every turn.
    return 1;
}


void item::process_artifact( player *carrier, const tripoint_bub_ms & /*pos*/ )
{
    if( !is_artifact() ) {
        return;
    }
    // Artifacts are currently only useful for the player character, the messages
    // don't consider npcs. Also they are not processed when laying on the ground.
    // TODO: change game::process_artifact to work with npcs,
    // TODO: consider moving game::process_artifact here.
    if( carrier == &get_avatar() ) {
        g->process_artifact( *this, *carrier );
    }
}

std::vector<trait_id> item::mutations_from_wearing( const Character &guy ) const
{
    if( !is_relic() ) {
    return std::vector<trait_id> {};
}
std::vector<trait_id> muts;

for( const enchantment &ench : relic_data->get_enchantments() ) {
    for( const trait_id &mut : ench.get_mutations() ) {
            // this may not be perfectly accurate due to conditions
            muts.push_back( mut );
        }
    }

for( const trait_id &char_mut : guy.get_mutations() ) {
    for( auto iter = muts.begin(); iter != muts.end(); ) {
            if( char_mut == *iter ) {
                iter = muts.erase( iter );
            } else {
                ++iter;
            }
        }
    }

    return muts;
}

void item::process_relic( Character *carrier )
{
    if( !is_relic() ) {
        return;
    }
    std::vector<enchantment> active_enchantments;

    if( carrier ) {
        for( const enchantment &ench : get_enchantments() ) {
            if( ench.is_active( *carrier, *this ) ) {
                active_enchantments.emplace_back( ench );
            }
        }
    }

    relic_funcs::process_recharge( *this, carrier );
}

detached_ptr<item> item::process_corpse( detached_ptr<item> &&self, player *carrier,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    // some corpses rez over time
    if( self->corpse == nullptr || self->damage() >= self->max_damage() ) {
        return std::move( self );
    }
    if( self->corpse->zombify_into && self->rotten() && !self->has_flag( flag_PULPED ) ) {
        self->rot -= self->get_shelf_life();
        self->corpse = &*self->corpse->zombify_into;
        return std::move( self );
    }
    if( !self->ready_to_revive( pos ) ) {
        return std::move( self );
    }
    if( rng( 0, self->volume() / units::legacy_volume_factor ) > self->burnt &&
        g->revive_corpse( pos, *self ) ) {
        if( carrier == nullptr ) {
            if( get_avatar().sees( pos ) ) {
                if( self->corpse->in_species( ROBOT ) ) {
                    add_msg( m_warning, _( "A nearby robot has repaired itself and stands up!" ) );
                } else {
                    add_msg( m_warning, _( "A nearby corpse rises and moves towards you!" ) );
                }
            }
        } else {
            if( self->corpse->in_species( ROBOT ) ) {
                carrier->add_msg_if_player( m_warning,
                                            _( "Oh dear god, a robot you're carrying has started moving!" ) );
            } else {
                carrier->add_msg_if_player( m_warning,
                                            _( "Oh dear god, a corpse you're carrying has started moving!" ) );
            }
        }
        // Destroy this corpse item
        return detached_ptr<item>();
    }

    return std::move( self );
}

detached_ptr<item> item::process_fake_mill( detached_ptr<item> &&self, player * /*carrier*/,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    map &here = get_map();
    if( here.furn( pos ) != furn_str_id( "f_wind_mill_active" ) &&
        here.furn( pos ) != furn_str_id( "f_water_mill_active" ) ) {
        self->set_counter( 0 );
        return detached_ptr<item>(); //destroy fake mill
    }
    if( self->age() >= 6_hours || self->item_counter == 0 ) {
        iexamine::mill_finalize( get_avatar(), pos,
                                 self->birthday() ); //activate effects when timers goes to zero
        return detached_ptr<item>(); //destroy fake mill item
    }

    return std::move( self );
}

detached_ptr<item> item::process_fake_cloning_vat( detached_ptr<item> &&self, player * /*carrier*/,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    map &here = get_map();
    if( here.furn( pos ) != furn_str_id( "f_cloning_vat_active" ) ) {
        self->item_counter = 0;
        return detached_ptr<item>(); //destroy fake smoke
    }

    if( self->item_counter == 0 ) {
        iexamine::cloning_vat_finalize( pos, self->birthday() ); //activate effects when timers goes to zero
        return detached_ptr<item>(); //destroy fake smoke when it 'burns out'
    }

    return std::move( self );
}

detached_ptr<item> item::process_fake_smoke( detached_ptr<item> &&self, player * /*carrier*/,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    map &here = get_map();
    if( here.furn( pos ) != furn_str_id( "f_smoking_rack_active" ) &&
        here.furn( pos ) != furn_str_id( "f_metal_smoking_rack_active" ) ) {
        self->set_counter( 0 );
        return detached_ptr<item>(); //destroy fake smoke
    }

    if( self->age() >= 6_hours || self->item_counter == 0 ) {
        iexamine::on_smoke_out( pos, self->birthday() ); //activate effects when timers goes to zero
        return detached_ptr<item>(); //destroy fake smoke when it 'burns out'
    }

    return std::move( self );
}

detached_ptr<item> item::process_litcig( detached_ptr<item> &&self, player *carrier,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    if( !one_in( 10 ) ) {
        return std::move( self );
    }
    self = self->process_extinguish( std::move( self ), carrier, pos );
    // process_extinguish might have extinguished the item already
    if( !self->is_active() ) {
        return std::move( self );
    }
    item &it = *self;
    map &here = get_map();
    // if carried by someone:
    if( carrier != nullptr ) {
        time_duration duration = 15_seconds;
        if( carrier->has_trait( trait_TOLERANCE ) ) {
            duration = 7_seconds;
        } else if( carrier->has_trait( trait_LIGHTWEIGHT ) ) {
            duration = 30_seconds;
        }
        carrier->add_msg_if_player( m_neutral, _( "You take a puff of your %s." ), it.tname() );

        // we need to figure out a way to get the item before this got converted,
        // but i don't think that's going to be very easy...
        if( it.has_flag( flag_TOBACCO ) ) {
            carrier->add_effect( effect_cig, duration );
        }
        if( it.has_flag( flag_MARIJUANA ) ) {
            carrier->add_effect( effect_weed_high, duration );
        }

        carrier->moves -= 15;

        if( ( carrier->has_effect( effect_shakes ) && one_in( 10 ) ) ) {
            carrier->add_msg_if_player( m_bad, _( "Your shaking hand causes you to drop your %s." ),
                                        it.tname() );
            here.add_item_or_charges( pos + point_rel_ms( rng( -1, 1 ), rng( -1, 1 ) ),
                                      std::move( self ) );
            return detached_ptr<item>(); // removes the item that has just been added to the map
        }

        if( carrier->has_effect( effect_sleep ) ) {
            carrier->add_msg_if_player( m_bad, _( "You fall asleep and drop your %s." ),
                                        it.tname() );
            here.add_item_or_charges( pos + point_rel_ms( rng( -1, 1 ), rng( -1, 1 ) ),
                                      std::move( self ) );
            self = detached_ptr<item>();
        }
    } else {
        // If not carried by someone, but laying on the ground:
        if( it.item_counter % 5 == 0 ) {
            // lit cigarette can start fires
            if( here.flammable_items_at( pos ) ||
                here.has_flag( flag_FLAMMABLE, pos ) ||
                here.has_flag( flag_FLAMMABLE_ASH, pos ) ) {
                here.add_field( pos, fd_fire, 1 );
            }
        }
    }

    // cig dies out
    if( it.item_counter == 0 ) {
        if( carrier != nullptr ) {
            carrier->add_msg_if_player( m_neutral, _( "You finish your %s." ), it.tname() );
        }
        it.convert( dynamic_cast<const iuse_transform *>
                    ( it.type->get_use( "transform" )->get_actor_ptr() )->target );
        if( it.has_flag( flag_MARIJUANA ) ) {
            if( carrier != nullptr ) {
                carrier->add_effect( effect_weed_high, 1_minutes ); // one last puff
                here.add_field( pos + point_rel_ms( rng( -1, 1 ), rng( -1, 1 ) ), fd_weedsmoke,
                                2 );
                weed_msg( *carrier );
            }
        }
        it.deactivate();
    }
    // Item remains
    return std::move( self );
}

detached_ptr<item> item::process_extinguish( detached_ptr<item> &&self, player *carrier,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    // checks for water
    bool extinguish = false;
    bool in_inv = carrier != nullptr && carrier->has_item( *self );
    bool submerged = false;
    bool precipitation = false;
    bool windtoostrong = false;
    bool in_veh = carrier != nullptr && carrier->in_vehicle;
    int windpower = get_weather().windspeed;
    switch( get_weather().weather_id->precip ) {
        case precip_class::very_light:
            precipitation = one_in( 100 );
            break;
        case precip_class::light:
            precipitation = one_in( 50 );
            break;
        case precip_class::medium:
            precipitation = one_in( 25 );
            break;
        case precip_class::heavy:
            precipitation = one_in( 10 );
            break;
        default:
            break;
    }
    map &here = get_map();
    if( in_inv && !in_veh && here.has_flag( flag_DEEP_WATER, pos ) ) {
        extinguish = true;
        submerged = true;
    }
    if( ( !in_inv && here.has_flag( flag_LIQUID, pos ) &&
          !here.veh_at( pos ) ) ||
        ( precipitation && !g->is_sheltered( pos ) ) ) {
        extinguish = true;
    }
    if( in_inv && windpower > 5 && !g->is_sheltered( pos ) &&
        self->has_flag( flag_WIND_EXTINGUISH ) ) {
        windtoostrong = true;
        extinguish = true;
    }
    if( !extinguish ||
        ( in_inv && precipitation && carrier->primary_weapon().has_flag( flag_RAIN_PROTECT ) ) ) {
        return std::move( self ); //nothing happens
    }
    if( carrier != nullptr ) {
        if( submerged ) {
            carrier->add_msg_if_player( m_neutral, _( "Your %s is quenched by water." ), self->tname() );
        } else if( precipitation ) {
            carrier->add_msg_if_player( m_neutral, _( "Your %s is quenched by precipitation." ),
                                        self->tname() );
        } else if( windtoostrong ) {
            carrier->add_msg_if_player( m_neutral, _( "Your %s is blown out by the wind." ),
                                        self->tname() );
        }
    }

    // cig dies out
    if( self->has_flag( flag_LITCIG ) ) {
        self->convert( dynamic_cast<const iuse_transform *>
                       ( self->type->get_use( "transform" )->get_actor_ptr() )->target );
    } else { // transform (lit) items
        if( !self->revert( carrier ) ) {
            self->type->invoke( carrier != nullptr ? *carrier : get_avatar(), *self, pos, "transform" );
        }

    }
    self->deactivate();
    // Item remains
    return std::move( self );
}

detached_ptr<item> item::process_cable( detached_ptr<item> &&self, player *carrier,
                                        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    //No need to process cable if it' has no map connections's only connected to character
    auto data = cable_connection_data::make_data( self.ptr );
    if( !data || data->empty() ) {
        self->reset_cable( carrier );
        return std::move( self );
    }
    if( data->character_only() || data->intermap_connection() ) {
        return std::move( self );
    }
    if( !data->get_nonchar_connection() ) {
        self->reset_cable( carrier );
        return std::move( self );
    }

    int distance = 0;
    map &here = get_map();
    //At this point we are sure that non_char is not empty
    auto nonchar = *data->get_nonchar_connection();

    //Caharacter connected to smth
    if( data->complete() ) {
        if( !carrier ) {
            if( data->get_map_connection() ) {
                data->unset_other_con( self.get(), nonchar );
            } else {
                self->reset_cable( carrier );
            }
            return std::move( self );
        }
        switch( nonchar.state ) {
            case state_solar_pack:
                if( !carrier->has_item( *self ) || !carrier->worn_with_flag( flag_SOLARPACK_ON ) ) {
                    carrier->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                    self->reset_cable( carrier );
                }
                return std::move( self );
            case state_UPS: {
                static const item_filter used_ups = [&]( const item & itm ) {
                    return itm.get_var( "cable" ) == "plugged_in";
                };

                if( !carrier->has_item( *self ) || !carrier->has_item_with( used_ups ) ) {
                    for( item *used : carrier->items_with( used_ups ) ) {
                        used->erase_var( "cable" );
                    }
                    carrier->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                    self->reset_cable( carrier );
                }
                return std::move( self );
            }
            case state_vehicle: {
                if( !here.veh_at( nonchar.point ) ) {
                    carrier->add_msg_if_player( m_bad, _( "You notice the cable has disconnected from a vehicle!" ) );
                    data->unset_con( self.get(), nonchar );
                    return std::move( self );
                }
                //Sitting in vehicle shenenigans
                //If character is sitting at vehicle - no need(and no way) to figure out cable distance and if it's the same vehicle we previously connected
                //So we just ignore distance
                const auto vp_pos = here.veh_at( pos );
                if( vp_pos ) {
                    const auto seat = vp_pos.part_with_feature( "BOARDABLE", true );
                    if( seat && carrier == seat->vehicle().get_passenger( seat->part_index() ) ) {
                        return std::move( self );
                    }
                }
                break;
            }
            case state_grid: {
                auto s = nonchar.point;
                auto *grid_connector = active_tiles::furn_at<vehicle_connector_tile>( s );
                if( !grid_connector ) {
                    if( carrier->has_item( *self ) ) {
                        carrier->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                    }
                    self->reset_cable( carrier );
                    return std::move( self );
                }
                break;
            }
            case state_none:
            case state_self:
            default:
                debugmsg( "Unexpected cable state %s", nonchar.state );
                self->reset_cable( carrier );
                return std::move( self );
        }
    }
    if( nonchar.map_point() ) {
        distance = rl_dist( pos, here.abs_to_bub( nonchar.point ) );
        self->charges = self->type->maximum_charges() - distance;
        if( self->charges < 1 ) {
            if( carrier ) {
                carrier->add_msg_if_player( m_bad, _( "The over-extended cable breaks loose!" ) );
            }
            self->reset_cable( carrier );
        }
    }

    return std::move( self );
}

void item::reset_cable( Character *who )
{
    int max_charges = type->maximum_charges();
    //erase legacy info
    erase_var( "state" );
    erase_var( "source_x" );
    erase_var( "source_y" );
    erase_var( "source_z" );
    //erase info
    cable_connection_data::unset_vars( this );
    deactivate();
    charges = max_charges;

    if( who != nullptr ) {
        who->add_msg_if_player( m_info, _( "You reel in the cable." ) );
        who->mod_moves( -10 * charges );
    }
}

detached_ptr<item> item::process_UPS( detached_ptr<item> &&self, player *carrier,
                                      const tripoint_bub_ms & /*pos*/ )
{
    if( !self ) {
        return std::move( self );
    }
    if( carrier == nullptr ) {
        self->erase_var( "cable" );
        self->deactivate();
        return std::move( self );
    }
    bool has_connected_cable = carrier->has_item_with( []( const item & it ) {
        return it.is_active() && cable_connection_data::ups_connected( &it );
    } );
    if( !has_connected_cable ) {
        self->erase_var( "cable" );
        self->deactivate();
    }
    return std::move( self );
}

bool item::process_wet( player * /*carrier*/, const tripoint_bub_ms & /*pos*/ )
{
    if( item_counter == 0 ) {
        if( is_tool() && type->tool->revert_to ) {
            convert( *type->tool->revert_to );
        }
        unset_flag( flag_WET );
        deactivate();
    }
    // Always return true so our caller will bail out instead of processing us as a tool.
    return true;
}

detached_ptr<item> item::process_tool( detached_ptr<item> &&self, player *carrier,
                                       const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    avatar &you = get_avatar();
    // items with iuse set_transformed which are restricted turn off if not attached to their dependency.
    if( self->type->can_use( "set_transformed" ) ) {
        const set_transformed_iuse *actor = dynamic_cast<const set_transformed_iuse *>
                                            ( self->get_use( "set_transformed" )->get_actor_ptr() );
        if( actor == nullptr ) {
            debugmsg( "iuse_actor type descriptor and actual type mismatch" );
            return std::move( self );
        }
        if( actor->restricted ) {
            if( !carrier ) {
                actor->bypass( carrier != nullptr ? *carrier : you, *self, false, pos );
                return std::move( self );
            } else {
                bool active = false;
                flag_id transform_flag( actor->dependencies );
                for( const auto &elem : carrier->worn ) {
                    if( elem->is_active() && elem->has_flag( transform_flag ) ) {
                        active = true;
                        break;
                    }
                }
                if( !active ) {
                    actor->bypass( carrier != nullptr ? *carrier : you, *self, false, pos );
                    return std::move( self );
                }
            }
        }
    }

    int energy = 0;
    const bool uses_UPS = self->has_flag( flag_USE_UPS );
    bool revert_destroy = false;
    if( self->type->tool->turns_per_charge > 0 ) {
        if( self->type->tool->turns_active >= self->type->tool->turns_per_charge ) {
            energy = std::max( self->ammo_required(), 1 );
            self->type->tool->turns_active = 0;
        }
        self->type->tool->turns_active += 1;
    } else if( self->type->tool->power_draw > 0 ) {
        // power_draw in mW / 1000000 to give kJ (battery unit) per second
        energy = self->type->tool->power_draw / 1000000;
        // energy_bat remainder results in chance at additional charge/discharge
        energy += x_in_y( self->type->tool->power_draw % 1000000, 1000000 ) ? 1 : 0;
    }

    // If ammo_required is 0 we just skip over this and go to tick processing.
    if( energy || self->ammo_required() > 0 ) {
        // No need to look for charges if energy is 0
        if( energy ) {
            energy -= self->ammo_consume( energy, pos );

            // for power armor pieces, try to use power armor interface first.
            if( carrier && self->is_power_armor() && character_funcs::can_interface_armor( *carrier ) ) {
                if( carrier->use_charges_if_avail( itype_bio_armor, energy ) ) {
                    energy = 0;
                }
            }

            // for items in player possession if insufficient charges within tool try UPS
            if( carrier && uses_UPS ) {
                if( carrier->use_charges_if_avail( itype_UPS, energy ) ) {
                    energy = 0;
                }
            }
        }

        // HACK: this means that UPS items will last one more check longer than they should since they don't trigger when
        // their ammo_remaining is 0, since that doesn't check the UPS "stock" available (which is an expensive check)
        // It's done like this cause grenades must be destroyed when charge reaches 0, or it will linger an extra turn.
        if( ( self->ammo_remaining() == 0 && !uses_UPS ) || energy > 0 ) {
            revert_destroy = true;
            if( carrier ) {
                if( self->is_power_armor() ) {
                    if( uses_UPS ) {
                        carrier->add_msg_if_player( m_info, _( "You need a UPS or Bionic Power Interface to run the %s!" ),
                                                    self->tname() );
                    } else {

                    }
                } else if( uses_UPS ) {
                    carrier->add_msg_if_player( m_info, _( "You need a UPS to run the %s!" ), self->tname() );
                }
            }
            if( carrier && self->type->can_use( "set_transform" ) ) {
                const set_transform_iuse *actor = dynamic_cast<const set_transform_iuse *>
                                                  ( self->get_use( "set_transform" )->get_actor_ptr() );
                if( actor == nullptr ) {
                    debugmsg( "iuse_actor type descriptor and actual type mismatch." );
                    return std::move( self );
                }
                flag_id transformed_flag( actor->flag );
                for( auto &elem : carrier->worn ) {
                    if( elem->is_active() && elem->has_flag( transformed_flag ) ) {
                        if( !elem->type->can_use( "set_transformed" ) ) {
                            debugmsg( "Expected set_transformed function" );
                            return std::move( self );
                        }
                        const set_transformed_iuse *actor = dynamic_cast<const set_transformed_iuse *>
                                                            ( elem->get_use( "set_transformed" )->get_actor_ptr() );
                        if( actor == nullptr ) {
                            debugmsg( "iuse_actor type descriptor and actual type mismatch" );
                            return std::move( self );
                        }
                        actor->bypass( *carrier, *elem, false, pos );
                    }
                }
            }
        }
    }

    // Process tick even if it's to be destroyed/reverted later, more for grenades
    // It technically gives an extra turn of action, but before the rework items functioned at 0 charges for a bit anyway.
    // Calls all use functions if active
    if( ( self->get_use( "REMOTEVEH" ) || self->get_use( "RADIOCONTROL" ) ) && self->is_active() ) {
        const use_function *method = nullptr;
        if( g->remoteveh() != nullptr && self->get_use( "REMOTEVEH" ) ) {
            method = &self->type->use_methods.find( "REMOTEVEH" )->second;
        } else if( !g->u.get_value( "remote_controlling" ).empty() && self->get_use( "RADIOCONTROL" ) ) {
            method = &self->type->use_methods.find( "RADIOCONTROL" )->second;
        }
        if( method != nullptr ) {
            method->call( carrier != nullptr ? *carrier : you, *self, true, pos );
        } else {
            self->type->tick( carrier != nullptr ? *carrier : you, *self, pos );
        }
    } else {
        self->type->tick( carrier != nullptr ? *carrier : you, *self, pos );
    }

    if( revert_destroy ) {
        // If no revert is defined, destroy it (candles and the like).
        if( self->is_active() && self->revert( carrier ) ) {
            self->deactivate();
            return std::move( self );
        } else {
            return detached_ptr<item>();
        }
    }

    return std::move( self );
}

detached_ptr<item> item::process_blackpowder_fouling( detached_ptr<item> &&self, player *carrier )
{
    if( !self ) {
        return std::move( self );
    }
    if( self->damage() < self->max_damage() && one_in( 2000 ) ) {
        self->inc_damage( DT_ACID );
        if( carrier ) {
            carrier->add_msg_if_player( m_bad, _( "Your %s rusts due to blackpowder fouling." ),
                                        self->tname() );
        }
    }
    return std::move( self );
}

detached_ptr<item> item::process( detached_ptr<item> &&self, player *carrier,
                                  const tripoint_bub_ms &pos,
                                  bool activate,
                                  temperature_flag flag )
{
    return process( std::move( self ), carrier, pos, activate, flag, get_weather() );
}

detached_ptr<item> item::process( detached_ptr<item> &&self, player *carrier,
                                  const tripoint_bub_ms &pos,
                                  bool activate,
                                  temperature_flag flag, const weather_manager &weather_generator )
{
    if( !self ) {
        return std::move( self );
    }
    const bool preserves = self->type->container && self->type->container->preserves;
    const bool seals = self->type->container && self->type->container->seals;
    item &obj = *self;

    obj.remove_items_with( [&]( detached_ptr<item> &&it ) {
        if( preserves ) {
            it->last_rot_check = calendar::turn;
        }
        it = it->process_internal( std::move( it ), carrier, pos, activate, seals, flag,
                                   weather_generator );
        return VisitResponse::NEXT;
    } );
    detached_ptr<item> res = process_internal( std::move( self ), carrier, pos, activate, seals, flag,
                             weather_generator );
    return res;
}

detached_ptr<item> item::process_internal( detached_ptr<item> &&self, player *carrier,
        const tripoint_bub_ms &pos, bool activate,
        const bool seals, const temperature_flag flag,
        const weather_manager &weather_generator )
{
    ZoneScopedN( "item_process_internal" );
    if( !self ) {
        return std::move( self );
    }
    if( self->has_flag( flag_ETHEREAL_ITEM ) ) {
        ZoneScopedN( "item_process_ethereal" );
        if( !self->has_var( "ethereal" ) ) {
            return detached_ptr<item>();
        }
        self->set_var( "ethereal", std::stoi( self->get_var( "ethereal" ) ) - 1 );
        const bool processed = std::stoi( self->get_var( "ethereal" ) ) <= 0;
        if( processed && carrier != nullptr ) {
            carrier->add_msg_if_player( _( "Your %s disappears!" ), self->tname() );
        }
        if( processed ) {
            return detached_ptr<item>();
        } else {
            return std::move( self );
        }
    }

    {
        ZoneScopedN( "item_process_artifact_relic" );
        self->process_artifact( carrier, pos );
        self->process_relic( carrier );
    }

    if( self->faults.contains( fault_gun_blackpowder ) ) {
        ZoneScopedN( "item_process_faults" );
        return process_blackpowder_fouling( std::move( self ), carrier );
    }

    avatar &you = get_avatar();
    if( activate ) {
        ZoneScopedN( "item_process_activate" );
        if( self->type->invoke( carrier != nullptr ? *carrier : you, *self, pos ) > 0 ) {
            return detached_ptr<item>();
        }
        return std::move( self );
    }
    // How this works: it checks what kind of processing has to be done
    // (e.g. for food, for drying towels, lit cigars), and if that matches,
    // call the processing function. If that function returns true, the item
    // has been destroyed by the processing, so no further processing has to be
    // done.
    // Otherwise processing continues. This allows items that are processed as
    // food and as litcig and as ...

    // Remaining stuff is only done for active items.
    if( !self->is_active() ) {
        ZoneScopedN( "item_process_inactive_return" );
        return std::move( self );
    }

    if( !self->is_food() && self->item_counter > 0 ) {
        ZoneScopedN( "item_process_counter" );
        self->item_counter--;
    }

    if( self->item_counter == 0 && self->type->countdown_action ) {
        ZoneScopedN( "item_process_countdown" );
        self->type->countdown_action.call( carrier ? *carrier : you, *self, false, pos );
        if( self->type->countdown_destroy ) {
            return detached_ptr<item>();
        }
    }

    map &here = get_map();
    if( !self->type->emits.empty() ) {
        ZoneScopedN( "item_process_emits" );
        for( const emit_id &e : self->type->emits ) {
            here.emit_field( pos, e );
        }
    }

    if( self->has_flag( flag_FAKE_SMOKE ) ) {
        ZoneScopedN( "item_process_fake_smoke" );
        self = process_fake_smoke( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->has_flag( flag_FAKE_CLONING_VAT ) ) {
        ZoneScopedN( "item_process_fake_cloning_vat" );
        self = process_fake_cloning_vat( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->has_flag( flag_FAKE_MILL ) ) {
        ZoneScopedN( "item_process_fake_mill" );
        self = process_fake_mill( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->is_corpse() ) {
        ZoneScopedN( "item_process_corpse" );
        self = process_corpse( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->has_flag( flag_WET ) ) {
        ZoneScopedN( "item_process_wet" );
        if( self->process_wet( carrier, pos ) ) {
            // Drying items are never destroyed, but we want to exit so they don't get processed as tools.
            return std::move( self );
        }
    }
    if( self->has_flag( flag_LITCIG ) ) {
        ZoneScopedN( "item_process_litcig" );
        self = process_litcig( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( ( self->has_flag( flag_WATER_EXTINGUISH ) || self->has_flag( flag_WIND_EXTINGUISH ) ) ) {
        ZoneScopedN( "item_process_extinguish" );
        self = process_extinguish( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->has_flag( flag_WATER_DISABLE ) && carrier->is_underwater() ) {
        ZoneScopedN( "item_process_water_disable" );
        carrier->add_msg_if_player( "Your %s gurgles and splutters.", self->tname() );
        self->revert( carrier );
        self->deactivate();
        return std::move( self );
    }
    if( self->has_flag( flag_CABLE_SPOOL ) ) {
        ZoneScopedN( "item_process_cable" );
        // DO NOT process this as a tool! It really isn't!
        return process_cable( std::move( self ), carrier, pos );
    }
    if( self->has_flag( flag_IS_UPS ) ) {
        ZoneScopedN( "item_process_ups" );
        // DO NOT process this as a tool! It really isn't!
        return process_UPS( std::move( self ), carrier, pos );
    }
    if( self->is_tool() ) {
        ZoneScopedN( "item_process_tool" );
        return process_tool( std::move( self ), carrier, pos );
    }
    // All foods that go bad have temperature
    if( ( self->is_food() || self->is_corpse() ) ) {
        ZoneScopedN( "item_process_rot" );
        item &obj = *self;
        self = process_rot( std::move( self ), seals, pos, carrier, flag, weather_generator );
        // If the item has rotted away, then self becomes a null pointer.
        if( !self ) {
            if( obj.is_comestible() ) {
                here.rotten_item_spawn( obj, pos );
            } else if( obj.is_corpse() ) {
                here.handle_decayed_corpse( obj, pos );
            }
        }
    }
    return std::move( self );
}

bool item::can_revive() const
{
    return is_corpse() && corpse->has_flag( MF_REVIVES ) && damage() < max_damage()
    && !( has_flag( flag_FIELD_DRESS ) || has_flag( flag_FIELD_DRESS_FAILED )
    || has_flag( flag_QUARTERED ) || has_flag( flag_SKINNED ) || has_flag( flag_PULPED ) );
}

bool item::ready_to_revive( const tripoint_bub_ms& pos ) const
{
    if( !can_revive() ) { return false; }
if( get_map().veh_at( pos ) ) { return false; }
if( !calendar::once_every( 1_seconds ) ) { return false; }
int age_in_hours = to_hours<int>( age() );
age_in_hours -= static_cast<int>( static_cast<float>( burnt ) / ( volume() / 250_ml ) );
    if( damage_level( 4 ) > 0 ) { age_in_hours /= ( damage_level( 4 ) + 1 ); }
    int rez_factor = 48 - age_in_hours;
    if( age_in_hours > 6 && ( rez_factor <= 0 || one_in( rez_factor ) ) ) {
        // If we're a special revival zombie, wait to get up until the player is nearby.
        const bool isReviveSpecial = has_flag( flag_REVIVE_SPECIAL );
        if( isReviveSpecial ) {
            const int distance = rl_dist( pos, get_player_character().bub_pos() );
            if( distance > 3 ) { return false; }
            if( !one_in( distance + 1 ) ) { return false; }
        }

        return true;
    }
    return false;
}
