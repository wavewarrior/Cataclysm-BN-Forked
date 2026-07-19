
#include "coop_session.h"

// static
auto coop_session::get() -> coop_session&
{
    static coop_session instance;
    return instance;
}

