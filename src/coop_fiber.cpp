
// MINICORO_IMPL must be defined in exactly one translation unit.
#define MINICORO_IMPL
#define MCO_NO_DEBUG
#include "coop_fiber.h"
#include "driver_loop.h"

#include "minicoro.h"

#include <cassert>
#include <exception>
#include <utility>

// ---------------------------------------------------------------------------
// Thread-local channel
// ---------------------------------------------------------------------------

thread_local coop_fiber *coop_fiber::active_fiber_ = nullptr;
thread_local input_event coop_fiber::pending_event_;

namespace
{
// An exception that escaped a fiber function while the agent driver serves, parked until
// resume() rethrows it on the caller's stack: it must not unwind past the coroutine's own stack.
// Outside the driver nothing is caught here, so co-op fibers behave as they always did.
thread_local std::exception_ptr fiber_error;
} // namespace

// ---------------------------------------------------------------------------
// minicoro entry point
// ---------------------------------------------------------------------------

auto coop_fiber::entry_( mco_coro* co ) -> void
{
    auto* self = static_cast<coop_fiber *>( mco_get_user_data( co ) );
    if( !driver_mode_active() ) {
        self->fn_();
        return;
    }
    try {
        self->fn_();
    } catch( ... ) {
        fiber_error = std::current_exception();
    }
    // fn_() returned — fiber is done; minicoro marks it MCO_DEAD.
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

coop_fiber::coop_fiber( std::function<void()> fn ): fn_( std::move( fn ) )
{
    mco_desc desc = mco_desc_init( entry_, STACK_SIZE );
    desc.user_data = this;
    const mco_result r = mco_create( &co_, &desc );
    assert( r == MCO_SUCCESS );
    ( void )r;
}

coop_fiber::~coop_fiber()
{
    if( co_ ) { mco_destroy( co_ ); }
}

auto coop_fiber::resume( const input_event& evt ) -> void
{
    pending_event_ = evt;
    active_fiber_ = this;
    mco_resume( co_ ); // switches to fiber; returns when fiber yields or finishes
    active_fiber_ = nullptr;
    if( fiber_error ) {
        std::rethrow_exception( std::exchange( fiber_error, nullptr ) );
    }
}

auto coop_fiber::done() const -> bool { return !co_ || mco_status( co_ ) == MCO_DEAD; }

// static — called from inside input_context::handle_input() on every iteration
auto coop_fiber::yield_event() -> input_event
{
    assert( active_fiber_ != nullptr && "coop_fiber::yield_event() called outside a fiber" );
    mco_yield( mco_running() ); // suspends; main loop writes pending_event_ then calls resume()
    return pending_event_;    // whatever the main loop just fed us
}

// static
auto coop_fiber::active() -> bool { return active_fiber_ != nullptr; }

