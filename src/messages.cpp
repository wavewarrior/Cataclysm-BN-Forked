#include "messages.h"
#include "message_types.h"
#include "calendar.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "enums.h"
#include "game.h"
#include "ime.h"
#include "input.h"
#include "json.h"
#include "output.h"
#include "point.h"
#include "sdl_wrappers.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "ui_manager.h"
#include "uistate.h"

#include "options.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"

#include <algorithm>
#include <deque>
#include <iterator>
#include <memory>
#include <ranges>
#include <map>
#include <set>

namespace
{

struct game_message : public JsonDeserializer, public JsonSerializer {
    std::string       message;
    time_point timestamp_in_turns  = calendar::start_of_cataclysm;
    int               timestamp_in_user_actions = 0;
    int               count = 1;
    // number of times this message has been seen while it was in cooldown.
    unsigned cooldown_seen = 1;
    // hide the message, because at some point it was in cooldown period.
    bool cooldown_hidden = false;
    game_message_type type  = m_neutral;
    // Session-monotonic sequence number for stable identity (NOT serialized).
    unsigned seq = 0;

    game_message() = default;
    game_message( std::string &&msg, game_message_type const t ) :
        message( std::move( msg ) ),
        timestamp_in_turns( calendar::turn ),
        timestamp_in_user_actions( g->get_user_action_counter() ),
        type( t ) {
    }

    const time_point &turn() const {
        return timestamp_in_turns;
    }

    std::string get_with_count() const {
        if( count <= 1 ) {
            return message;
        }
        //~ Message %s on the message log was repeated %d times, e.g. "You hear a whack! x 12"
        return string_format( _( "%s x %d" ), message, count );
    }

    /** Get whether or not a message should not be displayed (hidden) in the side bar because it's in a cooldown period.
     * @returns `true` if the message should **not** be displayed, `false` otherwise.
     */
    bool is_in_cooldown() const {
        return cooldown_hidden;
    }

    bool is_new( const time_point &current ) const {
        return turn() >= current;
    }

    bool is_recent( const time_point &current ) const {
        return turn() + 5_turns >= current;
    }

    nc_color get_color( const time_point &current ) const {
        if( is_new( current ) ) {
        // color for new messages
        return msgtype_to_color( type, false );

        } else if( is_recent( current ) ) {
        // color for slightly old messages
        return msgtype_to_color( type, true );
        }

        // color for old messages
        return c_dark_gray;
    }

    void deserialize( JsonIn &jsin ) override {
        JsonObject obj = jsin.get_object();
        obj.read( "turn", timestamp_in_turns );
        message = obj.get_string( "message" );
        count = obj.get_int( "count" );
        type = static_cast<game_message_type>( obj.get_int( "type" ) );
    }

    void serialize( JsonOut &jsout ) const override {
        jsout.start_object();
        jsout.member( "turn", timestamp_in_turns );
        jsout.member( "message", message );
        jsout.member( "count", count );
        jsout.member( "type", static_cast<int>( type ) );
        jsout.end_object();
    }
};
using Messages::rich_message;

class messages_impl
{
    public:
        std::deque<game_message> messages;   // Messages to be printed
        std::vector<game_message> cooldown_templates; // Message cooldown
        time_point curmes = calendar::turn_zero; // The last-seen message.
        bool active = true;
        unsigned next_seq = 1;

        bool has_undisplayed_messages() const {
            return !messages.empty() && messages.back().turn() > curmes;
        }

        const game_message &history( const int i ) const {
            return messages[messages.size() - i - 1];
        }

        // coalesce recent like messages
        bool coalesce_messages( const game_message &m ) {
            if( messages.empty() ) {
                return false;
            }

            auto &last_msg = messages.back();
            if( last_msg.turn() + 3_turns < calendar::turn ) {
                return false;
            }

            if( m.type != last_msg.type || m.message != last_msg.message ) {
                return false;
            }

            // update the cooldown message timer due to coalescing
            const auto cooldown_it = std::ranges::find_if( cooldown_templates,
            [&m]( game_message & am ) -> bool {
                return m.message == am.message;
            } );
            if( cooldown_it != cooldown_templates.end() ) {
                cooldown_it->timestamp_in_turns = calendar::turn;
            }

            // coalesce messages
            last_msg.count++;
            last_msg.timestamp_in_turns = calendar::turn;
            last_msg.timestamp_in_user_actions = g->get_user_action_counter();
            last_msg.type = m.type;

            return true;
        }

