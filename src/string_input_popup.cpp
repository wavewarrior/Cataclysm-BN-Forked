#include "string_input_popup.h"

#include "catacharset.h"
#include "char_validity_check.h"
#include "ime.h"
#include "input.h"
#include "lighting/rmlui_layer.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "sdl_wrappers.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "wcwidth.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

string_input_popup::string_input_popup() = default;

// ~string_input_popup is defined after rml_session_t (see bottom of file).

void string_input_popup::create_window()
{
    titlesize = utf8_width( remove_color_tags( _title ) ); // Occupied horizontal space
    if( _max_length <= 0 ) { _max_length = _width; }
    // 2 for border (top and bottom) and 1 for the input text line.
    w_height = 2 + 1;

    // |"w_width = width + titlesize (this text) + 5": _____  |
    w_width = FULL_SCREEN_WIDTH;
    if( _width <= 0 ) {
        _width = std::max( 5, FULL_SCREEN_WIDTH - titlesize - 5 ); // Default if unspecified
    } else {
        _width = std::min( FULL_SCREEN_WIDTH - 20, _width );
        w_width = _width + titlesize + 5;
    }

    title_split = {_title};
    if( w_width > FULL_SCREEN_WIDTH ) {
        // Out of horizontal space- wrap the title
        titlesize = FULL_SCREEN_WIDTH - _width - 5;
        w_width = FULL_SCREEN_WIDTH;

        for( int wraplen = w_width - 2; wraplen >= titlesize; wraplen-- ) {
            title_split = foldstring( _title, wraplen );
            if( utf8_width( title_split.back() ) <= titlesize ) { break; }
        }
        title_height = static_cast<int>( title_split.size() ) - 1;
        w_height += title_height;
    }

    if( !_description.empty() ) {
        const int twidth = std::min( utf8_width( remove_color_tags( _description ) ), w_width - 4 );
        description_height = foldstring( _description, twidth ).size();
        w_height += description_height;
        if( w_height > TERMY ) {
            description_height = TERMY - 2 - title_height - 1;
            w_height = TERMY;
        }
    }
    // length of title + border (left) + space
    _startx = titlesize + 1;

    if( _max_length <= 0 ) { _max_length = 1024; }
    _endx = w_width - 3;

    const int w_y = ( TERMY - w_height ) / 2;
    const int w_x = std::max( ( TERMX - w_width ) / 2, 0 );
    _starty = title_height;
    w_full = catacurses::newwin( w_height, w_width, point( w_x, w_y ) );
    if( !_description.empty() ) {
        w_description = catacurses::newwin( description_height, w_width - 1, point( w_x, w_y + 1 ) );
        desc_view_ptr = std::make_unique<scrolling_text_view>( w_description );
        desc_view_ptr->set_text( _description );
    }
    w_title_and_entry = catacurses::newwin(
                            w_height - description_height - 2, w_width - 2,
                            point( w_x + 1, w_y + 1 + description_height ) );

    custom_window = false;
}

void string_input_popup::create_context()
{
    ctxt_ptr = std::make_unique<input_context>( "STRING_INPUT" );
    ctxt = ctxt_ptr.get();
    ctxt->register_action( "TEXT.QUIT" );
    ctxt->register_action( "TEXT.CONFIRM" );
    if( !_identifier.empty() ) {
        ctxt->register_action( "HISTORY_UP" );
        ctxt->register_action( "HISTORY_DOWN" );
    }
    ctxt->register_action( "TEXT.LEFT" );
    ctxt->register_action( "TEXT.RIGHT" );
    ctxt->register_action( "TEXT.CLEAR" );
    ctxt->register_action( "TEXT.BACKSPACE" );
    ctxt->register_action( "TEXT.HOME" );
    ctxt->register_action( "TEXT.END" );
    ctxt->register_action( "TEXT.DELETE" );
    ctxt->register_action( "TEXT.PASTE" );
    ctxt->register_action( "TEXT.INPUT_FROM_FILE" );
    ctxt->register_action( "HELP_KEYBINDINGS" );
    ctxt->register_action( "PAGE_UP" );
    ctxt->register_action( "PAGE_DOWN" );
    ctxt->register_action( "SCROLL_UP" );
    ctxt->register_action( "SCROLL_DOWN" );
    ctxt->register_action( "NUMPAD_6" );
    ctxt->register_action( "ANY_INPUT" );
}

