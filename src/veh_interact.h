#pragma once

#include "color.h"
#include "coordinates.h"
#include "cursesdef.h"
#include "input.h"
#include "inventory.h"
#include "memory_fast.h"
#include "player_activity_ptr.h"
#include "rml_screen.h"
#include "type_id.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

class player;
class vpart_info;
struct requirement_data;

/** Represents possible return values from the cant_do function. */
enum task_reason {
    UNKNOWN_TASK = -1, // No such task
    CAN_DO,            // Task can be done
    INVALID_TARGET,    // No valid target i.e. can't "change tire" if no tire present
    LACK_TOOLS,        // Player doesn't have all the tools they need
    NOT_FREE,          // Part is attached to something else and can't be unmounted
    LACK_SKILL,        // Player doesn't have high enough mechanics skill
    MOVING_VEHICLE,    // vehicle is moving, no modifications allowed
    LOW_MORALE,        // Player has too low morale (for operations that require it)
    LOW_LIGHT,         // Player cannot see enough to work (for operations that require it)
    DOUBLE_STACK // Player cannot interact with a vehicle that is blocked off by another vehicle
};

class ui_adaptor;
class vehicle;
struct vehicle_part;
struct vehicle_preview_window;

// For marking 'leaking' tanks/reactors/batteries
const std::string leak_marker = "<color_red>*</color>";

class veh_interact
{
        using part_selector = std::function<bool( const vehicle_part& pt )>;

    public:
        static std::unique_ptr<player_activity> run( vehicle& veh, tripoint_mnt_veh p );

        /** Prompt for a part matching the selector function */
        static vehicle_part &select_part(
            const vehicle& veh, const part_selector& sel, const std::string& title = std::string() );

        static void complete_vehicle( Character& who );

    private:
        veh_interact( vehicle& veh, tripoint_mnt_veh p = tripoint_mnt_veh::zero() );
        ~veh_interact();

        item *target = nullptr;

        tripoint_mnt_veh vehicle_cursor = tripoint_mnt_veh::zero();
        tripoint_rel_ms stored_view_offset;
        /* starting offset for vehicle parts description display and max offset for scrolling */
        int start_at = 0;
        int start_limit = 0;
        /* starting offset for the parts list display */
        int parts_list_offset = 0;
        /* starting offset for the overview and the max offset for scrolling */
        int overview_offset = 0;
        int overview_limit = 0;

        const vpart_info *sel_vpart_info = nullptr;
        // Command currently being run by the player
        char sel_cmd = ' ';

        const vehicle_part *sel_vehicle_part = nullptr;

        int cpart = -1;
        int page_size;
        int fuel_index = 0; /** Starting index of where to start printing fuels from */
        // height of the stats window
        const int stats_h = 8;
        catacurses::window w_border;
        catacurses::window w_mode;
        catacurses::window w_msg;
        catacurses::window w_disp;
        catacurses::window w_parts;
        catacurses::window w_stats;
        catacurses::window w_list;
        catacurses::window w_details;
        catacurses::window w_name;

        std::unique_ptr<vehicle_preview_window> tile_preview;

        bool ui_hidden = false;
        weak_ptr_fast<ui_adaptor> ui;

        std::optional<std::string> title;
        std::optional<std::string> msg;

        int highlight_part = -1;

        /// Set by rml_on_overview() when a clicked overview row carries a
        /// per-part hotkey letter (see part_option::hotkey / calc_overview()).
        /// Checked and cleared once at the top of do_main_loop()'s while loop,
        /// which replicates the same overview_opts hotkey lookup used by the
        /// keyboard path in overview() (raw-hotkey branch) to fire the bound
        /// overview_action. '\0' means no pending hotkey.
        char pending_hotkey = '\0';

        /// Set by rml_on_install() when a click on an install row should
        /// commit that part, mirroring pressing CONFIRM/Enter on it. Checked
        /// and cleared once at the top of do_install()'s while loop, which
        /// then falls into the existing CONFIRM branch (reason/shape-select
        /// logic reused as-is) instead of duplicating it.
        bool pending_install_confirm = false;

        struct install_info_t;
        std::unique_ptr<install_info_t> install_info;

        vehicle *veh;
        inventory crafting_inv;
        input_context main_context;

        // RmlUi render path (§8.1 gate-blocker backlog, the giant — SLICED).
        // Slice 1: lifecycle harness + the two zero-dependency panes (the action
        // mode bar + the vehicle name). The number-heavy panes (stats / overview),
        // the part list + msg (need vehicle.cpp text producers), the 2D vehicle
        // diagram, and the install/repair sub-mode panes land in later slices.
        // `rml` is the F.3 harness doc; `rml_data` (RmlUi types) is pimpl'd into
        // the .cpp. Render-only; the keyboard owns all vehicle actions.
        struct veh_rml_data;
        std::unique_ptr<veh_rml_data> rml_data;
        rml_doc rml;
        void sync_rml();

        // maximum level of available lifting equipment (if any)
        int max_lift;
        // maximum level of available jacking equipment (if any)
        int max_jack;

        shared_ptr_fast<ui_adaptor> create_or_get_ui_adaptor();
        void hide_ui( bool hide );

        std::unique_ptr<player_activity> serialize_activity();

        /** Format list of requirements returning true if all are met */
        bool format_reqs(
            std::string& msg, const requirement_data& reqs, const std::map<skill_id, int> &skills,
            int moves ) const;

