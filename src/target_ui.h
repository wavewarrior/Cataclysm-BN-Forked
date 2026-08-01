#pragma once
#ifndef CATA_SRC_TARGET_UI_H
#define CATA_SRC_TARGET_UI_H

#include <string>
#include <vector>

#include "coordinates.h"
#include "creature.h"
#include "cursesdef.h"
#include "enums.h"
#include "input.h"
#include "item.h"
#include "memory_fast.h"
#include "point.h"
#include "ranged.h"
#include "type_id.h"
#include "ui.h"
#include "units_angle.h"

class aim_activity_actor;
class spell;
class turret_data;
class vehicle;

class target_ui
{
    public:
        /* None of the public members (except range) should be modified during execution */

        enum class TargetMode : int {
            Fire,
            Throw,
            ThrowBlind,
            Turrets,
            TurretManual,
            Reach,
            Spell,
            Shape
        };

        // Avatar
        avatar *you;
        // Interface mode
        TargetMode mode = TargetMode::Fire;
        // Weapon being fired/thrown
        item *relevant = nullptr;
        // Cached selection range from player's position
        int range = 0;
        // Turret being manually fired
        turret_data *turret = nullptr;
        // Turrets being fired (via vehicle controls)
        const std::vector<vehicle_part *> *vturrets = nullptr;
        // Vehicle that turrets belong to
        vehicle *veh = nullptr;
        // Spell being cast
        spell *casting = nullptr;
        // Spell cannot fail
        bool no_fail = false;
        // Spell does not require mana
        bool no_mana = false;
        // Relevant activity
        aim_activity_actor *activity = nullptr;
        // Generator of AoE shapes
        std::optional<shape_factory> shape_gen;

        // Initialize UI and run the event loop
        target_handler::trajectory run();

    private:
        enum class ExitCode : int { Abort, Fire, Timeout, Reload };

        enum class Status : int {
            Good,      // All UI elements are enabled
            BadTarget, // Bad 'dst' selected; forbid aiming/firing
            OutOfAmmo, // Selected gun mode is out of ammo; forbid moving cursor,aiming and firing
            OutOfRange // Selected target is out of range of current gun mode; forbid aiming/firing
        };

        // Ui status (affects which UI controls are temporarily disabled)
        Status status = Status::Good;

        // Cached current ammo to display
        const itype *ammo = nullptr;
        // Current trajectory
        std::vector<tripoint_bub_ms> traj;
        // Aiming source (player's position)
        tripoint_bub_ms src;
        // Aiming destination (cursor position)
        // Use set_cursor_pos() to modify
        tripoint_bub_ms dst;
        // Continuous aim direction. Derived from mouse pixel (exact) or keyboard
        // tile-move (atan2 sync). Drives DDA ray for traj and dst.
        units::angle aim_angle = 0_radians;
        // Creature currently under cursor. nullptr if aiming at empty tile,
        // yourself or a creature you cannot see
        Creature *dst_critter = nullptr;
        // List of visible hostile targets
        std::vector<Creature *> targets;

        // 'true' if map has z levels and 3D fov is on
        bool allow_zlevel_shift = false;
        // Snap camera to cursor. Can be permanently toggled in settings
        // or temporarily in this window
        bool snap_to_target = false;
        // If true, LEVEL_UP, LEVEL_DOWN and directional keys
        // responsible for moving cursor will shift view instead.
        bool shifting_view = false;

        // Compact layout
        bool compact = false;
        // Tiny layout - when extremely short on space
        bool tiny = false;
        // Narrow layout - to keep in theme with
        // "compact" and "labels-narrow" sidebar styles.
        bool narrow = false;
        // Window
        catacurses::window w_target;
        // Input context
        input_context ctxt;

        /* These members are relevant for TargetMode::Fire */
        // Weapon sight dispersion
        int sight_dispersion = 0;
        // List of available weapon aim types
        std::vector<ranged::aim_type> aim_types;
        // Currently selected aim mode
        std::vector<ranged::aim_type>::iterator aim_mode;
        // 'Recoil' value the player will reach if they
        // start aiming at cursor position. Equals player's
        // 'recoil' while they are actively spending moves to aim,
        // but increases the further away the new aim point will be
        // relative to the current one.
        double predicted_recoil = 0;
        // RMB press-to-aim: true when the targeting UI was opened via RMB press
        bool opened_by_rmb = false;
        // Mouse pixel the current aim angle was committed from. Free aim resamples
        // the pointer every 50 ms TIMEOUT tick, so this doubles as the "no movement"
        // guard and as the origin of the jitter buffer below.
        point aim_commit_px = point_zero;
        // Radius in screen pixels around aim_commit_px inside which pointer movement
        // does not re-point the weapon. Zero disables the buffer.
        //
        // Why it exists: in lockstep (non-co-op) time the clock only advances while
        // do_aim() still has recoil to burn off, so a perfectly still pointer costs
        // nothing. Re-pointing for a pixel of hand-shake charges the full turning
        // penalty, pushes recoil back above min_recoil, and do_aim() spends moves
        // clawing it back — the player pays turns for standing still. The buffer is
        // measured from the committed pixel rather than the previous sample, so a
        // slow deliberate drag still gets charged once it adds up.
        int aim_jitter_px = 0;

        // Throw charge (0..1): grows over throw_charge_full_ms ms in Throw mode
        double throw_charge = 0.0;
        uint64_t throw_charge_start_ms = 0;
        static constexpr double throw_charge_full_ms = 1500.0;
        int max_throw_range = 0;

