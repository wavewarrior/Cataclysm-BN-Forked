#include "frame_report.h"

void frame_report::add_step( frame_step_record record )
{
    if( record_count_ < max_records_ ) {
        records_[record_count_] = record;
        ++record_count_;
    }
    if( record.lap < laps_.size() ) {
        frame_lap_timing &l = laps_[record.lap];
        ++l.steps;
        l.sum_ms += record.duration_ms;
        l.max_ms = l.max_ms > record.duration_ms ? l.max_ms : record.duration_ms;
    }
}

void frame_report::set_precondition( double duration_ms )
{
    precondition_.ran = true;
    precondition_.duration_ms = duration_ms;
    frame_lap_timing &l = laps_[0];
    ++l.steps;
    l.sum_ms += duration_ms;
    l.max_ms = l.max_ms > duration_ms ? l.max_ms : duration_ms;
}

auto frame_report::lap_ms( std::size_t index ) const -> double
{
    return index < laps_.size() ? laps_[index].sum_ms : 0.0;
}

auto frame_report::total_step_ms() const -> double
{
    double total = 0.0;
    for( const frame_step_record &r : step_records() ) {
        total += r.duration_ms;
    }
    return total;
}

void frame_report::note_order_violation()
{
    order_violation_ = true;
}

auto frame_report::record_for( frame_step_kind kind ) const -> const frame_step_record
*   // *NOPAD*
{
    for( const frame_step_record &r : step_records() ) {
        if( r.kind == kind ) {
            return &r;
        }
    }
    return nullptr;
}
