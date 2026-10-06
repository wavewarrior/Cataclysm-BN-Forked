#include "driver_loop.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#include "action.h"
#include "avatar.h"
#include "calendar.h"
#include "coop_fiber.h"
#include "driver_items.h"
#include "driver_message_delta.h"
#include "fstream_utils.h"
#include "game.h"
#include "input.h"
#include "json.h"
#include "messages.h"
#include "path_info.h"
#include "rng.h"

namespace
{

/// Buffered line reader over a raw descriptor. `read_line` is false at EOF or on a read error.
class line_reader
{
    public:
        explicit line_reader( int fd ) : fd_( fd ) {}

        auto read_line( std::string &out ) -> bool {
            size_t nl = buf_.find( '\n' );
            while( nl == std::string::npos ) {
                char chunk[4096];
                const ssize_t n = ::read( fd_, chunk, sizeof( chunk ) );
                if( n <= 0 ) {
                    return false;
                }
                buf_.append( chunk, static_cast<size_t>( n ) );
                nl = buf_.find( '\n' );
            }
            out = buf_.substr( 0, nl );
            buf_.erase( 0, nl + 1 );
            if( !out.empty() && out.back() == '\r' ) {
                out.pop_back();
            }
            return true;
        }

    private:
        int fd_;
        std::string buf_;
};

void write_all( int fd, const std::string &s )
{
    const char *ptr = s.data();
    size_t left = s.size();
    while( left > 0 ) {
        const ssize_t w = ::write( fd, ptr, left );
        if( w <= 0 ) {
            return;
        }
        ptr += w;
        left -= static_cast<size_t>( w );
    }
}

/// `id` is empty when the request carried none that could be read.
void begin_response( JsonOut &jo, const std::optional<int> &id, std::string_view status )
{
    jo.start_object();
    jo.member( "id" );
    if( id ) {
        jo.write( *id );
    } else {
        jo.write_null();
    }
    jo.member( "status", std::string( status ) );
}

auto error_line( const std::optional<int> &id, const std::string &why ) -> std::string
{
    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "error" );
    jo.member( "error", why );
    jo.end_object();
    return os.str() + "\n";
}

auto ping_line( int id ) -> std::string
{
    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "ok" );
    jo.member( "ready", true );
    jo.end_object();
    return os.str() + "\n";
}

auto quit_line( int id ) -> std::string
{
    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "ok" );
    jo.end_object();
    return os.str() + "\n";
}

/// Newest messages compared before and after a request, and the caps on what one response
/// carries (a response must stay within about 1.5K tokens).
constexpr size_t message_window = 64;
constexpr size_t max_new_messages = 10;
constexpr size_t max_message_bytes = 240;
/// Hard cap on the turns one request may run. A request that reaches it is interrupted.
constexpr int max_turns_per_request = 1000;

/// What the world looked like before a request began, for the fields that are deltas.
struct snapshot {
    tripoint_abs_ms pos;
    time_point turn;
    std::vector<log_entry> messages;
};

/// A screen the avatar opened that now holds the game's input until the agent answers it.
struct open_modal {
    /// The raw action that opened it, which is what the response calls the prompt.
    std::string prompt;
    /// The avatar's moves when it opened, to tell whether answering it spent any.
    int moves_before = 0;
};

/// The screen currently waiting for a key, if any. Only one can be open: the game runs a
/// single modal fiber.
std::optional<open_modal> modal;

/// What a request did, as far as the driver can tell without looking at the world again.
struct action_result {
    bool time_passed = false;
    std::string_view outcome = "completed";
    /// Only meaningful for outcomes `interrupted` (why) and `unsupported` (which guard).
    std::string_view reason;
    /// Free text that explains `reason`, such as the deny-list entry's own note.
    std::string detail;
    /// Name of the screen waiting for a key; empty when none is.
    std::string_view prompt;
};

/// How a request reads while a screen waits for a key.
auto modal_result( bool time_passed = false ) -> action_result
{
    action_result result = { .time_passed = time_passed };
    if( modal ) {
        result.outcome = "awaiting_input";
        result.prompt = modal->prompt;
    }
    return result;
}

auto log_window() -> std::vector<log_entry>
{
    const auto recent = Messages::recent_messages_rich( message_window );
    std::vector<log_entry> entries;
    entries.reserve( recent.size() );
    for( const Messages::rich_message &message : recent ) {
        entries.push_back( { .seq = message.seq, .text = message.text } );
    }
    return entries;
}

auto take_snapshot() -> snapshot
{
    return { .pos = get_avatar().abs_pos(), .turn = calendar::turn, .messages = log_window() };
}

