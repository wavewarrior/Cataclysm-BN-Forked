#pragma once
#ifndef CATA_SRC_RML_CALLBACK_H
#define CATA_SRC_RML_CALLBACK_H

#include <RmlUi/Core.h>
#include <utility>

/// Wraps a callable `fn(int)` into an RmlUI event callback that extracts
/// the first VariantList argument as an int index. Negative indices are
/// silently ignored.  Eliminates the repeated boilerplate in every
/// BindEventCallback call across migrated screens.
template<typename Fn>
auto rml_idx_callback( Fn &&fn )
{
    return [fn = std::forward<Fn>( fn )](
               Rml::DataModelHandle, Rml::Event &,
    const Rml::VariantList & args ) {
        int idx = -1;
        if( !args.empty() ) {
            args[0].GetInto( idx );
        }
        if( idx >= 0 ) {
            fn( idx );
        }
    };
}

#endif // CATA_SRC_RML_CALLBACK_H
