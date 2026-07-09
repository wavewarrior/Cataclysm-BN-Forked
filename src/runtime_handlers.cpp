#include "runtime_handlers.h"
#include "cursesdef.h"
#include "debug.h"
#include "init.h"
#include "game.h"

[[ noreturn ]]
void exit_handler( int status )
{
    // Must join prewarm worker before unload_data() to avoid race
    init::clear_prewarm();
    DynamicDataLoader::get_instance().unload_data();
    deinitDebug();
    g.reset();
    catacurses::endwin();
    exit( status );
}