void string_input_popup::show_history( utf8_wrapper& ret )
{
    if( _identifier.empty() ) { return; }
    std::vector<std::string> &hist = uistate.gethistory( _identifier );
    uilist hmenu;
    hmenu.title = _( "d: delete history" );
    hmenu.allow_anykey = true;
    for( size_t h = 0; h < hist.size(); h++ ) { hmenu.addentry( h, true, -2, hist[h] ); }
    if( !ret.empty()
        && ( hmenu.entries.empty() || hmenu.entries[hist.size() - 1].txt != ret.str() ) ) {
        hmenu.addentry( hist.size(), true, -2, ret.str() );
    }

    if( !hmenu.entries.empty() ) {
        hmenu.selected = hmenu.entries.size() - 1;

        hmenu.w_height_setup = [&]() -> int {
            // number of lines that make up the menu window: 2*border+entries
            int height = 2 + hmenu.entries.size();
            if( getbegy( w_full ) < height ) { height = std::max( getbegy( w_full ), 4 ); }
            return height;
        };
        hmenu.w_x_setup = [&]( int ) -> int { return getbegx( w_full ); };
        hmenu.w_y_setup = [&]( const int height ) -> int {
            return std::max( getbegy( w_full ) - height, 0 );
        };

        bool finished = false;
        do {
            hmenu.query();
            if( hmenu.ret >= 0 && hmenu.entries[hmenu.ret].txt != ret.str() ) {
                ret = hmenu.entries[hmenu.ret].txt;
                if( static_cast<size_t>( hmenu.ret ) < hist.size() ) {
                    hist.erase( hist.begin() + hmenu.ret );
                    hist.push_back( ret.str() );
                }
                _position = ret.size();
                finished = true;
            } else if( hmenu.ret == UILIST_UNBOUND && hmenu.keypress == 'd' ) {
                hist.clear();
                finished = true;
            } else if( hmenu.ret != UILIST_UNBOUND ) {
                finished = true;
            }
        } while( !finished );
    }
}

void string_input_popup::add_to_history( const std::string& value ) const
{
    if( !_identifier.empty() && !value.empty() ) {
    std::vector<std::string> &hist = uistate.gethistory( _identifier );
        if( hist.empty() || hist[hist.size() - 1] != value ) { hist.push_back( value ); }
    }
}

void string_input_popup::update_input_history( utf8_wrapper& ret, bool up )
{
    if( _identifier.empty() ) { return; }

    std::vector<std::string> &hist = uistate.gethistory( _identifier );

    if( hist.empty() ) { return; }

    if( hist.size() >= _hist_max_size ) {
        hist.erase( hist.begin(), hist.begin() + ( hist.size() - _hist_max_size ) );
    }

    if( up ) {
        if( _hist_str_ind >= static_cast<int>( hist.size() ) ) {
            return;
        } else if( _hist_str_ind == 0 ) {
            _session_str_entered = ret.str();

            // avoid showing the same result twice (after reopen filter window without reset)
            if( hist.size() > 1 && _session_str_entered == hist.back() ) { _hist_str_ind += 1; }
        }
    } else {
        if( _hist_str_ind == 1 ) {
            ret = _session_str_entered;
            _position = ret.length();
            // show initial string entered and 'return'
            _hist_str_ind = 0;
            return;
        } else if( _hist_str_ind == 0 ) {
            return;
        }
    }

    _hist_str_ind += up ? 1 : -1;
    ret = hist[hist.size() - _hist_str_ind];
    _position = ret.length();
}

void string_input_popup::draw(
    ui_adaptor* const ui, const utf8_wrapper& ret, const utf8_wrapper& edit ) const
{
    // RmlUi handles rendering when it is active for this instance.
    if( rml_session ) { return; }
}

void string_input_popup::query( const bool loop, const bool draw_only )
{
    query_string( loop, draw_only );
}

int string_input_popup::query_int( const bool loop, const bool draw_only )
{
    return std::atoi( query_string( loop, draw_only ).c_str() );
}

int64_t string_input_popup::query_int64_t( const bool loop, const bool draw_only )
{
    return std::atoll( query_string( loop, draw_only ).c_str() );
}

