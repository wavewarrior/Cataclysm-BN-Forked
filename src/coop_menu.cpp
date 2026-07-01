#ifdef COOP_ENABLED

#include "coop_menu.h"

#include "coop_client.h"
#include "coop_server.h"
#include "coop_session.h"
#include "input.h"
#include "output.h"
#include "translations.h"

namespace coop_menu {

auto run() -> void {
    // TODO: Phase 2 — show Host/Join selection UI, call start_host() or start_join()
    popup(_("Co-op coming soon!  Host/Join flow not yet implemented."));
}

} // namespace coop_menu

#endif // COOP_ENABLED