        void add_msg_string( std::string &&msg ) {
            add_msg_string( std::move( msg ), m_neutral, gmf_none );
        }

        void add_msg_string( std::string &&msg, const game_message_params &params ) {
            add_msg_string( std::move( msg ), params.type, params.flags );
        }

        void add_msg_string( std::string &&msg, game_message_type const type,
                             const game_message_flags flags ) {
            if( msg.empty() || !active ) {
                return;
            }

            if( type == m_debug && !debug_mode ) {
                return;
            }

            if( type == m_debug ) {
                DebugLog( DL::Info, DC::DebugModeMsg ) << msg;
            }

            game_message m = game_message( std::move( msg ), type );

            refresh_cooldown( m, flags );
            hide_message_in_cooldown( m );

            if( coalesce_messages( m ) ) {
                return;
            }

            unsigned int message_limit = get_option<int>( "MESSAGE_LIMIT" );
            while( messages.size() > message_limit ) {
                messages.pop_front();
            }

            m.seq = next_seq++;
            messages.emplace_back( m );
        }

        /** Check if the current message needs to be prevented (hidden) or not from being displayed in the side bar.
         * @param message The message to be checked.
         */
        void hide_message_in_cooldown( game_message &message ) {
            message.cooldown_hidden = false;

            if( message_cooldown <= 0 || message.turn() <= calendar::start_of_cataclysm ) {
                return;
            }

            // We look for **exactly the same** message string in the cooldown templates
            // If there is one, this means the same message was already displayed.
            const auto cooldown_it = std::ranges::find_if( cooldown_templates,
            [&message]( game_message & m_cooldown ) -> bool {
                return m_cooldown.message == message.message;
            } );
            if( cooldown_it == cooldown_templates.end() ) {
                // nothing found, not in cooldown.
                return;
            }

            // note: from this point the current message (`message`) has the same string than one of the active cooldown template messages (`cooldown_it`).

            // check how much times this message has been seen during its cooldown.
            // If it's only one time, then no need to hide it.
            if( cooldown_it->cooldown_seen == 1 ) {
                return;
            }

            // check if it's the message that started the cooldown timer.
            if( message.turn() == cooldown_it->turn() ) {
                return;
            }

            // current message turn.
            const auto cm_turn = to_turn<int>( message.turn() );
            // maximum range of the cooldown timer.
            const auto max_cooldown_range = to_turn<int>( cooldown_it->turn() ) + message_cooldown;
            // If the current message is in the cooldown range then hide it.
            if( cm_turn <= max_cooldown_range ) {
                message.cooldown_hidden = true;
            }
        }

        std::vector<std::pair<std::string, std::string>> recent_messages( size_t count ) const {
            count = std::min( count, messages.size() );

            std::vector<std::pair<std::string, std::string>> result;
            result.reserve( count );

            const int offset = static_cast<std::ptrdiff_t>( messages.size() - count );

            std::transform( begin( messages ) + offset, end( messages ), back_inserter( result ),
            []( const game_message & msg ) {
                return std::make_pair( to_string_time_of_day( msg.timestamp_in_turns ),
                                       msg.get_with_count() );
            } );

            return result;
        }
        // Same slice as recent_messages(), but the text carries a colorize() tag using
        // the same new/recent/old fade get_color() applies to the curses draw — the
        // RmlUi sidebar HUD's "Log" panel needs that colour, recent_messages() doesn't.
        std::vector<std::pair<std::string, std::string>> recent_messages_colored(
            size_t count ) const {
            count = std::min( count, messages.size() );

            std::vector<std::pair<std::string, std::string>> result;
            result.reserve( count );

            const int offset = static_cast<std::ptrdiff_t>( messages.size() - count );

            std::transform( begin( messages ) + offset, end( messages ), back_inserter( result ),
            []( const game_message & msg ) {
                return std::make_pair( to_string_time_of_day( msg.timestamp_in_turns ),
                                       colorize( msg.get_with_count(), msg.get_color( calendar::turn ) ) );
            } );

            return result;
        }