const std::string &string_input_popup::query_string(
    const bool loop, const bool draw_only, const bool printable )
{
    if( !custom_window && !w_full ) {
        create_window();
        _position = -1;
    }
    if( !ctxt ) { create_context(); }

    std::optional<ime_sentry> sentry;
    if( !draw_only && loop ) { sentry.emplace(); }
    utf8_wrapper ret( _text );
    utf8_wrapper edit( ctxt->get_edittext() );
    if( _position == -1 ) { _position = ret.length(); }
    const int scrmax = std::max( 0, _endx - _startx );

    std::unique_ptr<ui_adaptor> ui;
    if( !draw_only && !custom_window ) {
        ui = std::make_unique<ui_adaptor>();
        ui->position_from_window( w_full );
        ui->on_screen_resize( [this]( ui_adaptor & ui ) {
            create_window();
            ui.position_from_window( w_full );
        } );
        ui->on_redraw( [&]( ui_adaptor & ui ) { draw( &ui, ret, edit ); } );
    }

    int ch = 0;

    const bool use_rmlui = !draw_only && rml_open();

    _canceled = false;
    _confirmed = false;
    do {
        if( _position < 0 ) { _position = 0; }
        if( shift < 0 ) { shift = 0; }

        const size_t width_to_cursor_start = ret.substr( 0, _position ).display_width();
        size_t width_to_cursor_end = width_to_cursor_start;
        if( static_cast<size_t>( _position ) < ret.length() ) {
            width_to_cursor_end += ret.substr( _position, 1 ).display_width();
        } else {
            width_to_cursor_end += 1;
        }
        // starts scrolling when the cursor is this far from the start or end
        const size_t scroll_width = std::min( 10, scrmax / 5 );
        if( ret.display_width() < static_cast<size_t>( scrmax ) ) {
            shift = 0;
        } else if( width_to_cursor_start < static_cast<size_t>( shift ) + scroll_width ) {
            shift = std::max( width_to_cursor_start, scroll_width ) - scroll_width;
        } else if( width_to_cursor_end > static_cast<size_t>( shift + scrmax ) - scroll_width ) {
            shift = std::min( width_to_cursor_end + scroll_width, ret.display_width() ) - scrmax;
        }
        const utf8_wrapper text_before_start = ret.substr_display( 0, shift );
        const size_t width_before_start = text_before_start.display_width();
        if( width_before_start != static_cast<size_t>( shift ) ) {
            // This prevents a multi-cell character from been split, which is not possible
            // instead scroll a cell further to make that character disappear completely
            const size_t width_at_start = ret.substr( text_before_start.length(), 1 ).display_width();
            shift = width_before_start + width_at_start;
        }

        if( use_rmlui ) {
            _text = ret.str();
            rml_sync();
        }

        if( ui ) {
            ui_manager::redraw();
        } else {
            draw( nullptr, ret, edit );
        }

        if( draw_only ) { break; }

        const std::string action = use_rmlui ? ctxt->handle_input( 16 ) : ctxt->handle_input();
        const input_event ev = ctxt->get_raw_input();
        ch = ev.type == input_event_t::keyboard ? ev.get_first_input() : 0;
        _handled = true;

        if( callbacks[ch] ) {
            if( callbacks[ch]() ) { continue; }
        }

        if( use_rmlui && action == "TIMEOUT" ) {
            if( loop ) {
                // Internal frame tick — redraw and keep looping.
                // Mouse hover / caret animation updates without input.
                continue;
            }
            // draw_only or single-shot; fall through to exit.
        }

        if( action == "TEXT.QUIT" ) {
            _text.clear();
            _position = -1;
            _canceled = true;
            break;
        } else if( action == "TEXT.CONFIRM"
                   || ( action == "TEXT.RIGHT"
                        && !( edit.empty() && _position + 1 <= static_cast<int>( ret.size() ) ) ) ) {
            add_to_history( ret.str() );
            _confirmed = true;
            _text = ret.str();
            if( !_hist_use_uilist ) {
                _hist_str_ind = 0;
                _session_str_entered.erase( 0 );
            }
            break;
        } else if( action == "HISTORY_UP" ) {
            if( !_identifier.empty() ) {
                if( edit.empty() ) {
                    if( _hist_use_uilist ) {
                        show_history( ret );
                    } else {
                        update_input_history( ret, true );
                    }
                }
            } else {
                _handled = false;
            }
        } else if( action == "HISTORY_DOWN" ) {
            if( !_identifier.empty() ) {
                if( edit.empty() && !_hist_use_uilist ) { update_input_history( ret, false ); }
            } else {
                _handled = false;
            }

        } else if( action == "TEXT.RIGHT" ) {
            if( edit.empty() && _position + 1 <= static_cast<int>( ret.size() ) ) { _position++; }
        } else if( action == "TEXT.LEFT" ) {
            if( edit.empty() && _position > 0 ) { _position--; }
        } else if( action == "TEXT.CLEAR" ) {
            _position = 0;
            ret.erase( 0 );
        } else if( action == "TEXT.BACKSPACE" ) {
            if( _position > 0 && _position <= static_cast<int>( ret.size() ) ) {
                _position--;
                ret.erase( _position, 1 );
            }
        } else if( action == "TEXT.HOME" ) {
            if( edit.empty() ) { _position = 0; }
        } else if( action == "TEXT.END" ) {
            if( edit.empty() ) { _position = ret.size(); }
        } else if( action == "TEXT.DELETE" ) {
            if( _position < static_cast<int>( ret.size() ) ) { ret.erase( _position, 1 ); }
            /*Note: SCROLL_UP/SCROLL_DOWN should by default only trigger on mousewheel,
             * since up/down arrow were handled above */
        } else if( action == "SCROLL_UP" ) {
            if( desc_view_ptr ) { desc_view_ptr->scroll_up(); }
        } else if( action == "SCROLL_DOWN" ) {
            if( desc_view_ptr ) { desc_view_ptr->scroll_down(); }
        } else if( action == "PAGE_UP" ) {
            if( desc_view_ptr ) { desc_view_ptr->page_up(); }
        } else if( action == "PAGE_DOWN" ) {
            if( desc_view_ptr ) { desc_view_ptr->page_down(); }
        } else if( action == "TEXT.PASTE" || action == "TEXT.INPUT_FROM_FILE"
                   || ( action == "ANY_INPUT" && !ev.text.empty() ) ) {
            // paste, input from file, or text input
            // bail out early if already at length limit
            if( _max_length <= 0 || ret.display_width() < static_cast<size_t>( _max_length ) ) {
                std::string entered;
                if( action == "TEXT.PASTE" ) {
                    if( edit.empty() ) {
                        char *const clip = SDL_GetClipboardText();
                        if( clip ) {
                            entered = clip;
                            SDL_free( clip );
                        }
                    }
                } else if( action == "TEXT.INPUT_FROM_FILE" ) {
                    if( edit.empty() ) { entered = get_input_string_from_file(); }
                } else {
                    entered = ev.text;
                }
                if( !entered.empty() ) {
                    utf8_wrapper insertion;
                    const char *str = entered.c_str();
                    int len = entered.length();
                    int width = ret.display_width();
                    while( len > 0 ) {
                        const uint32_t ch = UTF8_getch( &str, &len );
                        // Use mk_wcwidth to filter out control characters
                        if( _only_digits ? ch == '-' || isdigit( ch ) : mk_wcwidth( ch ) >= 0 ) {
                            const int newwidth = mk_wcwidth( ch );
                            // Filter out non-printable characters if necessary
                            if( printable && !is_char_allowed( ch ) ) { break; }
                            if( _max_length <= 0 || width + newwidth <= _max_length ) {
                                insertion.append( utf8_wrapper( utf32_to_utf8( ch ) ) );
                                width += newwidth;
                            } else {
                                break;
                            }
                        }
                    }
                    ret.insert( _position, insertion );
                    _position += insertion.length();
                    edit = utf8_wrapper();
                    ctxt->set_edittext( std::string() );
                }
            }
        } else if( ev.edit_refresh ) {
            edit = utf8_wrapper( ev.edit );
            ctxt->set_edittext( ev.edit );
        } else {
            _handled = false;
        }
    } while( loop && !_canceled && !_confirmed );
    if( use_rmlui ) { rml_close(); }
    _text = ret.str();
    return _text;
}

