#pragma once

#include "activity_actor.h"
#include "activity_handlers.h"
#include "craft_command.h"

#include <optional>

#include "coordinates.h"
#include "crafting.h"
#include "item_handling_util.h"
#include "location_ptr.h"
#include "locations.h"
#include "memory_fast.h"
#include "pickup_token.h"
#include "point.h"
#include "type_id.h"
#include "units_energy.h"
#include "player_activity.h"

class Creature;
class vehicle;
struct partial_con;

class aim_activity_actor : public activity_actor
{
    private:
        safe_reference<item> weapon;
        location_ptr<item> fake_weapon;
        units::energy bp_cost_per_shot = 0_J;
        int stamina_cost_per_shot = 0;
        std::vector<tripoint_bub_ms> fin_trajectory;

    public:
        std::string action;
        int aif_duration = 0; // Counts aim-and-fire duration
        bool aiming_at_critter = false; // Whether aiming at critter or a tile
        bool snap_to_target = false;
        bool shifting_view = false;
        tripoint_rel_ms initial_view_offset;
        /** Target UI requested to abort aiming */
        bool aborted = false;
        /** RELOAD_AND_SHOOT weapon is kept loaded by the activity */
        bool loaded_RAS_weapon = false;
        /** Item location for RAS weapon reload */
        safe_reference<item> reload_loc;
        /** if true abort if no targets are available when re-entering aiming ui after shooting */
        bool abort_if_no_targets = false;
        /**
         * Target UI requested to abort aiming and reload weapon
         * Implies aborted = true
         */
        bool reload_requested = false;
        /**
         * A friendly creature may enter line of fire during aim-and-shoot,
         * and that generates a warning to proceed/abort. If player decides to
         * proceed, that creature is saved in this vector to prevent the same warning
         * from popping up on the following turn(s).
         */
        std::vector<weak_ptr_fast<Creature>> acceptable_losses;

        aim_activity_actor();

        /** Aiming wielded gun */
        static std::unique_ptr<aim_activity_actor> use_wielded();

        /** Aiming fake gun provided by a bionic */
        static std::unique_ptr<aim_activity_actor> use_bionic( detached_ptr<item> &&fake_gun,
                const units::energy &cost_per_shot );

        /** Aiming gun provided by gear */
        static std::unique_ptr<aim_activity_actor> use_gear( item *gun );

        /** Aiming fake gun provided by a mutation */
        static std::unique_ptr<aim_activity_actor> use_mutation( detached_ptr<item> &&fake_gun );

