#include "frame_executor.h"
auto frame_executor::elapsed_ms( clock::time_point now, clock::time_point start ) -> double
{
    return std::chrono::duration<double, std::milli>( now - start ).count();
}

frame_executor::frame_executor( const frame_plan &plan, frame_report &report )
    : plan_( plan ), report_( report ) {}

void frame_executor::begin_precondition()
{
    precondition_at_ = clock::now();
    precondition_open_ = true;
}

void frame_executor::end_precondition()
{
    if( !precondition_open_ ) {
        return;
    }
    precondition_open_ = false;
    report_.set_precondition( elapsed_ms( clock::now(), precondition_at_ ) );
}

void frame_executor::close_open( clock::time_point now )
{
    if( !open_ ) {
        return;
    }
    report_.add_step( {
        .kind = open_->kind,
        .status = open_->status,
        .reason = open_->reason,
        .duration_ms = elapsed_ms( now, open_at_ ),
        .lap = frame_lap_of( open_->kind ),
    } );
    open_.reset();
}

auto frame_executor::next_step() -> std::optional<frame_step>
{
    const clock::time_point now = clock::now();
    close_open( now );
    while( index_ < plan_.size() ) {
        const frame_step &step = plan_.step_at( index_++ );
        if( step.status != frame_step_status::run ) {
            report_.add_step( {
                .kind = step.kind,
                .status = step.status,
                .reason = step.reason,
                .duration_ms = 0.0,
                .lap = frame_lap_of( step.kind ),
            } );
            continue;
        }
        open_ = step;
        open_at_ = now;
        return step;
    }
    return std::nullopt;
}

void frame_executor::split( frame_step_kind kind )
{
    const clock::time_point now = clock::now();
    close_open( now );
    // The fused body may only split along the plan: a split that is not the next
    // planned member means the body's order drifted from the asserted order.
    if( index_ >= plan_.size() || plan_.step_at( index_ ).kind != kind ) {
        report_.note_order_violation();
        return;
    }
    open_ = plan_.step_at( index_++ );
    open_at_ = now;
}

void frame_executor::finish()
{
    close_open( clock::now() );
}