/// Observation response: the contract's common payload. Reads the world, never advances it.
/// `payload` adds members of its own to the response (a query's answer) and says whether it had
/// to cut something to stay within the size ceiling.
auto observation_line( int id, const snapshot &before, const action_result &result,
                       const std::function<bool( JsonOut & )> &payload = nullptr ) -> std::string
{
    const avatar &u = get_avatar();
    message_delta delta = compute_message_delta( before.messages, log_window() );
    const bool capped = cap_messages( delta.fresh, max_new_messages, max_message_bytes );
    bool truncated = delta.lost || capped;
    // Death ends the Episode, so it outranks whatever the action itself reported.
    const bool dead = u.is_dead_state();

    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "ok" );
    jo.member( "boundary", std::string( !dead && result.outcome == "awaiting_input" ? "needs_input" :
                                        "turn_complete" ) );
    jo.member( "turn", to_turn<int>( calendar::turn ) );
    jo.member( "time_passed", result.time_passed );
    jo.member( "moved", u.abs_pos() != before.pos );
    jo.member( "new_messages" );
    jo.start_array();
    for( const std::string &message : delta.fresh ) {
        jo.write( message );
    }
    jo.end_array();
    jo.member( "prompt" );
    if( result.prompt.empty() ) {
        jo.write_null();
    } else {
        jo.write( std::string( result.prompt ) );
    }
    jo.member( "hp", u.hp_percentage() );
    jo.member( "pain", u.get_pain() );
    jo.member( "stamina", u.get_stamina() );
    jo.member( "hunger", u.get_stored_kcal() );
    jo.member( "thirst", u.get_thirst() );
    if( payload ) {
        truncated |= payload( jo );
    }
    jo.member( "outcome", std::string( dead ? "died" : result.outcome ) );
    if( !dead && ( result.outcome == "interrupted" || result.outcome == "unsupported" ) ) {
        jo.member( "reason", std::string( result.reason ) );
    }
    if( !dead && !result.detail.empty() ) {
        jo.member( "detail", result.detail );
    }
    if( truncated ) {
        jo.member( "truncated", true );
    }
    jo.end_object();
    return os.str() + "\n";
}

auto state_line( int id ) -> std::string
{
    return observation_line( id, take_snapshot(), modal_result() );
}

auto seed_line( int id, unsigned int seed ) -> std::string
{
    rng_set_engine_seed( seed );
    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "ok" );
    jo.member( "seed", seed );
    jo.end_object();
    return os.str() + "\n";
}

/// Runs world steps until the avatar has moves again, at most `turn_budget` of them.
/// Whatever is left of the avatar's turn is forfeited first, as the co-op host does for a
/// client that queued no further action.
void advance_to_turn_boundary( int &turn_budget )
{
    avatar &u = get_avatar();
    u.moves = std::min( u.moves, 0 );
    while( u.moves <= 0 && turn_budget > 0 && !u.is_dead_state() ) {
        g->post_action_world_step();
        --turn_budget;
    }
}

/// How one action went.
struct step_report {
    /// The action spent moves; only then does the world advance.
    bool spent = false;
    /// World steps run, including one that finished a turn that was already under way.
    int turns = 0;
    /// The game's safe mode was stopping movement, so a rejection is the game's, not a wall's.
    bool safe_mode_stopped = false;
};

/// Starts the modal fiber the action queued, if any, and runs it to its first wait for a key.
/// The first resume only starts the fiber, so it carries no key. Returns true, and records the
/// screen, while it waits; a screen that finished without asking is simply gone.
auto prime_modal( const std::string &action ) -> bool
{
    const bool queued = g->modal_fiber_.has_value();
    if( !queued ) {
        return false;
    }
    g->modal_fiber_->resume( input_event() );
    if( g->modal_fiber_->done() ) {
        g->modal_fiber_.reset();
        return false;
    }
    modal = open_modal{ .prompt = action, .moves_before = get_avatar().moves };
    return true;
}

/// A world saved mid-turn leaves the avatar without moves: the first action completes
/// that partial turn before it can act.
auto complete_partial_turn( int &turn_budget ) -> void
{
    avatar &u = get_avatar();
    while( u.moves <= 0 && turn_budget > 0 && !u.is_dead_state() ) {
        g->post_action_world_step();
        --turn_budget;
    }
}