        int part_at( tripoint_bub_ms d );
        void move_cursor( tripoint_rel_veh d, int dstart_at = 0 );
        task_reason cant_do( char mode );
        bool can_potentially_install( const vpart_info& vpart );
        /** Move index (parameter pos) according to input action:
         * (up or down, single step or whole page).
         * @param pos index to change.
         * @param action input action (taken from input_context::handle_input)
         * @param size size of the list to scroll, used to wrap the cursor around.
         * @param header number of lines reserved for list header.
         * @return false if the action is not a move action, the index is not changed in this case.
         */
        bool move_in_list( int &pos, const std::string& action, int size, int header = 0 ) const;
        void move_fuel_cursor( int delta );

        /**
         * @name Task handlers
         *
         * One function for each specific task
         * @warning presently functions may mutate local state
         * @param msg failure message to display (if any)
         */
        /*@{*/
        void do_install();
        void do_repair();
        void do_mend();
        void do_refill();
        void do_remove();
        void do_rename();
        void do_siphon();
        // Returns true if exiting the screen
        bool do_unload();
        void do_change_shape();
        void do_assign_crew();
        void do_relabel();
        /*@}*/

        // RmlUi slice 2: stat lines as colour-tagged strings (parallels
        // display_stats; the curses 3-column slot layout is dropped).
        std::vector<std::string> stats_lines() const;
        // RmlUi slice 3: one overview entry's right-column detail (parallels the
        // per-entry `details` draw lambdas in calc_overview). A member so it keeps
        // veh_interact's friend access to vehicle_part::base.
        std::string overview_detail( const vehicle_part& pt, const std::string& key ) const;
        // RmlUi slice 5: the 2D vehicle diagram as monospace colour-tagged rows
        // (parallels display_veh's w_disp glyph grid; debug CoM/pivot dropped).
        std::vector<std::string> diagram_lines() const;
        // RmlUi slice 6: the install/repair sub-mode. The subtab bar (parallels the
        // draw_subtab loop in display_list) and the selected part's detail block
        // (parallels display_details; the border + 2-column layout dropped).
        std::string install_tabs_text() const;
        std::string install_details_text( const vpart_info* part ) const;

        struct part_option {
            part_option( const std::string& key, vehicle_part* part, char hotkey )
                : key( key ),
                  part( part ),
                  hotkey( hotkey ) {}

            std::string key;
            vehicle_part *part;
            char hotkey;
        };
        std::vector<part_option> overview_opts;
        using overview_enable_t = std::function<bool( const vehicle_part& pt )>;
        using overview_action_t = std::function<void( vehicle_part& pt )>;
        overview_enable_t overview_enable;
        overview_action_t overview_action;
        int overview_pos = -1;

        void calc_overview();
        /**
         * Display overview of parts, optionally with interactive selection of one part
         *
         * @param enable used to determine parts of interest. If \p action also present, these
                         parts are the ones that can be selected. Otherwise, these are the parts
                         that will be highlighted
         * @param action callback when part is selected.
         */
        void overview( const overview_enable_t &enable = {}, const overview_action_t &action = {} );
        void move_overview_line( int );

        // Mouse callbacks for the overview pane (Step 2, mouse-interactivity
        // plan). `idx` is the row index into veh_rml_data::overview_rows as
        // seen by data-for="r : overview_rows" (it_index) — NOT an index into
        // overview_opts, since overview_rows interleaves header rows.
        void rml_on_overview( int idx );
        void rml_on_overview_hover( int idx );
        // Mouse callbacks for the install/repair sub-mode part list. `idx` is
        // the row index into veh_rml_data::install_rows, which maps 1:1 onto
        // install_info->tab_vparts (no interleaved headers there).
        void rml_on_install( int idx );
        void rml_on_install_hover( int idx );

        void count_durability();

        std::string total_durability_text;
        nc_color total_durability_color;

        /** Returns the most damaged part's index, or -1 if they're all healthy. */
        vehicle_part *get_most_damaged_part() const;

        /** Returns the index of the part that needs repair the most.
         * This may not be mostDamagedPart since not all parts can be repaired
         * If there are no damaged parts this returns -1 */
        vehicle_part *get_most_repariable_part() const;

        // do_remove supporting operation, writes requirements to ui
        bool can_remove_part( int idx, const Character& who );
        // do install support, writes requirements to ui
        bool update_part_requirements();
        // true if trying to install foot crank with electric engines for example
        // writes failure to ui
        bool is_drive_conflict();

        /* Vector of all vpart TYPES that can be mounted in the current square.
         * Can be converted to a vector<vpart_info>.
         * Updated whenever the cursor moves. */
        std::vector<const vpart_info *> can_mount;

        /* Maps part names to vparts representing different shapes of a part.
         * Used to slim down installable parts list. Only built once. */
        std::map<std::string, std::vector<const vpart_info *>> vpart_shapes;

        /* Vector of all wheel types. Used for changing wheels, so it only needs
         * to be built once. */
        std::vector<const vpart_info *> wheel_types;

        /* Vector of vparts in the current square that can be repaired. Strictly a
         * subset of parts_here.
         * Can probably be removed entirely, otherwise is a vector<vehicle_part>.
         * Updated whenever parts_here is updated.
         */
        std::vector<int> need_repair;

        /* Vector of all vparts that exist on the vehicle in the current square.
         * Can be converted to a vector<vehicle_part>.
         * Updated whenever the cursor moves. */
        std::vector<int> parts_here;

        /* Refers to the wheel (if any) in the currently selected square. */
        struct vehicle_part *wheel;

        /* called by exec() */
        void cache_tool_availability();
        void allocate_windows();
        void do_main_loop();

        void cache_tool_availability_update_lifting( const tripoint_bub_ms& world_cursor_pos );

        /** Returns true if the vehicle has a jack powerful enough to lift itself installed */
        bool can_self_jack();
};
