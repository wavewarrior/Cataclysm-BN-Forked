#include "rml_screen.h"

#include <set>

#include <RmlUi/Core.h>

#include "input.h"
#include "path_info.h"
#include "lighting/rmlui_layer.h"

namespace
{
// Model names with a live document — the single-instance guard, generalized from
// the per-screen g_*_model_active globals the committed screens still use. A name
// is present here ONLY while a fully-opened document for it exists; open()'s
// failure paths never insert, so a failed open can't leave a name stuck (which
// would stop that screen from ever reopening — the "2nd-open empty/fallback"
// failure the plan flagged).
std::set<std::string> &active_models()
{
    static std::set<std::string> instances;
    return instances;
}
} // namespace

bool rml_doc::open( bool enabled, const std::string &model_name, input_context &ctx,
                    const std::function<void( Rml::DataModelConstructor & )> &bind,
                    bool passive )
{
    if( doc_ != nullptr ) {
        // Already open on this instance — nothing to do.
        return true;
    }
    if( !enabled || !rmlui_layer::ready() || active_models().contains( model_name ) ) {
        return false;
    }
    Rml::Context *ctx_rml = rmlui_layer::context();
    if( ctx_rml == nullptr ) {
        return false;
    }
    Rml::DataModelConstructor c = ctx_rml->CreateDataModel( model_name );
    if( !c ) {
        return false;
    }
    // Screen-specific: register structs, Bind members, BindEventCallback, and
    // capture c.GetModelHandle() into the screen's session.
    bind( c );
    Rml::ElementDocument *doc =
        rmlui_layer::open_document( PATH_INFO::datadir() + "gui/" + model_name + ".rml", passive );
    if( doc == nullptr ) {
        // Roll back the model so the guard below is never taken on failure.
        ctx_rml->RemoveDataModel( model_name );
        return false;
    }
    // Commit state LAST, so every early-return above left the guard untouched.
    model_name_ = model_name;
    doc_ = doc;
    active_models().insert( model_name );
    // Tick at 16ms so RmlUi hover/mouse-wheel stay live between keys.
    ctx.set_timeout( 16 );
    return true;
}

void rml_doc::close()
{
    if( doc_ == nullptr ) {
        return;
    }
    rmlui_layer::close_document( doc_ );
    if( Rml::Context *ctx_rml = rmlui_layer::context() ) {
        ctx_rml->RemoveDataModel( model_name_ );
    }
    active_models().erase( model_name_ );
    doc_ = nullptr;
    model_name_.clear();
}

rml_doc::~rml_doc()
{
    close();
}