/// Hands one action to the game as if its key was pressed, then lets the world respond.
/// An action that spends no moves (a blocked move, a cancelled menu) does not advance the world,
/// and one that opens a screen waits for its answer first: see `modal`.
auto perform_action( const std::string &action, int &turn_budget ) -> step_report
{
    avatar &u = get_avatar();
    const int budget_before = turn_budget;
    complete_partial_turn( turn_budget );
    step_report report;
    report.safe_mode_stopped = g->safe_mode == SAFE_MODE_STOP;
    if( u.moves > 0 && !u.is_dead_state() ) {
        const int moves_before = u.moves;
        g->handle_action_from( action );
        report.spent = u.moves < moves_before;
        // A screen that waits for its answer holds the world still; its answer decides the rest.
        if( !prime_modal( action ) && report.spent ) {
            advance_to_turn_boundary( turn_budget );
        }
    }
    report.turns = budget_before - turn_budget;
    return report;
}

/// An item command, run on the avatar directly: the game's own checks and costs, no menu. What
/// it spent in moves lets the world advance, as after any other action. A rejection costs
/// nothing and carries the game's message.
auto run_item( driver_items::command kind, const safe_reference<item> &target,
               const snapshot &before ) -> action_result
{
    avatar &u = get_avatar();
    int budget = max_turns_per_request;
    complete_partial_turn( budget );
    driver_items::command_result done = { .outcome = "refused", .detail = "the avatar cannot act" };
    const int moves_before = u.moves;
    if( u.moves > 0 && !u.is_dead_state() ) {
        done = driver_items::run_command( kind, target );
    }
    const bool spent = u.moves < moves_before;
    if( spent ) {
        advance_to_turn_boundary( budget );
    }
    action_result result = { .time_passed = spent || budget < max_turns_per_request,
                             .outcome = done.outcome, .reason = done.reason,
                             .detail = std::move( done.detail )
                           };
    if( result.outcome == "refused" && result.detail.empty() ) {
        // The game rejected it with a message of its own: report that one.
        const std::vector<std::string> said = compute_message_delta( before.messages, log_window() ).fresh;
        result.detail = said.empty() ? "the game would not do that" : said.back();
    }
    return result;
}

/// `dir` is a compass point or `up`/`down`; empty when it is none of them.
auto move_action_name( const std::string &dir ) -> std::string
{
    static const std::vector<std::string> known = { "n", "ne", "e", "se", "s", "sw", "w", "nw", "up", "down" };
    return std::ranges::find( known, dir ) == known.end() ? std::string() : "move_" + dir;
}

/// The item command a request names, if it names one.
auto item_command_named( const std::string &name ) -> std::optional<driver_items::command>
{
    using driver_items::command;
    static const std::map<std::string, command> known = {
        { "pickup", command::pickup }, { "drop", command::drop }, { "wield", command::wield },
        { "wear", command::wear }, { "take_off", command::take_off },
    };
    const auto found = known.find( name );
    return found == known.end() ? std::nullopt : std::make_optional( found->second );
}

auto run_move( const std::string &action, const snapshot &before ) -> action_result
{
    int budget = max_turns_per_request;
    const step_report step = perform_action( action, budget );
    action_result result;
    result.time_passed = step.spent || step.turns > 0;
    if( modal ) {
        return modal_result( result.time_passed );
    }
    if( !step.spent && get_avatar().abs_pos() == before.pos ) {
        // Nothing was spent and nothing moved: the game said no, or the world did.
        result.outcome = step.safe_mode_stopped ? "refused" : "blocked";
    }
    return result;
}

auto run_wait( int turns ) -> action_result
{
    int budget = max_turns_per_request;
    action_result result;
    for( int done = 0; done < turns; ++done ) {
        if( budget <= 0 ) {
            result.outcome = "interrupted";
            result.reason = "turn_cap";
            break;
        }
        const step_report step = perform_action( "pause", budget );
        result.time_passed = result.time_passed || step.spent || step.turns > 0;
        if( modal ) {
            return modal_result( result.time_passed );
        }
        if( get_avatar().is_dead_state() ) {
            break;
        }
        if( !step.spent ) {
            if( done == 0 ) {
                result.outcome = step.safe_mode_stopped ? "refused" : "no_effect";
            } else {
                result.outcome = "interrupted";
                result.reason = step.safe_mode_stopped ? "monster_in_view" : "other";
            }
            break;
        }
    }
    return result;
}

/// A whole-number member of `jo` within [`min`, `max`]; empty when it is fractional or out of
/// range. Throws, like any bad request, when the member is missing or not a number.
auto whole_number( const JsonObject &jo, const std::string &name, int64_t min,
                   int64_t max ) -> std::optional<int64_t>
{
    // get_int would silently truncate 1.5, so read the number as a float and check it.
    const double value = jo.get_float( name );
    if( value != std::floor( value ) || value < static_cast<double>( min ) ||
        value > static_cast<double>( max ) ) {
        return std::nullopt;
    }
    return static_cast<int64_t>( value );
}