        // For AOE spells, list of tiles affected by the spell
        // relevant for TargetMode::Spell
        std::set<tripoint_bub_ms> spell_aoe;

        // For shaped attacks, we want both points and coverage
        std::map<tripoint_bub_ms, double> shape_coverage;

        // Represents a turret and a straight line from that turret to target
        struct turret_with_lof {
            vehicle_part *turret;
            std::vector<tripoint_bub_ms> line;
        };

        // List of vehicle turrets in range (out of those listed in 'vturrets')
        std::vector<turret_with_lof> turrets_in_range;

        // If true, draws turret lines
        // relevant for TargetMode::Turrets
        bool draw_turret_lines = false;

        // Create window and set up input context
        void init_window_and_input();

        // Handle input related to cursor movement.
        // Returns 'true' if action was recognized and processed.
        // 'skip_redraw' is set to 'true' if there is no need to redraw the UI.
        bool handle_cursor_movement( const std::string& action, bool& skip_redraw );

        // Set cursor position. If new position is out of range,
        // selects closest position in range.
        // Returns 'false' if cursor position did not change
        bool set_cursor_pos( const tripoint_bub_ms& new_pos );

        // Set aim angle, update traj/dst via DDA ray, update facing and critter.
        auto set_aim_angle( units::angle angle ) -> void;

        // Sync aim_angle from current dst position (for keyboard/cycle_targets paths).
        auto sync_aim_angle_from_dst() -> void;

        // Free aim: point the reticle at the current mouse pixel. Returns 'true'
        // when the aim angle actually changed, so the caller knows to redraw.
        auto track_mouse_aim() -> bool;

        // Calculate half-angle of spread cone from current dispersion.
        // Returns 0_radians when not in Fire mode or no relevant weapon.
        auto calc_spread_half_angle() const -> units::angle;

        // Called when range/ammo changes (or may have changed)
        void on_range_ammo_changed();

        // Updates 'targets' for current range
        void update_target_list();

        // Choose where to position the cursor when opening the ui
        tripoint_bub_ms choose_initial_target();

        /**
         * Try to re-acquire target for aim-and-fire.
         * @param critter whether were aiming at a critter, or a tile
         * @param new_dst where to move aim cursor (if e.g. critter moved)
         * @returns true on success
         */
        bool try_reacquire_target( bool critter, tripoint_bub_ms& new_dst );

        // Update 'status' variable
        void update_status();

        // Calculates distance from 'src'. For consistency, prefer using this over rl_dist.
        int dist_fn( const tripoint_bub_ms& p );

        // Set creature (or tile) under cursor as player's last target
        void set_last_target();

        // Prompts player to confirm attack on neutral NPC
        // Returns 'true' if attack should proceed
        bool confirm_non_enemy_target();

        // Prompts player to re-confirm an ongoing attack if
        // a non-hostile NPC / friendly creatures enters line of fire.
        // Returns 'true' if attack should proceed
        bool prompt_friendlies_in_lof();

        // List friendly creatures currently occupying line of fire.
        std::vector<weak_ptr_fast<Creature>> list_friendlies_in_lof();

        // Toggle snap-to-target
        void toggle_snap_to_target();

        // Cycle targets. 'direction' is either 1 or -1
        void cycle_targets( int direction );

        // Set new view offset. Updates map cache if necessary
        void set_view_offset( const tripoint_rel_ms& new_offset );

        // Updates 'turrets_in_range'
        void update_turrets_in_range();

        // Recalculate 'recoil' penalty. This should be called if
        // avatar's 'recoil' value has been modified
        // Relevant for TargetMode::Fire
        void recalc_aim_turning_penalty();

        // Apply penalty to avatar's 'recoil' value based on
        // how much they moved their aim point.
        // Relevant for TargetMode::Fire
        void apply_aim_turning_penalty();

        // Switch firing mode.
        void action_switch_mode();

        // Ensure we're using ranged gun mode.
        void ensure_ranged_gun_mode();

        // Update range & ammo from current gun mode
        void update_ammo_range_from_gun_mode();

        // Switch ammo. Returns 'false' if requires a reloading UI.
        bool action_switch_ammo();

        // Aim for 10 turns. Returns 'false' if ran out of moves
        bool action_aim();

        // Aim and shoot. Returns 'false' if ran out of moves
        bool action_aim_and_shoot( const std::string& action );

        // Draw UI-specific terrain overlays
        void draw_terrain_overlay();

        // Draw aiming window

        // Generate ui window title
        std::string uitext_title();

        // Generate flavor text for 'Fire!' key
        std::string uitext_fire();


        // Draw list of available controls at the bottom of the window.
        // text_y - first free line counting from the top


        // RmlUi: colour-tagged text equivalent of the old draw_ui_window — the
        // shallow sections (title/cursor/gun/recoil/spell/target/turret/controls,
        // slice 2a) plus the aim/hit-chance readout (slice 2b, via
        // ranged_chance_lines / aim_lines / throw_aim_lines). The curses panel_*
        // and print_* draw fns have been deleted.
        std::string panel_text();

        // On-selected-as-target checks that act as if they are on-hit checks.
        // `harmful` is `false` if using a non-damaging spell
        void on_target_accepted( bool harmful );
};


#endif // CATA_SRC_TARGET_UI_H