string_input_popup &string_input_popup::window( const catacurses::window& w, point start,
        int endx )
{
    if( !custom_window && this->w_full ) {
        // default window already created
        return *this;
    }
    this->w_title_and_entry = w;
    _startx = start.x;
    _starty = start.y;
    _endx = endx;
    custom_window = true;
    this->w_full = catacurses::newwin( 1, _endx - _startx, point( getbegx( w ) + _startx, _starty ) );
    return *this;
}

string_input_popup &string_input_popup::context( input_context& ctxt )
{
    ctxt_ptr.reset();
    this->ctxt = &ctxt;
    return *this;
}

void string_input_popup::edit( std::string& value )
{
    only_digits( false );
    text( value );
    query();
    if( !canceled() ) { value = text(); }
}

// NOLINTNEXTLINE(cata-no-long)
void string_input_popup::edit( long &value )
{
    only_digits( true );
    text( std::to_string( value ) );
    query();
    if( !canceled() ) { value = std::atol( text().c_str() ); }
}

void string_input_popup::edit( int &value )
{
    only_digits( true );
    text( std::to_string( value ) );
    query();
    if( !canceled() ) { value = std::atoi( text().c_str() ); }
}

string_input_popup &string_input_popup::text( const std::string& value )
{
    _text = value;
    const auto u8size = utf8_wrapper( _text ).size();
    if( _position < 0 || static_cast<size_t>( _position ) > u8size ) { _position = u8size; }
    return *this;
}

