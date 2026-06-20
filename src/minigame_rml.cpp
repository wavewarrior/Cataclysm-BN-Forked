#include "minigame_rml.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"

// Tier 9 shared char-grid widget. Single active minigame → file-static model
// state + the rml_doc single-instance guard (keyed "minigame"). The bind lambda
// registers the row struct + array and binds the three model vars; the game feeds
// colour-tagged strings via set_*, which cata_text_to_rml converts to RML spans.

namespace
{

struct mg_row {
    Rml::String rml;
};

rml_doc g_doc;
Rml::DataModelHandle g_handle;
Rml::String g_title;
Rml::String g_footer;
Rml::Vector<mg_row> g_rows;

}  // namespace

bool &minigames_rmlui_enabled()
{
    static bool enabled = false;
    return enabled;
}

namespace minigame_rml
{

bool open( bool enabled, input_context &ctxt )
{
    return g_doc.open( enabled, "minigame", ctxt,
    []( Rml::DataModelConstructor & c ) {
        Rml::StructHandle<mg_row> rh = c.RegisterStruct<mg_row>();
        rh.RegisterMember( "rml", &mg_row::rml );
        c.RegisterArray<Rml::Vector<mg_row>>();
        c.Bind( "title", &g_title );
        c.Bind( "footer", &g_footer );
        c.Bind( "rows", &g_rows );
        g_handle = c.GetModelHandle();
    } );
}

bool active()
{
    return static_cast<bool>( g_doc );
}

void close()
{
    g_doc.close();
    g_handle = Rml::DataModelHandle();
    g_rows.clear();
}

void set_title( const std::string &title )
{
    g_title = cata_text_to_rml( title );
}

void set_footer( const std::string &footer )
{
    g_footer = cata_text_to_rml( footer );
}

void set_grid( const std::vector<std::string> &rows )
{
    g_rows.clear();
    g_rows.reserve( rows.size() );
    for( const std::string &r : rows ) {
        g_rows.push_back( mg_row{ cata_text_to_rml( r ) } );
    }
}

void sync()
{
    if( g_handle ) {
        g_handle.DirtyAllVariables();
    }
}

}  // namespace minigame_rml