        /// Rich message accessor for the animated log. Returns structured data
        /// (time, text, type, color, seq) without colorize tags - the consumer
        /// formats per-message RML with symbols and attributes.
        std::vector<rich_message> recent_messages_rich( size_t count ) const {
            count = std::min( count, messages.size() );

            std::vector<rich_message> result;
            result.reserve( count );

            const int offset = static_cast<std::ptrdiff_t>( messages.size() - count );

            std::transform( begin( messages ) + offset, end( messages ), back_inserter( result ),
            []( const game_message & msg ) {
                rich_message rm;
                rm.time = to_string_time_of_day( msg.timestamp_in_turns );
                rm.text = msg.get_with_count();
                rm.type = msg.type;
                rm.color = msg.get_color( calendar::turn );
                rm.seq = msg.seq;
                return rm;
            } );

            return result;
        }

        /** Refresh the cooldown timers, removing elapsed ones and making new ones if needed.
         * @param message The current message that needs to be checked.
         * @param flags Flags pertaining to the message.
         */
        void refresh_cooldown( const game_message &message, const game_message_flags flags ) {
            // is cooldown used? (also checks for messages arriving here at game initialization: we don't care about them).
            if( message_cooldown <= 0 || message.turn() <= calendar::start_of_cataclysm ) {
                return;
            }

            // housekeeping: remove any cooldown message with an expired cooldown time from the cooldown queue.
            const auto now = calendar::turn;
            for( auto it = cooldown_templates.begin(); it != cooldown_templates.end(); ) {
                // number of turns elapsed since the cooldown started.
                const auto turns = to_turns<int>( now - it->turn() );
                if( turns >= message_cooldown ) {
                    // time elapsed! remove it.
                    it = cooldown_templates.erase( it );
                } else {
                    ++it;
                }
            }

            // do not hide messages which bypasses cooldown.
            if( ( flags & gmf_bypass_cooldown ) != 0 ) {
                return;
            }

            // Is the message string already in the cooldown queue?
            // If it's not we must put it in the cooldown queue now, otherwise just increment the number of times we have seen it.
            const auto cooldown_message_it = std::ranges::find_if( cooldown_templates,
            [&message]( game_message & cooldown_message ) -> bool {
                return cooldown_message.message == message.message;
            } );
            if( cooldown_message_it == cooldown_templates.end() ) {
                // push current message to cooldown message templates.
                cooldown_templates.emplace_back( message );
            } else {
                // increment the number of time we have seen this message.
                cooldown_message_it->cooldown_seen++;
            }
        }
};

// Messages object.
messages_impl player_messages;

} //namespace

std::vector<std::pair<std::string, std::string>> Messages::recent_messages( const size_t count )
{
    return player_messages.recent_messages( count );
}

std::vector<std::pair<std::string, std::string>> Messages::recent_messages_colored(
    const size_t count )
{
    return player_messages.recent_messages_colored( count );
}

auto Messages::recent_messages_rich( const size_t count ) -> std::vector<rich_message>
{
    return player_messages.recent_messages_rich( count );
}

void Messages::serialize( JsonOut &json )
{
    json.member( "player_messages" );
    json.start_object();
    json.member( "messages", player_messages.messages );
    json.member( "curmes", player_messages.curmes );
    json.end_object();
}

void Messages::deserialize( const JsonObject &json )
{
    if( !json.has_member( "player_messages" ) ) {
        return;
    }

    JsonObject obj = json.get_object( "player_messages" );
    obj.read( "messages", player_messages.messages );
    obj.read( "curmes", player_messages.curmes );
    // Reloaded messages get fresh sequence numbers.
    for( game_message &msg : player_messages.messages ) {
        msg.seq = player_messages.next_seq++;
    }
}

void Messages::add_msg( std::string msg )
{
    player_messages.add_msg_string( std::move( msg ) );
}

void Messages::add_msg( const game_message_params &params, std::string msg )
{
    player_messages.add_msg_string( std::move( msg ), params );
}

void Messages::clear_messages()
{
    player_messages.messages.clear();
    player_messages.active = true;
}

void Messages::deactivate()
{
    player_messages.active = false;
}

size_t Messages::size()
{
    return player_messages.messages.size();
}