// ---- RmlUi session ----------------------------------------------------------

struct string_input_popup::rml_session_t {
    Rml::String title_rml;
    Rml::String desc_rml;
    bool has_title = false;
    bool has_description = false;
    Rml::String before;
    Rml::String after;
    Rml::DataModelHandle handle;
    Rml::ElementDocument *doc = nullptr;
};

static bool g_rml_string_input_types_registered = false;
static bool g_rml_string_input_model_active = false;

string_input_popup::~string_input_popup() { delete rml_session; }

bool string_input_popup::rml_open()
{
    if( custom_window || !string_input_rmlui_enabled() || !rmlui_layer::ready() ) { return false; }
    Rml::Context* ctx = rmlui_layer::context();
    if( ctx == nullptr || g_rml_string_input_model_active ) { return false; }
    Rml::DataModelConstructor c = ctx->CreateDataModel( "string_input" );
    if( !c ) { return false; }
    rml_session = new rml_session_t();

    if( !g_rml_string_input_types_registered ) {
        c.RegisterArray<Rml::Vector<Rml::String>>();
        g_rml_string_input_types_registered = true;
    }

    c.Bind( "title_rml", &rml_session->title_rml );
    c.Bind( "desc_rml", &rml_session->desc_rml );
    c.Bind( "has_title", &rml_session->has_title );
    c.Bind( "has_description", &rml_session->has_description );
    c.Bind( "before", &rml_session->before );
    c.Bind( "after", &rml_session->after );
    rml_session->handle = c.GetModelHandle();

    rml_session->doc = rmlui_layer::open_document( PATH_INFO::datadir() + "gui/string_input.rml" );
    if( rml_session->doc == nullptr ) {
        ctx->RemoveDataModel( "string_input" );
        delete rml_session;
        rml_session = nullptr;
        return false;
    }
    g_rml_string_input_model_active = true;
    rml_sync();
    ctx->Update();
    return true;
}

void string_input_popup::rml_sync()
{
    if( !rml_session ) { return; }
    rml_session_t &s = *rml_session;

    s.title_rml = cata_text_to_rml( _title );
    s.desc_rml = cata_text_to_rml( _description );
    s.has_title = !_title.empty();
    s.has_description = !_description.empty();

    utf8_wrapper uw( _text );
    const size_t pos = std::min( static_cast<size_t>( _position ), uw.length() );
    s.before = uw.substr( 0, pos ).str();
    s.after = uw.substr( pos ).str();

    Rml::DataModelHandle h = s.handle;
    h.DirtyVariable( "title_rml" );
    h.DirtyVariable( "desc_rml" );
    h.DirtyVariable( "has_title" );
    h.DirtyVariable( "has_description" );
    h.DirtyVariable( "before" );
    h.DirtyVariable( "after" );
}

void string_input_popup::rml_close()
{
    if( !rml_session ) { return; }
    if( rml_session->doc != nullptr ) { rmlui_layer::close_document( rml_session->doc ); }
    if( Rml::Context * ctx = rmlui_layer::context() ) { ctx->RemoveDataModel( "string_input" ); }
    g_rml_string_input_model_active = false;
    delete rml_session;
    rml_session = nullptr;
}