/// Actions the driver refuses, each with the note that says why. Loaded once at start.
std::map<std::string, std::string> deny_list;

/// Where the deny list lives under the data directory when no other file is named.
const std::string default_deny_list_name = "driver_deny_list.json";

/// True once the driver serves requests: the input layer's guard keys off it.
bool driver_serving = false;

void parse_deny_list( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    JsonArray entries = jo.get_array( "deny" );
    for( size_t i = 0; i < entries.size(); ++i ) {
        JsonObject entry = entries.get_object( i );
        deny_list[entry.get_string( "action" )] = entry.get_string( "why", "" );
    }
}

/// Fills `deny_list` from the data file; false when it cannot be read. A driver that does not
/// know what hangs it must not start.
auto load_deny_list( const std::string &path ) -> bool
{
    deny_list.clear();
    return read_from_file_json( path, parse_deny_list );
}

/// Runs `run`, and turns a read that would have blocked the game into outcome `unsupported`
/// instead of a hang. Whatever the aborted action left half-open is dropped.
auto guarded( const snapshot &before, const std::function<action_result()> &run ) -> action_result
{
    const int timeout = inp_mngr.get_timeout();
    try {
        return run();
    } catch( const driver_blocking_read &err ) {
        // handle_input restores the read timeout only when it returns, not when it unwinds.
        inp_mngr.set_timeout( timeout );
        g->modal_fiber_.reset();
        modal.reset();
        return { .time_passed = calendar::turn != before.turn, .outcome = "unsupported",
                 .reason = "blocking_read", .detail = err.what() };
    }
}

/// A raw action, as if its key had been pressed: refused if listed, otherwise handed to the
/// game, which may open a screen that waits for `key`.
auto run_action( const std::string &action, const snapshot &before ) -> action_result
{
    if( const auto denied = deny_list.find( action ); denied != deny_list.end() ) {
        return { .outcome = "unsupported", .reason = "deny_list", .detail = denied->second };
    }
    int budget = max_turns_per_request;
    const step_report step = perform_action( action, budget );
    const bool time_passed = step.spent || step.turns > 0;
    if( modal ) {
        return modal_result( time_passed );
    }
    // Anything the player could see change counts; only a silent nothing is `no_effect`.
    const bool changed = time_passed || get_avatar().abs_pos() != before.pos ||
                         !compute_message_delta( before.messages, log_window() ).fresh.empty();
    return { .time_passed = time_passed, .outcome = changed ? "completed" : "no_effect" };
}

/// The key a request names: one printable character, or a key name such as `ESC` or `RETURN`.
/// Empty when it names no key.
auto key_event( const std::string &key ) -> std::optional<input_event>
{
    const bool printable = key.size() == 1 && key[0] >= ' ' && key[0] <= '~';
    const int code = printable ? key[0] : key.empty() ? 0 : inp_mngr.get_keycode( key );
    if( code <= 0 ) {
        return std::nullopt;
    }
    input_event evt( code, input_event_t::keyboard );
    if( printable ) {
        evt.text = key;
    }
    return evt;
}

/// Answers the open screen with one key. When the screen closes, whatever it spent in moves
/// lets the world advance, as it would after any other action.
auto run_key( const input_event &evt, const snapshot &before ) -> action_result
{
    const int moves_before = modal->moves_before;
    g->modal_fiber_->resume( evt );
    if( !g->modal_fiber_->done() ) {
        return modal_result( calendar::turn != before.turn );
    }
    g->modal_fiber_.reset();
    modal.reset();
    int budget = max_turns_per_request;
    const bool spent = get_avatar().moves < moves_before;
    if( spent ) {
        advance_to_turn_boundary( budget );
    }
    return { .time_passed = spent || budget < max_turns_per_request };
}

} // namespace