bool Messages::has_undisplayed_messages()
{
    return player_messages.has_undisplayed_messages();
}
namespace Messages
{

// ── RmlUi render path (full UI→RmlUi migration, §8.1 gate-blocker backlog) ────
// The full message-LOG screen (display_messages, the ESC log). A scrolling text
// pane: one row per folded message line — a right-aligned time column (shown
// only when the time string changes; the curses ASCII time-range bracket glyphs
// are DROPPED, semantic rewrite à la diary's border) + the message text coloured
// by msgtype via cata_text_to_rml(colorize(...)). Native RmlUi scroll replaces
// the curses offset windowing (UP/DOWN/PAGE_* scroll the pane). The transient
// FILTER input + help overlay is left as the legacy string_input_popup
// (Tier-0) compositing on top — like diary's nested editor — so only the log
// pane itself moves off curses here.
namespace
{
struct messages_rml_row {
    Rml::String time_rml;
    Rml::String text_rml;
};
struct messages_rml_data {
    Rml::Vector<messages_rml_row> rows;
    Rml::String footer_rml;
    Rml::DataModelHandle handle;
};

// Data-model for the filter-help backdrop (one colour-tagged help string). The
// syntax help lists the registered message types, so it is bound (not literal in
// the .rml like the static autopickup/safemode helps).
struct messages_filter_help_data {
    Rml::String help_rml;
    Rml::DataModelHandle handle;
};

bool g_messages_types_registered = false;

void register_messages_rml_types( Rml::DataModelConstructor &c )
{
    if( g_messages_types_registered ) {
        return;
    }
    Rml::StructHandle<messages_rml_row> rh = c.RegisterStruct<messages_rml_row>();
    rh.RegisterMember( "time_rml", &messages_rml_row::time_rml );
    rh.RegisterMember( "text_rml", &messages_rml_row::text_rml );
    c.RegisterArray<Rml::Vector<messages_rml_row>>();
    g_messages_types_registered = true;
}
} // namespace
} // namespace Messages

bool &messages_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

namespace Messages
{

// NOLINTNEXTLINE(cata-xy)
class dialog
{
    public:
        dialog();
        void run();
    private:
        void init_first_time();
        void init( ui_adaptor &ui );
        void show();
        void input( const ui_adaptor &ui );
        void do_filter( const std::string &filter_str );
        void set_size();
        static std::vector<std::string> filter_help_text( int width );

        // RmlUi render path (see the file note above).
        void sync_rml();
        void rml_scroll( int dir );
        // Open the filter-help backdrop doc (lazily, while filtering).
        void open_filter_help_rml();

        const nc_color border_color;
        const nc_color filter_color;
        const nc_color time_color;
        const nc_color bracket_color;

        // border_width padding_width         border_width
        //      v           v                     v
        //
        //      | 12 seconds Never mind. x 2      |
        //
        //       '-----v---' '---------v---------'
        //        time_width       msg_width
        static constexpr int border_width = 1;
        static constexpr int padding_width = 1;
        int time_width = 0;
        int msg_width = 0;

        size_t max_lines = 0; // Max number of lines the window can show at once

        int w_x = 0;
        int w_y = 0;
        int w_width = 0;
        int w_height = 0; // Main window position
        catacurses::window w; // Main window

        int w_fh_x = 0;
        int w_fh_y = 0;
        int w_fh_width = 0;
        int w_fh_height = 0; // Filter help window position
        catacurses::window w_filter_help; // Filter help window

        std::vector<std::string> help_text; // Folded filter help text

        string_input_popup filter;
        bool filtering = false;
        std::string filter_str;

        input_context ctxt;

        // Message indices and folded strings
        std::vector<std::pair<size_t, std::string>> folded_all;
        // Indices of filtered messages
        std::vector<size_t> folded_filtered;

        size_t offset = 0; // Index of the first printed message

        bool canceled = false;
        bool errored = false;

        std::optional<ime_sentry> filter_sentry;

        bool first_init = true;

        // RmlUi render path (the file note above). `rml` is the F.3 harness doc;
        // `rml_data` holds the bound model (declared after the curses members so
        // it tears down first). `rml_initial_scroll_frames` counts down a few
        // frames after open to scroll the pane to the newest end (matching the
        // curses initial offset) once RmlUi has laid the rows out.
        rml_doc rml;
        std::unique_ptr<messages_rml_data> rml_data;
        int rml_initial_scroll_frames = 0;

        // Filter-help backdrop (data declared before the doc so it outlives it,
        // per rml_screen.h). Opened only while `filtering` and the log doc is RML.
        std::unique_ptr<messages_filter_help_data> filter_help_data;
        rml_doc filter_help_rml;
};
} // namespace Messages

Messages::dialog::dialog()
    : border_color( BORDER_COLOR ), filter_color( c_white ),
      time_color( c_light_blue ), bracket_color( c_dark_gray )
{
}

