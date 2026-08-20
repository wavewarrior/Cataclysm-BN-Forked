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

static const itype_id itype_fertilizer( "fertilizer" );
static const skill_id skill_survival( "survival" );
static const flag_id flag_NO_PAINT( "NO_PAINT" );
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

void colors_to_vars(
    data_vars::data_set& vars, const RGBColorPair& col,
    const iuse_paint_stuff_config::paint_layer layer )
{
    switch( layer ) {
        default:
        case iuse_paint_stuff_config::both:
            if( col.fg == col.bg ) {
                vars.set<RGBColor>( TINT_COLOR_VAR_NAME, col.fg );
                vars.erase( TINT_COLOR_FG_VAR_NAME );
                vars.erase( TINT_COLOR_BG_VAR_NAME );
            } else {
                vars.erase( TINT_COLOR_VAR_NAME );
                vars.set<RGBColor>( TINT_COLOR_FG_VAR_NAME, col.fg );
                vars.set<RGBColor>( TINT_COLOR_BG_VAR_NAME, col.bg );
            }
            break;
        case iuse_paint_stuff_config::fg:
            vars.set<RGBColor>( TINT_COLOR_FG_VAR_NAME, col.fg );
            break;
        case iuse_paint_stuff_config::bg:
            vars.set<RGBColor>( TINT_COLOR_BG_VAR_NAME, col.bg );
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
        colors_to_vars( it->item_vars(), col, layer );
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
        colors_to_vars( *get_vars( p ), col, layer );
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
        const auto col = RGBColor::try_parse( lst.entries[lst.ret].txt ).value_or( RGBColor{} );
        it.set_var<RGBColor>( iuse_paint_stuff::PAINT_VAR, col );
        colors_to_vars( it.item_vars(), RGBColorPair{.bg = col, .fg = col}, both );
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