auto run_driver_loop( int fd, const std::string &deny_list_path ) -> bool
{
    const std::string path = deny_list_path.empty() ? PATH_INFO::datadir() + default_deny_list_name :
                             deny_list_path;
    const bool loaded = load_deny_list( path );
    if( !loaded ) {
        std::cerr << "driver: cannot load the deny list " << path << "\n";
        return false;
    }
    driver_serving = true;
    line_reader in( fd );
    std::string line;
    while( in.read_line( line ) ) {
        if( line.empty() ) {
            continue;
        }
        std::optional<int> id;
        try {
            std::istringstream ss( line );
            JsonIn jsin( ss );
            JsonObject jo = jsin.get_object();
            jo.allow_omitted_members();
            id = jo.get_int( "id" );
            const std::string cmd = jo.get_string( "cmd" );
            const std::optional<driver_items::command> item_kind = item_command_named( cmd );
            if( modal && ( cmd == "move" || cmd == "wait" || cmd == "action" || item_kind ) ) {
                write_all( fd, error_line( id, "a menu is open (prompt '" + modal->prompt +
                                           "'): answer it with key" ) );
                continue;
            }
            if( cmd == "ping" ) {
                write_all( fd, ping_line( *id ) );
            } else if( cmd == "state" ) {
                write_all( fd, state_line( *id ) );
            } else if( cmd == "move" ) {
                const std::string action = move_action_name( jo.get_string( "dir" ) );
                if( action.empty() ) {
                    write_all( fd, error_line( id, "unknown direction" ) );
                    continue;
                }
                const snapshot before = take_snapshot();
                write_all( fd, observation_line( *id, before, guarded( before, [&]() {
                    return run_move( action, before );
                } ) ) );
            } else if( cmd == "wait" ) {
                const std::optional<int64_t> turns = whole_number( jo, "turns", 1,
                                                     std::numeric_limits<int>::max() );
                if( !turns ) {
                    write_all( fd, error_line( id, "turns must be a whole number, at least 1" ) );
                    continue;
                }
                const snapshot before = take_snapshot();
                write_all( fd, observation_line( *id, before, guarded( before, [&]() {
                    return run_wait( static_cast<int>( *turns ) );
                } ) ) );
            } else if( cmd == "action" ) {
                const std::string name = jo.get_string( "name" );
                if( look_up_action( name ) == ACTION_NULL ) {
                    write_all( fd, error_line( id, "unknown action '" + name + "'" ) );
                    continue;
                }
                const snapshot before = take_snapshot();
                write_all( fd, observation_line( *id, before, guarded( before, [&]() {
                    return run_action( name, before );
                } ) ) );
            } else if( cmd == "key" ) {
                const std::string key = jo.get_string( "key" );
                if( !modal ) {
                    write_all( fd, error_line( id, "no menu is open: key answers an open menu" ) );
                    continue;
                }
                const std::optional<input_event> evt = key_event( key );
                if( !evt ) {
                    write_all( fd, error_line( id, "unknown key '" + key + "'" ) );
                    continue;
                }
                const snapshot before = take_snapshot();
                write_all( fd, observation_line( *id, before, guarded( before, [&]() {
                    return run_key( *evt, before );
                } ) ) );
            } else if( cmd == "query" ) {
                const std::string topic = jo.get_string( "topic" );
                if( !driver_items::is_query_topic( topic ) ) {
                    write_all( fd, error_line( id, "unknown topic '" + topic +
                                               "': query takes inventory or effects" ) );
                    continue;
                }
                write_all( fd, observation_line( *id, take_snapshot(), modal_result(),
                [&]( JsonOut & out ) {
                    return driver_items::write_query( out, topic );
                } ) );
            } else if( item_kind ) {
                const driver_items::found_item found = driver_items::find_item( jo.get_string( "item" ) );
                if( !found.error.empty() ) {
                    write_all( fd, error_line( id, found.error ) );
                    continue;
                }
                const snapshot before = take_snapshot();
                write_all( fd, observation_line( *id, before, guarded( before, [&]() {
                    return run_item( *item_kind, found.ref, before );
                } ) ) );
            } else if( cmd == "seed" ) {
                const std::optional<int64_t> seed = whole_number( jo, "seed", 0,
                                                    std::numeric_limits<unsigned int>::max() );
                if( !seed ) {
                    write_all( fd, error_line( id, "seed must be a whole number from 0 to 4294967295" ) );
                    continue;
                }
                write_all( fd, seed_line( *id, static_cast<unsigned int>( *seed ) ) );
            } else if( cmd == "quit" ) {
                write_all( fd, quit_line( *id ) );
                break;
            } else {
                write_all( fd, error_line( id, "unknown cmd '" + cmd + "'" ) );
            }
        } catch( const std::exception &err ) {
            write_all( fd, error_line( id, std::string( "bad request: " ) + err.what() ) );
        }
    }
    driver_serving = false;
    return true;
}

auto driver_mode_active() -> bool
{
    return driver_serving;
}

driver_blocking_read::driver_blocking_read()
    : std::runtime_error( "the game waited for a key with no menu to answer it" )
{
}