inline void Messages::dialog::set_size()
{
    w_width
        = std::min( TERMX, static_cast<int>( FULL_SCREEN_WIDTH * ( uistate.msg_window_wide_display ? 1.8 :
                    1 ) ) );
    w_height = std::min( TERMY, uistate.msg_window_full_height_display ? TERMY : FULL_SCREEN_HEIGHT );
    w_x = ( TERMX - w_width ) / 2;
    w_y = ( TERMY - w_height ) / 2;
}

void Messages::dialog::init_first_time()
{
    ctxt = input_context( "MESSAGE_LOG" );
    ctxt.register_action( "UP", to_translation( "Scroll up" ) );
    ctxt.register_action( "DOWN", to_translation( "Scroll down" ) );

    static auto actionnames = {
        "PAGE_UP", "PAGE_DOWN", "FILTER", "RESET_FILTER",
        "QUIT", "HELP_KEYBINDINGS", "TOGGLE_WIDE_DISPLAY", "TOGGLE_FULL_HEIGHT_DISPLAY",
        "COPY_MESSAGE", "ERASE_HISTORY"
    };
    for( const auto &actionname : actionnames ) {
        ctxt.register_action( actionname );
    }

    // Calculate time string display width. The translated strings are expected to
    // be aligned, so we choose an arbitrary duration here to calculate the width.
    time_width = utf8_width( to_string_clipped( 1_turns, clipped_align::right ) );
}

void Messages::dialog::init( ui_adaptor &ui )
{
    set_size();

    w = catacurses::newwin( w_height, w_width, point( w_x, w_y ) );

    if( first_init ) {
        init_first_time();
    }

    if( border_width * 2 + time_width + padding_width >= w_width ||
        border_width * 2 >= w_height ) {

        errored = true;
        return;
    }
    msg_width = w_width - border_width * 2 - time_width - padding_width;
    max_lines = static_cast<size_t>( w_height - border_width * 2 );

    // Initialize filter help text and window
    w_fh_width = w_width;
    w_fh_x = w_x;
    help_text = filter_help_text( w_fh_width - border_width * 2 );
    w_fh_height = static_cast<int>( help_text.size() ) + border_width * 2;
    w_fh_y = w_y + w_height - w_fh_height;
    w_filter_help = catacurses::newwin( w_fh_height, w_fh_width, point( w_fh_x, w_fh_y ) );

    // Initialize filter input
    filter.window( w_filter_help, point( border_width + 2, w_fh_height - 1 ),
                   w_fh_width - border_width - 2 );

    // Initialize folded messages
    folded_all.clear();
    folded_filtered.clear();
    const size_t msg_count = size();
    for( size_t ind = 0; ind < msg_count; ++ind ) {
        const size_t msg_ind = log_from_top ? ind : msg_count - 1 - ind;
        const game_message &msg = player_messages.history( msg_ind );
        const auto &folded = foldstring( msg.get_with_count(), msg_width );
        for( const auto &it : folded ) {
            folded_filtered.emplace_back( folded_all.size() );
            folded_all.emplace_back( msg_ind, it );
        }
    }

    do_filter( filter_str );

    ui.position_from_window( w );

    // Open (or no-op if already open) the RmlUi log doc. Must run AFTER
    // init_first_time() built `ctxt` — open()'s set_timeout(16) lands on it and
    // survives later resize re-inits (init_first_time is first-time-only, so the
    // member ctxt is not rebuilt again). Idempotent: open() no-ops when already
    // open on this instance, preserving rml_data across re-inits.
    rml.open( messages_rmlui_enabled(), "messages", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        rml_data = std::make_unique<messages_rml_data>();
        register_messages_rml_types( c );
        c.Bind( "rows", &rml_data->rows );
        c.Bind( "footer_rml", &rml_data->footer_rml );
        rml_data->handle = c.GetModelHandle();
    } );
    if( rml ) {
        rml_initial_scroll_frames = 3;
    }

    first_init = false;
}

void Messages::dialog::show()
{
    if( rml ) {
        // RmlUi path owns the log pane — sync the doc and skip the curses draw.
        sync_rml();
        if( filtering ) {
            // The filter INPUT stays the legacy curses string_input_popup (Tier-0
            // primitive, not migrated), compositing on top of the log doc. The
            // help-text BOX is migrated to a passive RmlUi backdrop (opened lazily
            // here, closed when filtering ends) — the autopickup/safemode help
            // pattern. The curses input field + its <  > markers draw over the
            // backdrop's blank bottom row; only those cells paint (no werase), so
            // the RmlUi box shows through. The help box itself is the RmlUi backdrop.
            if( !filter_help_rml ) {
                open_filter_help_rml();
            }
            mvwprintz( w_filter_help, point( border_width, w_fh_height - 1 ), border_color, "< " );
            mvwprintz( w_filter_help, point( w_fh_width - border_width - 2, w_fh_height - 1 ), border_color,
                       " >" );
            wnoutrefresh( w_filter_help );
            filter.query( false, true ); // Draw only
        } else {
            // Left filtering (input confirmed/canceled) — tear the backdrop down.
            filter_help_rml.close();
        }
        return;
    }
}