        activity_id get_type() const override {
            return activity_id( "ACT_AIM" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;
        void canceled( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        item *get_weapon();
        void restore_view();
        // Load/unload a RELOAD_AND_SHOOT weapon
        bool load_RAS_weapon();
        void unload_RAS_weapon();
};

class autodrive_activity_actor : public activity_actor
{
    private:
        vehicle *player_vehicle = nullptr;

    public:
        autodrive_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_AUTODRIVE" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void canceled( player_activity &, Character & ) override;
        void finish( player_activity &act, Character & ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class craft_activity_actor final : public activity_actor
{
    protected:
        const recipe *rec = nullptr;
        int batch_size = 1;
        int craft_counter = 0;  // 0 to 10,000,000 — mirrors item's counter field
        tripoint_abs_ms location;

        std::vector<comp_selection<item_comp>> item_selections;
        std::vector<comp_selection<tool_comp>> tool_selections;

        bool tools_prepaid = false;
        bool is_long = false;
        bool is_valid = false;
        int last_turn_nr = -1;  // turn# when last do_turn ran; -1 = never set
        float cached_tools_mult = 0.0f;   // 0 = not yet computed; set once in start()

        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const auto &c_actor = static_cast<const craft_activity_actor &>( other );
            return equivalent_activity( c_actor );
        }

        bool equivalent_activity( const craft_activity_actor &other ) const {
            return location == other.location &&
                   rec == other.rec &&
                   batch_size == other.batch_size;
        }

    public:
        craft_activity_actor() = default;
        explicit craft_activity_actor(
            const recipe *rec,
            int batch_size = 1,
            int craft_counter = 0,
            const tripoint_abs_ms &location = tripoint_abs_ms::zero(),
            std::vector<comp_selection<item_comp>> item_selections = {},
            std::vector<comp_selection<tool_comp>> tool_selections = {},
            bool tools_prepaid = false,
            bool is_long = false
        );

        activity_id get_type() const override {
            return activity_id( "ACT_CRAFT" );
        }

        void calc_all_moves( player_activity &act, Character &who ) override;
        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;
        void canceled( player_activity &/*act*/, Character &/*who*/ ) override {}

        const recipe *get_recipe() const { return rec; }
        int get_batch_size() const { return batch_size; }
        int get_craft_counter() const { return craft_counter; }
        const tripoint_abs_ms &get_location() const { return location; }
        bool are_tools_prepaid() const { return tools_prepaid; }

        act_progress_message get_progress_message( const player_activity &act,
                const Character &who ) const override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        auto find_in_progress_craft( const player_activity &act,
                                     Character &who ) const -> item *; // *NOPAD*
        void do_complete_craft( player_activity &act, Character &who );
        void refresh_speed( player_activity &act, const Character &who, const item &craft_item,
                            std::optional<bench_location> bench = std::nullopt ) const;

};

class dig_activity_actor : public activity_actor
{
    private:
        int moves_total;
        /** location of the dig **/
        tripoint_bub_ms location;
        std::string result_terrain;
        tripoint_bub_ms byproducts_location;
        std::string byproducts_item_group;

        /**
         * Returns true if @p other and `this` are "equivalent" in the sense that
         *  `this` can be resumed instead of starting @p other.
         */
        bool equivalent_activity( const dig_activity_actor &other ) const {
            return  location == other.location &&
                    result_terrain == other.result_terrain &&
                    byproducts_location == other.byproducts_location &&
                    byproducts_item_group == other.byproducts_item_group;
        }

        /**
         * @pre @p other is a `dig_activity_actor`
         */
        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const dig_activity_actor &d_actor = static_cast<const dig_activity_actor &>( other );
            return equivalent_activity( d_actor );
        }

    public:
        dig_activity_actor(
            int dig_moves, const tripoint_bub_ms &dig_loc,
            const std::string &resulting_ter, const tripoint_bub_ms &dump_loc,
            const std::string &dump_item_group
        ):
            moves_total( dig_moves ), location( dig_loc ),
            result_terrain( resulting_ter ),
            byproducts_location( dump_loc ),
            byproducts_item_group( dump_item_group ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_DIG" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class dig_channel_activity_actor : public activity_actor
{
    private:
        int moves_total;
        /** location of the dig **/
        tripoint_bub_ms location;
        std::string result_terrain;
        tripoint_bub_ms byproducts_location;
        std::string byproducts_item_group;

        /**
         * Returns true if @p other and `this` are "equivalent" in the sense that
         *  `this` can be resumed instead of starting @p other.
         */
        bool equivalent_activity( const dig_channel_activity_actor &other ) const {
            return  location == other.location &&
                    result_terrain == other.result_terrain &&
                    byproducts_location == other.byproducts_location &&
                    byproducts_item_group == other.byproducts_item_group;
        }

        /**
         * @pre @p other is a `dig_activity_actor`
         */
        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const dig_channel_activity_actor &dc_actor = static_cast<const dig_channel_activity_actor &>
                    ( other );
            return equivalent_activity( dc_actor );
        }

    public:
        dig_channel_activity_actor(
            int dig_moves, const tripoint_bub_ms &dig_loc,
            const std::string &resulting_ter, const tripoint_bub_ms &dump_loc,
            const std::string &dump_item_group
        ):
            moves_total( dig_moves ), location( dig_loc ),
            result_terrain( resulting_ter ),
            byproducts_location( dump_loc ),
            byproducts_item_group( dump_item_group ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_DIG_CHANNEL" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class disassemble_activity_actor : public activity_actor
{
    private:
        std::vector<iuse_location> targets;
        tripoint_abs_ms pos;
        bool recursive = false;

    public:
        disassemble_activity_actor() = default;
        disassemble_activity_actor(
            std::vector<iuse_location> &&targets,
            tripoint_abs_ms pos,
            bool recursive
        ) : targets( std::move( targets ) ), pos( pos ), recursive( recursive ) {}
        ~disassemble_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_DISASSEMBLE" );
        }
        void calc_all_moves( player_activity &act, Character &who ) override;
        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        bool try_start_single( player_activity &act, Character &who );
        void process_target( player_activity &, iuse_location &target );
};

class drop_activity_actor : public activity_actor
{
    private:
        std::list<pickup::act_item> items;
        bool force_ground = false;
        tripoint_rel_ms relpos;

    public:
        drop_activity_actor() = default;
        drop_activity_actor( Character &ch, const drop_locations &items,
                             bool force_ground, const tripoint_rel_ms &relpos );

        activity_id get_type() const override {
            return activity_id( "ACT_DROP" );
        }

        void start( player_activity &, Character & ) override;
        void do_turn( player_activity &, Character &who ) override;
        void finish( player_activity &, Character & ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class hacking_activity_actor : public activity_actor
{
    private:
        bool using_bionic = false;

    public:
        struct use_bionic {};

        hacking_activity_actor() = default;
        hacking_activity_actor( use_bionic );

        activity_id get_type() const override {
            return activity_id( "ACT_HACKING" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class repair_item_activity_actor : public activity_actor
{
    public:
        repair_item_activity_actor() = default;
        repair_item_activity_actor(
            safe_reference<item> tool,
            const std::string &iuse_name
        ) : tool_item( tool ), iuse_name_string( iuse_name ) {}

        void set_hack_vehicle( const tripoint_abs_ms &pos, int crafter_idx,
                               const itype_id &tool_type ) {
            hack_type = hack_type_t::vehicle;
            hack_position = pos;
            hack_crafter_index = crafter_idx;
            hack_tool_type_id = tool_type;
        }
        void set_hack_furniture( const tripoint_abs_ms &pos, const itype_id &tool_type ) {
            hack_type = hack_type_t::furniture;
            hack_position = pos;
            hack_tool_type_id = tool_type;
        }

        activity_id get_type() const override {
            return activity_id( "ACT_REPAIR_ITEM" );
        }

        void start( player_activity &, Character & ) override {}
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        enum class hack_type_t : int { none = -1, vehicle = 0, furniture = 1 };
        enum repeat_type : int {
            REPEAT_INIT = 0, REPEAT_ONCE, REPEAT_FOREVER, REPEAT_FULL, REPEAT_EVENT, REPEAT_CANCEL
        };

        std::string iuse_name_string;
        repeat_type repeat = REPEAT_INIT;
        safe_reference<item> tool_item;
        safe_reference<item> fix_item;

        hack_type_t hack_type = hack_type_t::none;
        tripoint_abs_ms hack_position;
        itype_id hack_tool_type_id;
        int hack_crafter_index = 0;

        auto get_fake_tool() const -> item *; // *NOPAD*
        void discharge_real_power_source( item &tool, int original_charges ) const;
        repeat_type show_repeat_menu( const std::string &title, repeat_type last_selection ) const;
};

class mend_item_activity_actor : public activity_actor
{
    public:
        mend_item_activity_actor() = default;
        mend_item_activity_actor(
            safe_reference<item> target,
            const std::string &fault,
            const std::string &method
        ) : target_item( target ), fault_id_str( fault ), method_id( method ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_MEND_ITEM" );
        }

        void start( player_activity &, Character & ) override {}
        void do_turn( player_activity &, Character & ) override {}
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        safe_reference<item> target_item;
        std::string fault_id_str;
        std::string method_id;
};

class toolmod_add_activity_actor : public activity_actor
{
    public:
        toolmod_add_activity_actor() = default;
        toolmod_add_activity_actor(
            safe_reference<item> tool,
            safe_reference<item> mod
        ) : base_tool( tool ), mod_item( mod ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_TOOLMOD_ADD" );
        }

        void start( player_activity &, Character & ) override {}
        void do_turn( player_activity &, Character & ) override {}
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        safe_reference<item> base_tool;
        safe_reference<item> mod_item;
};

class gunmod_add_activity_actor : public activity_actor
{
    public:
        gunmod_add_activity_actor() = default;
        gunmod_add_activity_actor(
            safe_reference<item> gun,
            safe_reference<item> mod,
            int success_chance,
            int damage_chance,
            const itype_id &tool,
            int charges_qty
        ) : gun_item( gun ), mod_item( mod ),
            roll( success_chance ), risk( damage_chance ),
            tool_id( tool ), qty( charges_qty ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_GUNMOD_ADD" );
        }

        void start( player_activity &, Character & ) override {}
        void do_turn( player_activity &, Character & ) override {}
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        safe_reference<item> gun_item;
        safe_reference<item> mod_item;
        int roll = 0;
        int risk = 0;
        itype_id tool_id;
        int qty = 0;
};

class reload_activity_actor : public activity_actor
{
    public:
        reload_activity_actor() = default;
        reload_activity_actor(
            safe_reference<item> target,
            safe_reference<item> ammo,
            int quantity
        ) : target_item( target ), ammo_item( ammo ), qty( quantity ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_RELOAD" );
        }

        void start( player_activity &, Character & ) override {}
        void do_turn( player_activity &, Character & ) override {}
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        safe_reference<item> target_item;
        safe_reference<item> ammo_item;
        int qty = 0;
};

class wear_activity_actor : public activity_actor
{
    public:
        wear_activity_actor() = default;
        wear_activity_actor(
            std::vector<safe_reference<item>> items,
            std::vector<int> quantities
        ) : items( std::move( items ) ), quantities( std::move( quantities ) ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_WEAR" );
        }

        void start( player_activity &, Character & ) override {}
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {}

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        std::vector<safe_reference<item>> items;
        std::vector<int> quantities;
};

class armor_layers_activity_actor : public activity_actor
{
    public:
        armor_layers_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_ARMOR_LAYERS" );
        }

        void start( player_activity &, Character & ) override {}
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {}

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class hacksaw_activity_actor : public activity_actor
{
    public:
        explicit hacksaw_activity_actor( const tripoint_bub_ms &target,
                                         const safe_reference<item> &tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_HACKSAW" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        // debugmsg causes a backtrace when fired during cata_test
        bool testing = false;  // NOLINT(cata-serialize)
    private:
        tripoint_bub_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const hacksaw_activity_actor &actor = static_cast<const hacksaw_activity_actor &>
                                                  ( other );
            return actor.target == target;
        }
};

class boltcutting_activity_actor : public activity_actor
{
    public:
        explicit boltcutting_activity_actor( const tripoint_bub_ms &target,
                                             const safe_reference<item> tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_BOLTCUTTING" );
        }

        void start( player_activity &act, Character &/*who*/ ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        // debugmsg causes a backtrace when fired during cata_test
        bool testing = false;

    private:
        tripoint_bub_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const boltcutting_activity_actor &actor = static_cast<const boltcutting_activity_actor &>
                    ( other );
            return actor.target == target && actor.tool == tool;
        }
};

class lockpick_activity_actor : public activity_actor
{
    private:
        int moves_total;
        safe_reference<item> lockpick;
        location_ptr<item> fake_lockpick;
        tripoint_abs_ms target;

        lockpick_activity_actor(
            int moves_total,
            safe_reference<item> lockpick,
            detached_ptr<item> &&fake_lockpick,
            const tripoint_abs_ms &target
        ) : moves_total( moves_total ), lockpick( lockpick ), fake_lockpick( new fake_item_location() ),
            target( target ) {
            this->fake_lockpick = std::move( fake_lockpick );
        };

    public:
        /** Use regular lockpick. 'target' is in global coords */
        static std::unique_ptr<lockpick_activity_actor> use_item(
            int moves_total,
            item &lockpick,
            const tripoint_abs_ms &target
        );

        /** Use bionic lockpick. 'target' is in global coords */
        static std::unique_ptr<lockpick_activity_actor> use_bionic(
            detached_ptr<item> &&fake_lockpick,
            const tripoint_abs_ms &target
        );

        activity_id get_type() const override {
            return activity_id( "ACT_LOCKPICK" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        static bool is_pickable( const tripoint_bub_ms &p );
        static std::optional<tripoint_bub_ms> select_location( avatar &you );

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class migration_cancel_activity_actor : public activity_actor
{
    public:
        migration_cancel_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_MIGRATION_CANCEL" );
        }

        void start( player_activity &, Character & ) override {};
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class move_items_activity_actor : public activity_actor
{
    private:
        std::vector<safe_reference<item>> target_items;
        std::vector<int> quantities;
        bool to_vehicle;
        tripoint_rel_ms relative_destination;

    public:
        move_items_activity_actor( std::vector<item *> items, std::vector<int> quantities,
                                   bool to_vehicle, tripoint_rel_ms relative_destination ) :
            quantities( quantities ), to_vehicle( to_vehicle ),
            relative_destination( relative_destination ) {

            for( item *&it : items ) {
                target_items.emplace_back( it );
            }
        }

        activity_id get_type() const override {
            return activity_id( "ACT_MOVE_ITEMS" );
        }

        void start( player_activity &, Character & ) override {};
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};


        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class toggle_gate_activity_actor : public activity_actor
{
    private:
        int moves_total;
        tripoint_bub_ms placement;

        /**
         * @pre @p other is a toggle_gate_activity_actor
         */
        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const toggle_gate_activity_actor &og_actor = static_cast<const toggle_gate_activity_actor &>
                    ( other );
            return placement == og_actor.placement;
        }

    public:
        toggle_gate_activity_actor( int gate_moves, const tripoint_bub_ms &gate_placement ) :
            moves_total( gate_moves ), placement( gate_placement ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_TOGGLE_GATE" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character & ) override;


        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class pickup_activity_actor : public activity_actor
{
    private:
        /** Target items and the quantities thereof */
        std::vector<pickup::pick_drop_selection> target_items;

        /**
         * Position of the character when the activity is started. This is
         * stored so that we can cancel the activity if the player moves
         * (e.g. if the player is in a moving vehicle). This should be null
         * if not grabbing from the ground.
         */
        std::optional<tripoint_bub_ms> starting_pos;

    public:
        pickup_activity_actor( const std::vector<pickup::pick_drop_selection> &target_items,
                               const std::optional<tripoint_bub_ms> &starting_pos )
            : target_items( target_items )
            , starting_pos( starting_pos ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_PICKUP" );
        }

        void start( player_activity &, Character & ) override {};
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};


        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class stash_activity_actor : public activity_actor
{
    private:
        std::list<pickup::act_item> items;
        tripoint_rel_ms relpos;

    public:
        stash_activity_actor() = default;
        stash_activity_actor( Character &ch, const drop_locations &items, const tripoint_rel_ms &relpos );

        activity_id get_type() const override {
            return activity_id( "ACT_STASH" );
        }

        void start( player_activity &, Character & ) override;
        void do_turn( player_activity &, Character &who ) override;
        void finish( player_activity &, Character & ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class throw_activity_actor : public activity_actor
{
    private:

        safe_reference<item> target;
        std::optional<tripoint_bub_ms> blind_throw_from_pos;

    public:
        throw_activity_actor() = default;
        throw_activity_actor(
            item &target,
            std::optional<tripoint_bub_ms> blind_throw_from_pos
        ) : target( &target ),
            blind_throw_from_pos( blind_throw_from_pos ) {}
        ~throw_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_THROW" );
        }

        void start( player_activity &, Character & ) override {};
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};


class oxytorch_activity_actor : public activity_actor
{
    public:
        explicit oxytorch_activity_actor( const tripoint_bub_ms &target,
                                          const safe_reference<item> &tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_OXYTORCH" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        // debugmsg causes a backtrace when fired during cata_test
        bool testing = false;  // NOLINT(cata-serialize)
    private:
        tripoint_bub_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const oxytorch_activity_actor &actor = static_cast<const oxytorch_activity_actor &>
                                                   ( other );
            return actor.target == target;
        }
};

class construction_activity_actor : public activity_actor
{
    private:
        tripoint_abs_ms target;
        partial_con *pc;
    public:
        explicit construction_activity_actor( const tripoint_abs_ms &target ) : target( target ) {
        };

        activity_id get_type() const override {
            return activity_id( "ACT_BUILD" );
        }

        void calc_all_moves( player_activity &act, Character &who ) override;

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class assist_activity_actor : public activity_actor
{
    public:
        explicit assist_activity_actor() {
        };

        activity_id get_type() const override {
            return activity_id( "ACT_ASSIST" );
        }

        void calc_all_moves( player_activity & /*act*/, Character &/*who*/ ) override {};

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &/*who*/ ) override {};
        void finish( player_activity &/*act*/, Character &/*who*/ ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

};

class burrow_activity_actor : public activity_actor
{
    public:
        explicit burrow_activity_actor( const tripoint_abs_ms &target ) : target( target ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_BURROW" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const burrow_activity_actor &actor = static_cast<const burrow_activity_actor &>( other );
            return actor.target == target;
        }
};

class pickaxe_activity_actor : public activity_actor
{
    public:
        explicit pickaxe_activity_actor( const tripoint_abs_ms &target,
                                         const safe_reference<item> tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_PICKAXE" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const pickaxe_activity_actor &actor = static_cast<const pickaxe_activity_actor &>( other );
            return actor.target == target && actor.tool == tool;
        }
};

class jackhammer_activity_actor : public activity_actor
{
    public:
        explicit jackhammer_activity_actor( const tripoint_abs_ms &target,
                                            const safe_reference<item> tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_JACKHAMMER" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const jackhammer_activity_actor &actor = static_cast<const jackhammer_activity_actor &>( other );
            return actor.target == target && actor.tool == tool;
        }
};

class churn_activity_actor : public activity_actor
{
    public:
        explicit churn_activity_actor( const tripoint_abs_ms &target ) : target( target ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_CHURN" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const churn_activity_actor &actor = static_cast<const churn_activity_actor &>( other );
            return actor.target == target;
        }
};

class fill_pit_activity_actor : public activity_actor
{
    public:
        explicit fill_pit_activity_actor( const tripoint_abs_ms &target,
                                          const safe_reference<item> tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_FILL_PIT" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const fill_pit_activity_actor &actor = static_cast<const fill_pit_activity_actor &>( other );
            return actor.target == target && actor.tool == tool;
        }
};

class clear_rubble_activity_actor : public activity_actor
{
    public:
        explicit clear_rubble_activity_actor( const tripoint_abs_ms &target ) : target( target ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_CLEAR_RUBBLE" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const clear_rubble_activity_actor &actor = static_cast<const clear_rubble_activity_actor &>( other );
            return actor.target == target;
        }
};

class pry_nails_activity_actor : public activity_actor
{
    public:
        explicit pry_nails_activity_actor( const tripoint_abs_ms &target ) : target( target ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_PRY_NAILS" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const pry_nails_activity_actor &actor = static_cast<const pry_nails_activity_actor &>( other );
            return actor.target == target;
        }
};

class plant_seed_activity_actor : public activity_actor
{
    public:
        explicit plant_seed_activity_actor( const tripoint_abs_ms &target,
                                            const itype_id &seed ) : target( target ), seed_id( seed ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_PLANT_SEED" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;
        itype_id seed_id;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const plant_seed_activity_actor &actor = static_cast<const plant_seed_activity_actor &>( other );
            return actor.target == target && actor.seed_id == seed_id;
        }
};

class forage_activity_actor : public activity_actor
{
    public:
        explicit forage_activity_actor( const tripoint_abs_ms &target,
                                        bool auto_resume ) : target( target ), auto_resume( auto_resume ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_FORAGE" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        tripoint_abs_ms target;
        bool auto_resume;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const forage_activity_actor &actor = static_cast<const forage_activity_actor &>( other );
            return actor.target == target && actor.auto_resume == auto_resume;
        }
};

class hand_crank_activity_actor : public activity_actor
{
    public:
        explicit hand_crank_activity_actor( const safe_reference<item> tool,
                                            const std::vector<int> &values,
                                            const std::vector<std::string> &str_values ) : tool( tool ), values( values ), str_values( str_values ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_HAND_CRANK" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &/*act*/, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        safe_reference<item> tool;
        std::vector<int> values;
        std::vector<std::string> str_values;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const hand_crank_activity_actor &actor = static_cast<const hand_crank_activity_actor &>( other );
            return actor.tool == tool && actor.values == values && actor.str_values == str_values;
        }
};

class fill_liquid_activity_actor : public activity_actor
{
    public:
        fill_liquid_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_FILL_LIQUID" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &/*act*/, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        // Serialized via serialize_liquid_source/serialize_liquid_target into the activity
        // so we keep a reference to the activity for those external helpers.
        // Non-owning pointer, the activity outlives the actor.
        player_activity *parent_activity = nullptr; // *NOPAD*
};

class fertilize_plot_activity_actor : public activity_actor
{
    public:
        fertilize_plot_activity_actor() = default;
        explicit fertilize_plot_activity_actor( const std::string &fertilizer_str ) : str_value( fertilizer_str ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_FERTILIZE_PLOT" );
        }

        void start( player_activity &/*act*/, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &/*act*/, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        std::string str_value;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const fertilize_plot_activity_actor &actor = static_cast<const fertilize_plot_activity_actor &>( other );
            return actor.str_value == str_value;
        }
};

class salvage_activity_actor : public activity_actor
{
    private:
        iuse_locations targets;
        tripoint_abs_ms pos;
        bool mute_prompts = false;
    public:
        salvage_activity_actor() = default;
        salvage_activity_actor(
            iuse_locations &&targets,
            tripoint_abs_ms pos
        ) : targets( std::move( targets ) ), pos( pos ) {}

        ~salvage_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_LONGSALVAGE" );
        }

        void calc_all_moves( player_activity & /*act*/, Character &/*who*/ ) override;

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &/*who*/ ) override;
        void finish( player_activity &/*act*/, Character &/*who*/ ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class butchery_activity_actor : public activity_actor
{
    private:
        butcher_type type;
        std::vector<safe_reference<item>> targets;
        tripoint_abs_ms placement;

        auto setup_next_target( player_activity &act, Character &who ) -> bool; // *NOPAD*

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const butchery_activity_actor &actor = static_cast<const butchery_activity_actor &>( other );
            return actor.type == type && actor.placement == placement;
        }

    public:
        butchery_activity_actor() = default;
        butchery_activity_actor(
            butcher_type type,
            const std::vector<item *> &targets,
            const tripoint_abs_ms &placement = tripoint_abs_ms()
        ) : type( type ), placement( placement ) {
            for( item *it : targets ) {
                this->targets.emplace_back( it );
            }
        }

        butchery_activity_actor(
            butcher_type type,
            std::vector<safe_reference<item>> &&targets,
            const tripoint_abs_ms &placement = tripoint_abs_ms()
        ) : type( type ), targets( std::move( targets ) ), placement( placement ) {}

        activity_id get_type() const override {
            switch( type ) {
                case BUTCHER:
                    return activity_id( "ACT_BUTCHER" );
                case BUTCHER_FULL:
                    return activity_id( "ACT_BUTCHER_FULL" );
                case F_DRESS:
                    return activity_id( "ACT_FIELD_DRESS" );
                case SKIN:
                    return activity_id( "ACT_SKIN" );
                case QUARTER:
                    return activity_id( "ACT_QUARTER" );
                case BLEED:
                    return activity_id( "ACT_BLEED" );
                case DISMEMBER:
                    return activity_id( "ACT_DISMEMBER" );
                case DISSECT:
                    return activity_id( "ACT_DISSECT" );
            }
            return activity_id( "ACT_BUTCHER" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};
enum class consume_menu_type { EAT, FOOD, DRINK, MEDS };

class consume_menu_activity_actor : public activity_actor
{
    private:
        consume_menu_type menu_type;

    public:
        explicit consume_menu_activity_actor( consume_menu_type type ) : menu_type( type ) {}

        activity_id get_type() const override;

        void start( player_activity &, Character & ) override {}
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &, Character & ) override {}

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class firstaid_activity_actor : public activity_actor
{
    private:
        safe_reference<item> healing_item;
        std::string body_part;
        int moves = 0;

    public:
        firstaid_activity_actor() = default;
        firstaid_activity_actor( const safe_reference<item> &item, const std::string &part, int moves_ )
            : healing_item( item ), body_part( part ), moves( moves_ ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_FIRSTAID" );
        }

        void start( player_activity &act, Character & ) override {
            act.moves_left = moves;
        }
        void do_turn( player_activity &, Character & ) override {}
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

enum class wood_chop_type { TREE, LOGS, PLANKS };

class wood_chop_activity_actor : public activity_actor
{
    private:
        wood_chop_type chop_type;
        tripoint_abs_ms placement;
        safe_reference<item> axe;
        int moves = 0;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const wood_chop_activity_actor &actor = static_cast<const wood_chop_activity_actor &>( other );
            return actor.chop_type == chop_type && actor.placement == placement;
        }

    public:
        wood_chop_activity_actor() = default;
        wood_chop_activity_actor( wood_chop_type type, const tripoint_abs_ms &place, int moves_,
                                  const safe_reference<item> &tool = safe_reference<item>() )
            : chop_type( type ), placement( place ), moves( moves_ ), axe( tool ) {}

        activity_id get_type() const override;

        void start( player_activity &act, Character & ) override {
            act.moves_left = moves;
        }
        void do_turn( player_activity &act, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};
