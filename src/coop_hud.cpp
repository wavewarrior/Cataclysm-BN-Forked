#ifdef COOP_ENABLED

#include "coop_hud.h"

#include "coop_session.h"
#include "color.h"
#include "cursesdef.h"
#include "output.h"
#include "translations.h"

namespace coop_hud
{

auto draw( const catacurses::window& w ) -> void
{
    if( !coop_session::get().is_coop() ) { return; }
const auto& sess = coop_session::get();
const std::string name = sess.partner_name.empty() ? "Partner" : sess.partner_name;
const std::string mode_str = sess.is_host() ? _( "HOST" ) : _( "CLIENT" );
mvwprintz( w, point( 0, 0 ), c_light_blue,
           "[Co-op %s: %s]", mode_str, name );
if( sess.partner_ping_ms > 0 ) {
    mvwprintz( w, point( 0, 1 ), c_white, "Ping: %dms", sess.partner_ping_ms );
    }
    wrefresh( w );
}

} // namespace coop_hud

#endif // COOP_ENABLED