void Messages::dialog::open_filter_help_rml()
{
    filter_help_rml.open( messages_rmlui_enabled(), "messages_filter_help", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        filter_help_data = std::make_unique<messages_filter_help_data>();
        // Build the syntax help unfolded (huge width → foldstring only breaks on
        // the format's explicit \n; RmlUi re-wraps), then join and convert the
        // colour tags to RML spans.
        const std::vector<std::string> lines = filter_help_text( 10000 );
        std::string joined;
        for( size_t i = 0; i < lines.size(); ++i ) {
            joined += lines[i];
            if( i + 1 < lines.size() ) {
                joined += '\n';
            }
        }
        filter_help_data->help_rml = cata_text_to_rml( joined );
        c.Bind( "help_rml", &filter_help_data->help_rml );
        filter_help_data->handle = c.GetModelHandle();
    } );
}

void Messages::dialog::do_filter( const std::string &filter_str )
{
    // Split the search string into type and text
    bool has_type_filter = false;
    game_message_type filter_type = m_neutral;
    std::string filter_text;
    const auto colon = filter_str.find( ':' );
    if( colon != std::string::npos ) {
        has_type_filter = msg_type_from_name( filter_type, filter_str.substr( 0, colon ) );
        filter_text = filter_str.substr( colon + 1 );
    } else {
        filter_text = filter_str;
    }

    // Start filtering the log
    folded_filtered.clear();
    for( size_t folded_ind = 0; folded_ind < folded_all.size(); ) {
        const size_t msg_ind = folded_all[folded_ind].first;
        const game_message &msg = player_messages.history( msg_ind );
        const bool match = ( !has_type_filter || filter_type == msg.type ) &&
                           ci_find_substr( remove_color_tags( msg.get_with_count() ), filter_text ) >= 0;

        // Always advance the index, but only add to filtered list if the original message matches
        for( ; folded_ind < folded_all.size() && folded_all[folded_ind].first == msg_ind; ++folded_ind ) {
            if( match ) {
                folded_filtered.emplace_back( folded_ind );
            }
        }
    }

    // Reset view
    if( log_from_top || max_lines > folded_filtered.size() ) {
        offset = 0;
    } else {
        offset = folded_filtered.size() - max_lines;
    }
}

void Messages::dialog::input( const ui_adaptor &ui )
{
    canceled = false;
    if( filtering ) {
        filter.query( false );
        if( filter.confirmed() || filter.canceled() ) {
            filtering = false;
            if( filter_sentry ) {
                disable_ime();
            }
        }
        if( !filter.canceled() ) {
            const std::string &new_filter_str = filter.text();
            if( new_filter_str != filter_str ) {
                filter_str = new_filter_str;

                do_filter( filter_str );
            }
        } else {
            filter.text( filter_str );
        }
    } else {
        const std::string &action = ctxt.handle_input();
        if( action == "DOWN" && offset + max_lines < folded_filtered.size() ) {
            ++offset;
        } else if( action == "UP" && offset > 0 ) {
            --offset;
        } else if( action == "PAGE_DOWN" ) {
            if( offset + max_lines * 2 <= folded_filtered.size() ) {
                offset += max_lines;
            } else if( max_lines <= folded_filtered.size() ) {
                offset = folded_filtered.size() - max_lines;
            } else {
                offset = 0;
            }
        } else if( action == "PAGE_UP" ) {
            if( offset >= max_lines ) {
                offset -= max_lines;
            } else {
                offset = 0;
            }
        } else if( action == "FILTER" ) {
            filtering = true;
            if( filter_sentry ) {
                enable_ime();
            } else {
                // this implies enable_ime() and ensures that the ime mode is always
                // restored when closing the dialog if at least filtered once
                filter_sentry.emplace();
            }
        } else if( action == "RESET_FILTER" ) {
            filter_str.clear();
            filter.text( filter_str );
            do_filter( filter_str );
        } else if( action == "COPY_MESSAGE" ) {
            const auto type_names = msg_type_and_names() | std::ranges::to<std::map>();

            auto format_as_logfmt = [&]( const size_t msg_ind ) {
                const auto &msg = player_messages.history( msg_ind );
                const auto msg_time = msg.timestamp_in_turns;

                const auto it = type_names.find( msg.type );
                const auto &type_name = ( it != type_names.end() ) ? it->second : "unknown";

                const auto escaped_message = replace_all( remove_color_tags( msg.get_with_count() ), "\"", "\\\"" );
                return string_format( "turn=%-9d time=\"%s\" type=%-8s message=\"%s\"\n",
                                      to_turn<int>( msg_time ),
                                      to_string_clipped( calendar::turn - msg_time, clipped_align::right ),
                                      type_name,
                                      escaped_message );
            };

            const auto lines = folded_filtered
            | std::views::transform( [&]( const size_t idx ) { return folded_all[idx].first; } )
            | std::ranges::to<std::set>()
            | std::views::reverse
            | std::views::transform( format_as_logfmt );

            const auto clipboard_text = std::ranges::fold_left( lines, std::string{}, std::plus<> {} );

            DebugLog( DL::Info, DC::Main ) << " MESSAGE LOG COPY:\n" << clipboard_text;
            std::string popup_msg = _( "Messages written to debug.log" );

            // Also copy to clipboard in tiles mode
            const int clipboard_result = SDL_SetClipboardText( clipboard_text.c_str() );
            if( clipboard_result == 0 ) {
                popup_msg = _( "Messages written to debug.log and copied to clipboard" );
            }
            popup( popup_msg );
        } else if( action == "ERASE_HISTORY" ) {
            clear_messages();
            canceled = true;
        } else if( action == "QUIT" ) {
            canceled = true;
        } else if( action == "TOGGLE_WIDE_DISPLAY" || action == "TOGGLE_FULL_HEIGHT_DISPLAY" ) {
            if( action == "TOGGLE_WIDE_DISPLAY" ) {
                uistate.msg_window_wide_display = !uistate.msg_window_wide_display;
            } else {
                uistate.msg_window_full_height_display = !uistate.msg_window_full_height_display;
            }
            ui.mark_resize();
        }

        // RmlUi path: the offset math above is invisible (the doc renders all
        // filtered rows + scrolls natively), so mirror the scroll keys onto the
        // pane. Harmless to also run the offset logic — it just clamps `offset`.
        if( rml ) {
            if( action == "UP" ) {
                rml_scroll( -1 );
            } else if( action == "DOWN" ) {
                rml_scroll( 1 );
            } else if( action == "PAGE_UP" ) {
                rml_scroll( -2 );
            } else if( action == "PAGE_DOWN" ) {
                rml_scroll( 2 );
            }
        }
    }
}

void Messages::dialog::run()
{
    ui_adaptor ui;
    ui.on_screen_resize( [this]( ui_adaptor & ui ) {
        init( ui );
    } );
    ui.mark_resize();
    ui.on_redraw( [this]( const ui_adaptor & ) {
        show();
    } );

    while( !errored && !canceled ) {
        ui_manager::redraw();
        input( ui );
    }
}

// Rebuild the bound model from the (already-filtered) folded log. One row per
// folded line: text coloured by msgtype (inner <color> tags survive), and a time
// column shown only when the time string changes from the previous row (the
// curses ASCII bracket art that marked same-time ranges is dropped). Footer = the
// keybinding hint or the active filter string. Mirrors the curses show() draw.
void Messages::dialog::sync_rml()
{
    if( !rml || !rml_data ) {
        return;
    }
    rml_data->rows.clear();
    std::string prev_time_str;
    for( const size_t folded_ind : folded_filtered ) {
        const size_t msg_ind = folded_all[folded_ind].first;
        const game_message &msg = player_messages.history( msg_ind );
        const nc_color col = msgtype_to_color( msg.type, false );

        messages_rml_row row;
        row.text_rml = cata_text_to_rml( colorize( folded_all[folded_ind].second, col ) );

        const time_point msg_time = msg.timestamp_in_turns;
        const std::string time_str = to_string_clipped( calendar::turn - msg_time, clipped_align::right );
        if( time_str != prev_time_str ) {
            prev_time_str = time_str;
            row.time_rml = cata_text_to_rml( colorize( time_str, time_color ) );
        }
        rml_data->rows.push_back( std::move( row ) );
    }

    if( filter_str.empty() ) {
        rml_data->footer_rml = rml_escape( string_format(
                                               _( "< %s to filter, %s to reset, %s/%s to adjust size, %s to copy, %s to erase >" ),
                                               ctxt.get_desc( "FILTER" ), ctxt.get_desc( "RESET_FILTER" ),
                                               ctxt.get_desc( "TOGGLE_WIDE_DISPLAY" ), ctxt.get_desc( "TOGGLE_FULL_HEIGHT_DISPLAY" ),
                                               ctxt.get_desc( "COPY_MESSAGE" ), ctxt.get_desc( "ERASE_HISTORY" ) ) );
    } else {
        rml_data->footer_rml = cata_text_to_rml( colorize( string_format( "< %s >", filter_str ),
                               filter_color ) );
    }

    rml_data->handle.DirtyVariable( "rows" );
    rml_data->handle.DirtyVariable( "footer_rml" );

    // After a fresh open, scroll the pane to the newest end — matching the curses
    // initial offset (bottom slice when new-at-bottom). history(0) is newest, so
    // for !log_from_top the rows run oldest→newest top-to-bottom and the newest
    // sits at the bottom. Spread over a few frames so RmlUi has laid the rows out.
    if( rml_initial_scroll_frames > 0 ) {
        --rml_initial_scroll_frames;
        if( !log_from_top ) {
            if( Rml::Element *e = rml.document()->GetElementById( "msg-screen" ) ) {
                e->SetScrollTop( e->GetScrollHeight() );
            }
        }
    }
}

// Scroll the log pane. dir: -1 line up, +1 line down, -2 page up, +2 page down.
void Messages::dialog::rml_scroll( int dir )
{
    if( !rml ) {
        return;
    }
    Rml::Element *e = rml.document()->GetElementById( "msg-screen" );
    if( !e ) {
        return;
    }
    const float line = 18.0f; // approx one row; native wheel handles fine scroll
    const float page = e->GetClientHeight();
    float delta = 0.0f;
    switch( dir ) {
        case -1:
            delta = -line;
            break;
        case 1:
            delta = line;
            break;
        case -2:
            delta = -page;
            break;
        case 2:
            delta = page;
            break;
    }
    e->SetScrollTop( e->GetScrollTop() + delta );
}

std::vector<std::string> Messages::dialog::filter_help_text( int width )
{
    const auto &help_fmt = _(
                               "<color_light_gray>The default is to search the entire message log.  "
                               "Use message-types as prefixes followed by (:) to filter more specific.\n"
                               "Valid message-type values are:</color> %s\n"
                               "\n"
                               "<color_white>Examples:</color>\n"
                               "  <color_light_green>good</color><color_white>:mutation\n"
                               "  :you pick up: 1</color>\n"
                               "  <color_light_red>bad</color><color_white>:</color>\n"
                               "\n"
                           );
    std::string type_text;
    const auto &type_list = msg_type_and_names();
    for( auto it = type_list.begin(); it != type_list.end(); ++it ) {
        // Skip m_debug outside debug mode (but allow searching for it)
        if( debug_mode || it->first != m_debug ) {
            const auto &col_name = get_all_colors().get_name( msgtype_to_color( it->first ) );
            auto next_it = std::next( it );
            // Skip m_debug outside debug mode
            if( !debug_mode && next_it != type_list.end() && next_it->first == m_debug ) {
                next_it = std::next( next_it );
            }
            if( next_it != type_list.end() ) {
                //~ the 2nd %s is a type name, this is used to format a list of type names
                type_text += string_format( pgettext( "message log", "<color_%s>%s</color>, " ),
                                            col_name, pgettext( "message type", it->second ) );
            } else {
                //~ the 2nd %s is a type name, this is used to format the last type name in a list of type names
                type_text += string_format( pgettext( "message log", "<color_%s>%s</color>." ),
                                            col_name, pgettext( "message type", it->second ) );
            }
        }
    }
    return foldstring( string_format( help_fmt, type_text ), width );
}

void Messages::display_messages()
{
    dialog dlg;
    dlg.run();
    player_messages.curmes = calendar::turn;
}

void add_msg( std::string msg )
{
    Messages::add_msg( std::move( msg ) );
}

void add_msg( const game_message_params &params, std::string msg )
{
    Messages::add_msg( params, std::move( msg ) );
}
