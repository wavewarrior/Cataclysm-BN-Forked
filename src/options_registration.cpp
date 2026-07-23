#include "options.h"

#include <algorithm>
#include <locale>
#include <cfloat>
#include <climits>
#include <iterator>
#include <stdexcept>

#include "calendar.h"
#include "cached_options.h"
#include "cached_item_options.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "cursesport.h"
#include "debug.h"
#include "enum_bitset.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "game_constants.h"
#include "input.h"
#include "json.h"
#include "language.h"
#include "line.h"
#include "mapsharing.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "popup.h"
#include "sdlsound.h"
#include "sdltiles.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "title_screen.h"
#include "translations.h"
#include "ui_manager.h"
#include "worldfactory.h"

#include <RmlUi/Core.h>
#include "rml_screen.h"
#include "rml_util.h"

#include "cata_tiles.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <sstream>
#include <string>

#include "options.h"

constexpr auto general = "general";
constexpr auto interface = "interface";
constexpr auto graphics = "graphics";
constexpr auto performance = "performance";
constexpr auto world_default = "world_default";
constexpr auto debug = "debug";
constexpr auto coop = "coop";

struct debug_log_level {
    DL id;
    std::string opt_id;
    std::string opt_name;
    std::string opt_descr;
    bool opt_default;
};
struct debug_log_class {
    DC id;
    std::string opt_id;
    std::string opt_name;
    std::string opt_descr;
    bool opt_default;
};
static const std::vector<debug_log_level> debug_log_levels = { {
        { DL::Debug, "DEBUGLOG_LEV_DEBUG", translate_marker( "Debug" ), translate_marker( "Debug information" ), false },
        { DL::Info, "DEBUGLOG_LEV_INFO", translate_marker( "Info" ), translate_marker( "General information" ), true },
        { DL::Warn, "DEBUGLOG_LEV_WARNING", translate_marker( "Warning" ), translate_marker( "Warnings" ), true },
    }
};
static const std::vector<debug_log_class> debug_log_classes = { {
        { DC::Game, "DEBUGLOG_CL_GAME", translate_marker( "Game" ), translate_marker( "Messages from main game class." ), false },
        { DC::DebugModeMsg, "DEBUGLOG_CL_DEBUGMODE", translate_marker( "Debug mode" ), translate_marker( "Debug-type messages from in-game message log (when debug mode is enabled)." ), false },
        { DC::Main, "DEBUGLOG_CL_MAIN", translate_marker( "Main" ), translate_marker( "Generic messages related to game startup and operation." ), true },
        { DC::Map, "DEBUGLOG_CL_MAP", translate_marker( "Map" ), translate_marker( "Messages related to map and mapbuffer (map.cpp, mapbuffer.cpp)." ), false },
        { DC::MapGen, "DEBUGLOG_CL_MAPGEN", translate_marker( "Mapgen" ), translate_marker( "Messages related to mapgen (mapgen*.cpp) and overmap (overmap.cpp)." ), false },
        { DC::MapMem, "DEBUGLOG_CL_MAPMEM", translate_marker( "Map memory" ), translate_marker( "Messages related to tile memory (map_memory.cpp)." ), false },
        { DC::NPC, "DEBUGLOG_CL_NPC", translate_marker( "NPC" ), translate_marker( "Messages related to NPCs (npcs*.cpp)." ), false },
        { DC::SDL, "DEBUGLOG_CL_SDL", translate_marker( "SDL" ), translate_marker( "Messages related to SDL, tiles, tilesets and sound." ), false },
        { DC::Lua, "DEBUGLOG_CL_LUA", translate_marker( "Lua" ), translate_marker( "Messages from Lua scripts." ), true },
    }
};

void options_manager::add_options_general()
{
    const auto add_empty_line = [&]() {
        this->add_empty_line( general );
    };

    add( "PROMPT_ON_CHARACTER_DEATH", general, translate_marker( "Prompt on character death" ),
         translate_marker( "If enabled, when your character dies, the player is given a prompt that gives the option to reload the last saved game instead of dying." ),
         false
       );

    add_empty_line();

    add( "DEF_CHAR_NAME", general, translate_marker( "Default character name" ),
         translate_marker( "Set a default character name that will be used instead of a random name on character creation." ),
         "", 30
       );

    add( "DEF_CHAR_GENDER", general, translate_marker( "Default character gender" ),
    translate_marker( "Set a default character gender that will be used on character creation." ), {
        { "male", to_translation( "Male" )},
        { "female", to_translation( "Female" )},
    }, "male" );

    add_empty_line();

    add_option_group( general, Group( "comestible_merging",
                                      to_translation( "Merge similar comestibles" ),
                                      to_translation( "Configure how similar items are stacked." ) ),
    [&]( auto & page_id ) {
        add( "MERGE_COMESTIBLES", page_id, translate_marker( "Merging Mode" ),
        translate_marker( "Merge similar comestibles.  Legacy: default behavior.  Liquid: Merge only liquid comestibles.  All: Merge all comestibles." ), {
            { "legacy", to_translation( "Legacy" ) },
            { "liquid", to_translation( "Liquid" ) },
            { "all", to_translation( "All" ) }
        }, "all" );

        add( "MERGE_COMESTIBLES_THRESHOLD", general, translate_marker( "Freshness similarity threshold" ),
             translate_marker( "Limit maximum allowed staleness difference when merging comestibles."
                               "  The lower the value, the more similar the items must be to merge."
                               "  0.0: Only merge identical items."
                               "  1.0: Merge comestibles regardless of its freshness."
                             ),
             0.0, 1.0, 0.25, 0.05 );

        get_option( "MERGE_COMESTIBLES_THRESHOLD" ).setPrerequisites( "MERGE_COMESTIBLES", {"liquid", "all"} );
    } );

    add_empty_line();

    add( "AUTO_PICKUP", general, translate_marker( "Auto pickup enabled" ),
         translate_marker( "Enable item auto pickup.  Change pickup rules with the Auto Pickup Manager." ),
         false
       );

    add( "AUTO_PICKUP_ADJACENT", general, translate_marker( "Auto pickup adjacent" ),
         translate_marker( "If true, will enable to pickup items one tile around to the player.  You can assign No Auto Pickup zones with the Zones Manager 'Y' key for e.g.  your homebase." ),
         false
       );

    get_option( "AUTO_PICKUP_ADJACENT" ).setPrerequisite( "AUTO_PICKUP" );

    add( "AUTO_PICKUP_WEIGHT_LIMIT", general, translate_marker( "Auto pickup weight limit" ),
         translate_marker( "Auto pickup items with weight less than or equal to [option] * 50 grams.  You must also set the small items option.  '0' disables this option" ),
         0, 20, 0
       );

    get_option( "AUTO_PICKUP_WEIGHT_LIMIT" ).setPrerequisite( "AUTO_PICKUP" );

    add( "AUTO_PICKUP_VOL_LIMIT", general, translate_marker( "Auto pickup volume limit" ),
         translate_marker( "Auto pickup items with volume less than or equal to [option] * 50 milliliters.  You must also set the light items option.  '0' disables this option" ),
         0, 20, 0
       );

    get_option( "AUTO_PICKUP_VOL_LIMIT" ).setPrerequisite( "AUTO_PICKUP" );

    add( "AUTO_PICKUP_SAFEMODE", general, translate_marker( "Auto pickup safe mode" ),
         translate_marker( "Auto pickup is disabled as long as you can see monsters nearby.  This is affected by 'Safe Mode proximity distance'." ),
         false
       );

    get_option( "AUTO_PICKUP_SAFEMODE" ).setPrerequisite( "AUTO_PICKUP" );

    add( "NO_AUTO_PICKUP_ZONES_LIST_ITEMS", general,
         translate_marker( "List items within no auto pickup zones" ),
         translate_marker( "If false, you will not see messages about items, you step on, within no auto pickup zones." ),
         true
       );

    get_option( "NO_AUTO_PICKUP_ZONES_LIST_ITEMS" ).setPrerequisite( "AUTO_PICKUP" );

    add_empty_line();

    add( "AUTO_FEATURES", general, translate_marker( "Additional auto features" ),
         translate_marker( "If true, enables configured auto features below.  Disabled as long as any enemy monster is seen." ),
         false
       );

    add( "AUTO_PULP_BUTCHER", general, translate_marker( "Auto pulp or butcher" ),
         translate_marker( "Action to perform when 'Auto pulp or butcher' is enabled.  Pulp: Pulp corpses you stand on.  - Pulp Adjacent: Also pulp corpses adjacent from you.  - Butcher: Butcher corpses you stand on." ),
    { { "off", to_translation( "options", "Disabled" ) }, { "pulp", translate_marker( "Pulp" ) }, { "pulp_adjacent", translate_marker( "Pulp Adjacent" ) }, { "butcher", translate_marker( "Butcher" ) } },
    "off"
       );

    get_option( "AUTO_PULP_BUTCHER" ).setPrerequisite( "AUTO_FEATURES" );

    add( "AUTO_MINING", general, translate_marker( "Auto mining" ),
         translate_marker( "If true, enables automatic use of wielded pickaxes and jackhammers whenever trying to move into mineable terrain." ),
         false
       );

    get_option( "AUTO_MINING" ).setPrerequisite( "AUTO_FEATURES" );

    add( "AUTO_FORAGING", general, translate_marker( "Auto foraging" ),
         translate_marker( "Action to perform when 'Auto foraging' is enabled.  Bushes: Only forage bushes.  - Trees: Only forage trees.  - Everything: Forage bushes, trees, and everything else including flowers, cattails etc." ),
    { { "off", to_translation( "options", "Disabled" ) }, { "bushes", translate_marker( "Bushes" ) }, { "trees", translate_marker( "Trees" ) }, { "flowers", translate_marker( "Flowers" ) }, { "both", translate_marker( "Everything" ) } },
    "off"
       );

    get_option( "AUTO_FORAGING" ).setPrerequisite( "AUTO_FEATURES" );

    add_empty_line();

    add( "DANGEROUS_PICKUPS", general, translate_marker( "Dangerous pickups" ),
         translate_marker( "If false, will cause player to drop new items that cause them to exceed the weight limit." ),
         false
       );

    add( "DANGEROUS_TERRAIN_WARNING_PROMPT", general,
         translate_marker( "Dangerous terrain warning prompt" ),
         translate_marker( "Always: You will be prompted to move onto dangerous tiles.  Running: You will only be able to move onto dangerous tiles while running and will be prompted.  Crouching: You will only be able to move onto a dangerous tile while crouching and will be prompted.  Never:  You will not be able to move onto a dangerous tile unless running and will not be warned or prompted.  Ignore:  You will be able to move onto a dangerous tile without any warnings or prompts." ),
    {
        { "ALWAYS", to_translation( "Always" ) },
        { "RUNNING", translate_marker( "Running" ) },
        { "CROUCHING", translate_marker( "Crouching" ) },
        { "NEVER", translate_marker( "Never" ) },
        { "IGNORE", translate_marker( "Ignore" ) }
    },
    "ALWAYS"
       );

    add_empty_line();

    add( "SAFEMODE", general, translate_marker( "Safe mode" ),
         translate_marker( "If true, will hold the game and display a warning if a hostile monster/npc is approaching." ),
         true
       );

    add( "SAFEMODEPROXIMITY", general, translate_marker( "Safe mode proximity distance" ),
         translate_marker( "If safe mode is enabled, distance to hostiles at which safe mode should show a warning.  0 = Max player view distance.  This option only has effect when no safe mode rule is specified.  Otherwise, edit the default rule in Safe Mode Manager instead of this value." ),
         0, MAX_VIEW_DISTANCE, 0
       );

    add( "SAFEMODEVEH", general, translate_marker( "Safe mode when driving" ),
         translate_marker( "When true, safe mode will alert you of hostiles while you are driving a vehicle." ),
         false
       );

    add( "AUTOSAFEMODE", general, translate_marker( "Auto reactivate safe mode" ),
         translate_marker( "If true, safe mode will automatically reactivate after a certain number of turns.  See option 'Turns to auto reactivate safe mode.'" ),
         false
       );

    add( "AUTOSAFEMODETURNS", general, translate_marker( "Turns to auto reactivate safe mode" ),
         translate_marker( "Number of turns after which safe mode is reactivated.  Will only reactivate if no hostiles are in 'Safe mode proximity distance.'" ),
         1, 600, 50
       );

    add( "SAFEMODEIGNORETURNS", general, translate_marker( "Turns to remember ignored monsters" ),
         translate_marker( "Number of turns an ignored monster stays ignored after it is no longer seen.  0 disables this option and monsters are permanently ignored." ),
         0, 3600, 200
       );

    add( "QUERY_BEFORE_ATTACKING_NEUTRAL", general,
         translate_marker( "Query before attacking neutral monsters" ),
         translate_marker( "If true, you will be prompted to confirm before attacking neutral or fleeing monsters that you have yet to engage in combat with." ),
         true
       );

    add_empty_line();

    add_option_group( general, Group( "clothing_destruction_popup",
                                      to_translation( "Clothing destruction popup" ),
                                      to_translation( "Configure when popups appear due to clothing being destroyed." ) ),
    [&]( auto & page_id ) {
        add( "CLOTHING_DESTRUCTION_POPUP", page_id, translate_marker( "Enable popup" ),
             translate_marker( "If true, a popup will display when a piece of the player/NPC's worn clothing is destroyed." ),
             true );

        add( "CLOTHING_DESTRUCTION_POPUP_CONTENTS", page_id, translate_marker( "Only if contents present" ),
             translate_marker( "Only show popup if destroyed clothing has contents." ),
             false );

        add( "CLOTHING_DESTRUCTION_POPUP_MIN_WEIGHT", page_id,
             translate_marker( "Min weight for popup (g)" ),
             translate_marker( "Minimum weight of the item for the popup to trigger." ),
             0, 1000000, 0 );

        add( "CLOTHING_DESTRUCTION_POPUP_MIN_VOLUME", page_id,
             translate_marker( "Min volume for popup (ml)" ),
             translate_marker( "Minimum volume of the item for the popup to trigger." ),
             0, 1000000, 0 );
    } );

    add_empty_line();

    add( "TURN_DURATION", general, translate_marker( "Realtime turn progression" ),
         translate_marker( "If enabled, monsters will take periodic gameplay turns.  This value is the delay between each turn, in seconds.  Works best with Safe Mode disabled.  0 = disabled." ),
         0.0, 10.0, 0.0, 0.05
       );

    add_empty_line();

    add( "AUTOSAVE", general, translate_marker( "Autosave" ),
         translate_marker( "If true, game will periodically save the map.  Autosaves occur based on in-game turns or real-time minutes, whichever is larger." ),
         true
       );

    add( "AUTOSAVE_TURNS", general, translate_marker( "Game turns between autosaves" ),
         translate_marker( "Number of game turns between autosaves" ),
         10, 1000, 50
       );

    get_option( "AUTOSAVE_TURNS" ).setPrerequisite( "AUTOSAVE" );

    add( "AUTOSAVE_MINUTES", general, translate_marker( "Real minutes between autosaves" ),
         translate_marker( "Number of real time minutes between autosaves" ),
         0, 127, 5
       );

    get_option( "AUTOSAVE_MINUTES" ).setPrerequisite( "AUTOSAVE" );

    add_empty_line();

    add( "AUTO_NOTES", general, translate_marker( "Auto notes" ),
         translate_marker( "If true, automatically sets notes" ),
         true
       );

    add( "AUTO_NOTES_STAIRS", general, translate_marker( "Auto notes (stairs)" ),
         translate_marker( "If true, automatically sets notes on places that have stairs that go up or down" ),
         false
       );

    get_option( "AUTO_NOTES_STAIRS" ).setPrerequisite( "AUTO_NOTES" );

    add( "AUTO_NOTES_MAP_EXTRAS", general, translate_marker( "Auto notes (map extras)" ),
         translate_marker( "If true, automatically sets notes on places that contain various map extras" ),
         true
       );

    get_option( "AUTO_NOTES_MAP_EXTRAS" ).setPrerequisite( "AUTO_NOTES" );

    add( "AUTO_NOTES_DROPPED_FAVORITES", "general",
         translate_marker( "Auto notes (dropped favorites)" ),
         translate_marker( "If true, automatically sets notes when player drops favorited items." ),
         true
       );

    get_option( "AUTO_NOTES_DROPPED_FAVORITES" ).setPrerequisite( "AUTO_NOTES" );

    add_empty_line();

    add( "CIRCLEDIST", general, translate_marker( "Circular distances" ),
         translate_marker( "If true, the game will calculate range in a realistic way: light sources will be circles, diagonal movement will cover more ground and take longer.  If disabled, everything is square: moving to the northwest corner of a building takes as long as moving to the north wall." ),
         true
       );

    add( "DROP_EMPTY", general, translate_marker( "Drop empty containers" ),
         translate_marker( "Set to drop empty containers after use.  No: Don't drop any.  - Watertight: All except watertight containers.  - All: Drop all containers." ),
    { { "no", translate_marker( "No" ) }, { "watertight", translate_marker( "Watertight" ) }, { "all", translate_marker( "All" ) } },
    "no"
       );

    add( "DEATHCAM", general, translate_marker( "DeathCam" ),
         translate_marker( "Always: Always start deathcam.  Ask: Query upon death.  Never: Never show deathcam." ),
    { { "always", translate_marker( "Always" ) }, { "ask", translate_marker( "Ask" ) }, { "never", translate_marker( "Never" ) } },
    "ask"
       );

    add_empty_line();

    add( "SOUND_ENABLED", general, translate_marker( "Sound Enabled" ),
         translate_marker( "If true, music and sound are enabled." ),
         true, COPT_NO_SOUND_HIDE
       );

    add( "SOUNDPACKS", general, translate_marker( "Choose soundpack" ),
         translate_marker( "Choose the soundpack you want to use.  Requires restart." ),
         build_soundpacks_list(), "basic", COPT_NO_SOUND_HIDE
       ); // populate the options dynamically

    get_option( "SOUNDPACKS" ).setPrerequisite( "SOUND_ENABLED" );

    add( "MUSIC_VOLUME", general, translate_marker( "Music volume" ),
         translate_marker( "Adjust the volume of the music being played in the background." ),
         0, 128, 100, COPT_NO_SOUND_HIDE
       );

    get_option( "MUSIC_VOLUME" ).setPrerequisite( "SOUND_ENABLED" );

    add( "SOUND_EFFECT_VOLUME", general, translate_marker( "Sound effect volume" ),
         translate_marker( "Adjust the volume of sound effects being played by the game." ),
         0, 128, 100, COPT_NO_SOUND_HIDE
       );

    get_option( "SOUND_EFFECT_VOLUME" ).setPrerequisite( "SOUND_ENABLED" );

    add( "AMBIENT_SOUND_VOLUME", general, translate_marker( "Ambient sound volume" ),
         translate_marker( "Adjust the volume of ambient sounds being played by the game." ),
         0, 128, 100, COPT_NO_SOUND_HIDE
       );

    get_option( "AMBIENT_SOUND_VOLUME" ).setPrerequisite( "SOUND_ENABLED" );
    add( "ENABLE_TTS", general, translate_marker( "Enable TTS" ),
         translate_marker( "If true, text-to-speech synthesis is enabled for NPC dialogue." ),
         false, COPT_NO_SOUND_HIDE
       );

    get_option( "ENABLE_TTS" ).setPrerequisite( "SOUND_ENABLED" );
}

void options_manager::add_options_interface()
{
    const auto add_empty_line = [&]() {
        this->add_empty_line( interface );
    };

    std::vector<options_manager::id_and_option> lang_options = {
        { "", translate_marker( "System language" ) },
    };
    for( const language_info &info : list_available_languages() ) {
        lang_options.emplace_back( info.id, no_translation( info.name ) );
    }

    add( "USE_LANG", interface, translate_marker( "Language" ),
         translate_marker( "Switch Language." ), lang_options, "" );

    add_empty_line();

    add( title_screen::option_id, interface, translate_marker( "Title screen" ),
         translate_marker( "Title screen ASCII art to display on the main menu." ),
         title_screen::get_all_options(), title_screen::default_option_id );

    add_empty_line();

    add( "HEALTH_STYLE", interface, translate_marker( "Health Display Style" ),
    translate_marker( "Switch health-related display styling such as HP and hunger" ), {
        {"number", translate_marker( "Numerical" ) },
        {"bar", translate_marker( "Bar" ) },
        {"bar_alt", translate_marker( "Bar (Alt)" ) },
        {"bar_ascii", translate_marker( "Bar (Old)" ) },
    },
    "bar" );

    add_empty_line();

    add( "WIKI_DOC_URL", interface, translate_marker( "Wiki URL" ),
         translate_marker( "The URL opened by pressing the open wiki keybind." ),
         "https://docs.cataclysmbn.org", 60
       );

    add( "HHG_URL", interface, translate_marker( "Hitchhiker's Guide URL" ),
         translate_marker( "The URL opened by pressing the open HHG keybind." ),
         "https://cataclysmbn-guide.com", 60
       );

    add_empty_line();

    add( "USE_CELSIUS", interface, translate_marker( "Temperature units" ),
         translate_marker( "Switch between Celsius, Fahrenheit and Kelvin." ),
    { { "celsius", translate_marker( "Celsius" ) }, { "fahrenheit", translate_marker( "Fahrenheit" ) }, { "kelvin", translate_marker( "Kelvin" ) } },
    "celsius"
       );

    add( "USE_METRIC_SPEEDS", interface, translate_marker( "Speed units" ),
         translate_marker( "Switch between km/h, mph and tiles/turn." ),
    { { "km/h", translate_marker( "km/h" ) }, { "mph", translate_marker( "mph" ) }, { "t/t", translate_marker( "tiles/turn" ) } },
    "km/h"
       );

    add( "USE_METRIC_WEIGHTS", interface, translate_marker( "Mass units" ),
         translate_marker( "Switch between kg and lbs." ),
    {  { "kg", translate_marker( "kg" ) }, { "lbs", translate_marker( "lbs" ) } }, "kg"
       );

    add( "VOLUME_UNITS", interface, translate_marker( "Volume units" ),
         translate_marker( "Switch between the Liter ( L ), Cup ( c ), or Quart ( qt )." ),
    {  { "l", translate_marker( "Liter" ) }, { "c", translate_marker( "Cup" ) }, { "qt", translate_marker( "Quart" ) } },
    "l"
       );
    add( "DISTANCE_UNITS", interface, translate_marker( "Distance units" ),
         translate_marker( "Metric or Imperial" ),
    { { "metric", translate_marker( "Metric" ) }, { "imperial", translate_marker( "Imperial" ) } },
    "metric" );

    add(
        "OVERMAP_COORDINATE_FORMAT",
        interface,
        translate_marker( "Overmap coordinates format" ),
        translate_marker( "Are overmap coordinates displayed using absolute format like 338, 416 or subdivided into two components like 1'158, 2'56?" ),
    { { "subdivided", translate_marker( "Subdivided" ) }, { "absolute", translate_marker( "Absolute" ) } },
    "absolute"
    );

    add( "24_HOUR", interface, translate_marker( "Time format" ),
         translate_marker( "12h: AM/PM, e.g. 7:31 AM - Military: 24h Military, e.g. 0731 - 24h: Normal 24h, e.g. 7:31" ),
         //~ 12h time, e.g.  11:59pm
    {   { "12h", translate_marker( "12h" ) },
        //~ Military time, e.g.  2359
        { "military", translate_marker_context( "time format", "Military" ) },
        //~ 24h time, e.g.  23:59
        { "24h", translate_marker( "24h" ) }
    },
    "12h" );

    add_empty_line();

    add( "USE_PINYIN_SEARCH", interface, translate_marker( "Use pinyin in search" ),
         translate_marker( "If true, pinyin can be used in searching and filtering Chinese text.  "
                           "May slow down searches with many entries." ),
         false
       );

    add( "FORCE_CAPITAL_YN", interface, translate_marker( "Force Y/N in prompts" ),
         translate_marker( "If true, Y/N prompts are case-sensitive and y and n are not accepted." ),
         true
       );

    add( "SNAP_TO_TARGET", interface, translate_marker( "Snap to target" ),
         translate_marker( "If true, automatically follow the crosshair when firing/throwing." ),
         false
       );

    add( "AIM_AFTER_FIRING", interface, translate_marker( "Reaim after firing" ),
         translate_marker( "If true, after firing automatically aim again if targets are available." ),
         true
       );

    add( "THROW_RADIAL_HOLD", interface, translate_marker( "Throw quick-slot hold mode" ),
         translate_marker( "If true, the throw quick-slot menu stays open only while the key is held.  If false, it toggles open/closed." ),
         true
       );

    add( "QUERY_DISASSEMBLE", interface, translate_marker( "Query on disassembly while butchering" ),
         translate_marker( "If true, will query before disassembling items while butchering." ),
         true
       );

    add( "QUERY_KEYBIND_REMOVAL", interface, translate_marker( "Query on keybinding removal" ),
         translate_marker( "If true, will query before removing a keybinding from a hotkey." ),
         true
       );

    add( "CLOSE_ADV_INV", interface, translate_marker( "Close advanced inventory on move all" ),
         translate_marker( "If true, will close the advanced inventory when the move all items command is used." ),
         false
       );

    add( "OPEN_DEFAULT_ADV_INV", interface,
         translate_marker( "Open default advanced inventory layout" ),
         translate_marker( "Open default advanced inventory layout instead of last opened layout" ),
         false
       );

    add( "INV_USE_ACTION_NAMES", interface, translate_marker( "Display actions in Use Item menu" ),
         translate_marker( "If true, actions ( like \"Read\", \"Smoke\", \"Wrap tighter\" ) will be displayed next to the corresponding items." ),
         true
       );

    add( "VERBOSE_CRAFTING_SPEED_MODIFIERS", interface,
         translate_marker( "Verbose crafting/construction speed modifiers" ),
         translate_marker( "If true, show 100% crafting/construction speed modifiers in the info panels." ),
         false
       );

    add( "AUTOSELECT_SINGLE_VALID_TARGET", interface,
         translate_marker( "Autoselect if exactly one valid target" ),
         translate_marker( "If true, directional actions ( like \"Examine\", \"Open\", \"Pickup\" ) "
                           "will autoselect an adjacent tile if there is exactly one valid target." ),
         true
       );

    add_empty_line();

    add( "DIAG_MOVE_WITH_MODIFIERS_MODE", interface,
         translate_marker( "Diagonal movement with cursor keys and modifiers" ),
         /*
         Possible modes:

         # None

         # Mode 1: Numpad Emulation

         * Press and keep holding Ctrl
         * Press and release ↑ to set it as the modifier (until Ctrl is released)
         * Press and release → to get the move ↑ + → = ↗ i.e. just like pressing and releasing 9
         * Holding → results in repeated ↗, so just like holding 9
         * If I press any other direction, they are similarly modified by ↑, both for single presses and while holding.

         # Mode 2: CW/CCW

         * `Shift` + `Cursor Left` -> `7` = `Move Northwest`;
         * `Shift` + `Cursor Right` -> `3` = `Move Southeast`;
         * `Shift` + `Cursor Up` -> `9` = `Move Northeast`;
         * `Shift` + `Cursor Down` -> `1` = `Move Southwest`.

         and

         * `Ctrl` + `Cursor Left` -> `1` = `Move Southwest`;
         * `Ctrl` + `Cursor Right` -> `9` = `Move Northeast`;
         * `Ctrl` + `Cursor Up` -> `7` = `Move Northwest`;
         * `Ctrl` + `Cursor Down` -> `3` = `Move Southeast`.

         # Mode 3: L/R Tilt

         * `Shift` + `Cursor Left` -> `7` = `Move Northwest`;
         * `Ctrl` + `Cursor Left` -> `3` = `Move Southeast`;
         * `Shift` + `Cursor Right` -> `9` = `Move Northeast`;
         * `Ctrl` + `Cursor Right` -> `1` = `Move Southwest`.

         */
    translate_marker( "Allows diagonal movement with cursor keys using CTRL and SHIFT modifiers.  Diagonal movement action keys are taken from keybindings, so you need these to be configured." ), { { "none", translate_marker( "None" ) }, { "mode1", translate_marker( "Mode 1: Numpad Emulation" ) }, { "mode2", translate_marker( "Mode 2: CW/CCW" ) }, { "mode3", translate_marker( "Mode 3: L/R Tilt" ) } },
    "none", COPT_CURSES_HIDE );


    add( "SUGGEST_AUTOWALK_STAIRCASE", interface, translate_marker( "Suggest autowalk to staircases" ),
         translate_marker( "If true, upon pressing Ascend Stairs or Descend Stairs, the player will be prompted with an option to walk to the nearest visible staircase." ),
         true
       );

    add_empty_line();

    add( "VEHICLE_ARMOR_COLOR", interface, translate_marker( "Vehicle plating changes part color" ),
         translate_marker( "If true, vehicle parts will change color if they are armor plated" ),
         true
       );

    add( "DRIVING_VIEW_OFFSET", interface, translate_marker( "Auto-shift the view while driving" ),
         translate_marker( "If true, view will automatically shift towards the driving direction" ),
         true
       );

    add( "VEHICLE_DIR_INDICATOR", interface, translate_marker( "Draw vehicle facing indicator" ),
         translate_marker( "If true, when controlling a vehicle, a white 'X' ( in curses version ) or a crosshair ( in tiles version ) at distance 10 from the center will display its current facing." ),
         true
       );

    add( "REVERSE_STEERING", interface, translate_marker( "Reverse steering direction in reverse" ),
         translate_marker( "If true, when driving a vehicle in reverse, steering should also reverse like real life." ),
         false
       );

    add_empty_line();

    add( "SIDEBAR_POSITION", interface, translate_marker( "Sidebar position" ),
         translate_marker( "Switch between sidebar on the left or on the right side.  Requires restart." ),
         //~ sidebar position
    { { "left", translate_marker( "Left" ) }, { "right", translate_marker( "Right" ) } }, "right"
       );

    add( "SIDEBAR_SPACERS", interface, translate_marker( "Draw sidebar spacers" ),
         translate_marker( "If true, adds an extra space between sidebar panels." ),
         false
       );

    add( "LOG_FLOW", interface, translate_marker( "Message log flow" ),
         translate_marker( "Where new log messages should show." ),
         //~ sidebar/message log flow direction
    { { "new_top", translate_marker( "Top" ) }, { "new_bottom", translate_marker( "Bottom" ) } },
    "new_bottom"
       );

    add( "MESSAGE_TTL", interface, translate_marker( "Sidebar log message display duration" ),
         translate_marker( "Number of turns after which a message will be removed from the sidebar log.  '0' disables this option." ),
         0, 1000, 0
       );

    add( "MESSAGE_COOLDOWN", interface, translate_marker( "Message cooldown" ),
         translate_marker( "Number of turns during which similar messages are hidden.  '0' disables this option." ),
         0, 1000, 0
       );

    add( "MESSAGE_LIMIT", interface, translate_marker( "Limit message history" ),
         translate_marker( "Number of messages to preserve in the history, and when saving." ),
         1, 10000, 255
       );

    add( "NO_UNKNOWN_COMMAND_MSG", interface,
         translate_marker( "Suppress \"unknown command\" messages" ),
         translate_marker( "If true, pressing a key with no set function will not display a notice in the chat log." ),
         false
       );

    add( "LOOKAROUND_POSITION", interface, translate_marker( "Look around position" ),
         translate_marker( "Switch between look around panel being left or right." ),
    { { "left", translate_marker( "Left" ) }, { "right", translate_marker( "Right" ) } },
    "right"
       );

    add( "PICKUP_POSITION", interface, translate_marker( "Pickup position" ),
         translate_marker( "Switch between pickup panel being left, right, or overlapping the sidebar." ),
    { { "left", translate_marker( "Left" ) }, { "right", translate_marker( "Right" ) }, { "overlapping", translate_marker( "Overlapping" ) } },
    "left"
       );

    add( "ACCURACY_DISPLAY", interface, translate_marker( "Aim window display style" ),
         translate_marker( "How should confidence and steadiness be communicated to the player." ),
         //~ aim bar style - bars or numbers
    { { "numbers", translate_marker( "Numbers" ) }, { "bars", translate_marker( "Bars" ) } }, "bars"
       );

    add( "MORALE_STYLE", interface, translate_marker( "Morale style" ),
         translate_marker( "Morale display style in sidebar." ),
    { { "vertical", translate_marker( "Vertical" ) }, { "horizontal", translate_marker( "Horizontal" ) } },
    "Vertical"
       );

    add( "AIM_WIDTH", interface, translate_marker( "Full screen Advanced Inventory Manager" ),
         translate_marker( "If true, Advanced Inventory Manager menu will fit full screen, otherwise it will leave sidebar visible." ),
         false
       );

    add( "AIM_AUTORESET_FILTER", interface,
         translate_marker( "Advanced Inventory Manager Filter Resets" ),
         translate_marker( "If true, Advanced Inventory Manager filters will be reset when leaving the menu" ),
         false );

    add( "NEW_PICKUP_MENU", interface,
         translate_marker( "Use new pickup menu ui (EXPERIMENTAL)" ),
         translate_marker( "Whether to use the new or old pickup menu ui. WARNING: Is experimental feature" ),
         false );

    add_empty_line();

    add( "MOVE_VIEW_OFFSET", interface, translate_marker( "Move view offset" ),
         translate_marker( "Move view by how many squares per keypress." ),
         1, 50, 1
       );

    add( "FAST_SCROLL_OFFSET", interface, translate_marker( "Overmap fast scroll offset" ),
         translate_marker( "With Fast Scroll option enabled, shift view on the overmap and while looking around by this many squares per keypress." ),
         1, 50, 5
       );

    add( "MENU_SCROLL", interface, translate_marker( "Centered menu scrolling" ),
         translate_marker( "If true, menus will start scrolling in the center of the list, and keep the list centered." ),
         true
       );

    add( "SHIFT_LIST_ITEM_VIEW", interface, translate_marker( "Shift list item view" ),
         translate_marker( "Centered or to edge, shift the view toward the selected item if it is outside of your current viewport." ),
    { { "false", translate_marker( "False" ) }, { "centered", translate_marker( "Centered" ) }, { "edge", translate_marker( "To edge" ) } },
    "centered"
       );

    add( "AUTO_INV_ASSIGN", interface, translate_marker( "Auto inventory letters" ),
         translate_marker( "Enabled: automatically assign letters to any carried items that lack them.  Disabled: do not auto-assign letters.  "
    "Favorites: only auto-assign letters to favorited items." ), {
        { "disabled", translate_marker( "Disabled" ) },
        { "enabled", translate_marker( "Enabled" ) },
        { "favorites", translate_marker( "Favorites" ) }
    },
    "favorites" );

    add( "ITEM_HEALTH_BAR", interface, translate_marker( "Show item health bars" ),
         // NOLINTNEXTLINE(cata-text-style): one space after "etc."
         translate_marker( "If true, show item health bars instead of reinforced, scratched etc. text." ),
         true
       );

    add( "ITEM_SYMBOLS", interface, translate_marker( "Show item symbols" ),
         translate_marker( "If true, show item symbols in inventory and pick up menu." ),
         false
       );
    add( "HIGHLIGHT_UNREAD_RECIPES", interface, translate_marker( "Highlight unread recipes" ),
         translate_marker( "Highlight unread recipes to allow tracking of newly learned recipes." ),
         true
       );
    add( "ENABLE_NESTED_CATEGORIES", interface, translate_marker( "Enable nested crafting categories" ),
         translate_marker( "Show nested crafting categories in the crafting UI.  When disabled, nested recipes appear directly in their normal subcategories." ),
         true
       );
    add( "HIGHLIGHT_UNREAD_ITEMS", interface, translate_marker( "Highlight unread items" ),
         translate_marker( "Highlight unread items to allow tracking of newly discovered items." ),
         true
       );
    add( "AMMO_IN_NAMES", interface, translate_marker( "Add ammo to weapon/magazine names" ),
         translate_marker( "If true, the default ammo is added to weapon and magazine names.  For example \"Mosin-Nagant M44 (4/5)\" becomes \"Mosin-Nagant M44 (4/5 7.62x54mm)\"." ),
         true
       );

    add_empty_line();

    add( "ENABLE_JOYSTICK", interface, translate_marker( "Enable joystick" ),
         translate_marker( "Enable input from joystick." ),
         true, COPT_CURSES_HIDE
       );

    add( "HIDE_CURSOR", interface, translate_marker( "Hide mouse cursor" ),
         translate_marker( "Show: Cursor is always shown.  Hide: Cursor is hidden.  HideKB: Cursor is hidden on keyboard input and unhidden on mouse movement." ),
         //~ show mouse cursor
    {   { "show", translate_marker( "Show" ) },
        //~ hide mouse cursor
        { "hide", translate_marker( "Hide" ) },
        //~ hide mouse cursor when keyboard is used
        { "hidekb", translate_marker( "HideKB" ) }
    },
    "show", COPT_CURSES_HIDE );

    add( "EDGE_SCROLL", interface, translate_marker( "Edge scrolling" ),
    translate_marker( "Edge scrolling with the mouse." ), {
        std::make_tuple( -1, translate_marker( "Disabled" ) ),
        std::make_tuple( 100, translate_marker( "Slow" ) ),
        std::make_tuple( 30, translate_marker( "Normal" ) ),
        std::make_tuple( 10, translate_marker( "Fast" ) )
    },
    30, 30, COPT_CURSES_HIDE );

}

void options_manager::add_options_graphics()
{
    const auto add_empty_line = [&]() {
        this->add_empty_line( graphics );
    };

    add( "ANIMATIONS", graphics, translate_marker( "Animations" ),
         translate_marker( "If true, will display enabled animations." ),
         true
       );

    add( "ANIMATION_RAIN", graphics, translate_marker( "Rain animation" ),
         translate_marker( "If true, will display weather animations." ),
         true
       );

    get_option( "ANIMATION_RAIN" ).setPrerequisite( "ANIMATIONS" );

    add( "ANIMATION_PROJECTILES", graphics, translate_marker( "Projectile animation" ),
         translate_marker( "If true, will display animations for projectiles like bullets, arrows, and thrown items." ),
         true
       );

    get_option( "ANIMATION_PROJECTILES" ).setPrerequisite( "ANIMATIONS" );

    add( "ANIMATION_SCT", graphics, translate_marker( "SCT animation" ),
         translate_marker( "If true, will display scrolling combat text animations." ),
         true
       );

    get_option( "ANIMATION_SCT" ).setPrerequisite( "ANIMATIONS" );

    add( "ANIMATION_SCT_USE_FONT", graphics, translate_marker( "SCT with Unicode font" ),
         translate_marker( "If true, will display scrolling combat text with Unicode font." ),
         true
       );

    get_option( "ANIMATION_SCT_USE_FONT" ).setPrerequisite( "ANIMATION_SCT" );

    add( "ANIMATION_SCT_DAMAGE", graphics, translate_marker( "SCT damage numbers" ),
         translate_marker( "If true, will display floating damage numbers on hits." ),
         true
       );
    get_option( "ANIMATION_SCT_DAMAGE" ).setPrerequisite( "ANIMATION_SCT" );

    add( "ANIMATION_SCT_OUTCOMES", graphics, translate_marker( "SCT outcome indicators" ),
         translate_marker( "If true, will display MISS/DODGE/PARRY/BLOCK text on defensive outcomes." ),
         true
       );
    get_option( "ANIMATION_SCT_OUTCOMES" ).setPrerequisite( "ANIMATION_SCT" );

    add( "ANIMATION_SCT_CRITICALS", graphics, translate_marker( "SCT critical highlights" ),
         translate_marker( "If true, critical hits will be highlighted with larger size and gold color." ),
         true
       );
    get_option( "ANIMATION_SCT_CRITICALS" ).setPrerequisite( "ANIMATION_SCT" );

    add( "ANIMATION_SCT_TYPE_COLORS", graphics, translate_marker( "SCT damage type colors" ),
         translate_marker( "If true, damage numbers will be colored by damage type (bash=white, cut=cyan, stab=red-orange, etc.)." ),
         true
       );
    get_option( "ANIMATION_SCT_TYPE_COLORS" ).setPrerequisite( "ANIMATION_SCT" );

    add( "ANIMATION_SCT_COLORBLIND", graphics, translate_marker( "SCT colorblind mode" ),
         translate_marker( "If true, adds small damage type abbreviations next to numbers for colorblind accessibility." ),
         false
       );
    get_option( "ANIMATION_SCT_COLORBLIND" ).setPrerequisite( "ANIMATION_SCT" );

    add( "ANIMATION_SCT_MAX_ENTRIES", graphics, translate_marker( "SCT max entries" ),
         translate_marker( "Maximum simultaneous SCT entries. Oldest are removed first when exceeded." ),
         5, 30, 15
       );

    add( "ANIMATION_SCT_JITTER", graphics, translate_marker( "SCT position jitter range" ),
         translate_marker( "Position jitter range in tiles for SCT entries (0 = none). Higher values spread entries apart." ),
         0, 4, 2
       );
    get_option( "ANIMATION_SCT_JITTER" ).setPrerequisite( "ANIMATION_SCT" );

    add( "ANIMATION_SCT_SPEED", graphics, translate_marker( "SCT animation speed" ),
         translate_marker( "Animation speed multiplier for SCT entries (5-20). Higher values scroll faster." ),
         5, 20, 10
       );
    get_option( "ANIMATION_SCT_SPEED" ).setPrerequisite( "ANIMATION_SCT" );

    add( "ANIMATION_DELAY", graphics, translate_marker( "Animation delay" ),
         translate_marker( "The amount of time to pause between animation frames in ms." ),
         0, 100, 10
       );

    get_option( "ANIMATION_DELAY" ).setPrerequisite( "ANIMATIONS" );

    add( "SKIP_EXPLOSION_ANIMATION_AFTER", graphics,
         translate_marker( "Maximum rendered explosions per turn" ),
         translate_marker( "Skip rendering explosions after this many count per turn to prevent softlocks from chain reactions. Set to 0 to disable." ),
         0, 100, 10
       );

    get_option( "SKIP_EXPLOSION_ANIMATION_AFTER" ).setPrerequisite( "ANIMATIONS" );

    add( "SPRITE_ANIMATIONS", graphics, translate_marker( "Sprite animations" ),
         translate_marker( "If true, sprites bob when moving, breathe when idle, flinch when hit, and lunge when attacking (tiles renderer only)." ),
         true
       );
    get_option( "SPRITE_ANIMATIONS" ).setPrerequisite( "ANIMATIONS" );

    add( "SPRITE_MOVE_BOB", graphics, translate_marker( "Sprite movement bob" ),
         translate_marker( "If true, sprites slide and bob in their direction of movement." ),
         true
       );
    get_option( "SPRITE_MOVE_BOB" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_BOB_AMPLITUDE", graphics, translate_marker( "Movement bob amplitude" ),
         translate_marker( "Height in pixels of the movement bounce." ),
         0.0, 10.0, 4.0, 0.5
       );
    get_option( "SPRITE_BOB_AMPLITUDE" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_BOB_DURATION", graphics, translate_marker( "Movement bob duration" ),
         translate_marker( "Seconds the movement bounce takes to settle." ),
         0.1, 1.0, 0.3, 0.05
       );
    get_option( "SPRITE_BOB_DURATION" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_BREATHING", graphics, translate_marker( "Sprite idle sway" ),
         translate_marker( "If true, standing sprites shift their weight foot to foot." ),
         true
       );
    get_option( "SPRITE_BREATHING" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_IDLE_SWAY", graphics, translate_marker( "Idle sway amount" ),
         translate_marker( "Pixels a standing sprite sways from side to side." ),
         0.0, 4.0, 1.5, 0.25
       );
    get_option( "SPRITE_IDLE_SWAY" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_HIT_REACTION", graphics, translate_marker( "Sprite hit reaction" ),
         translate_marker( "If true, sprites flash and recoil when they take damage (you flash white, others red)." ),
         true
       );
    get_option( "SPRITE_HIT_REACTION" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_HIT_FLASH_INTENSITY", graphics, translate_marker( "Hit flash intensity" ),
         translate_marker( "Brightness of the damage flash." ),
         0.0, 2.0, 1.0, 0.1
       );
    get_option( "SPRITE_HIT_FLASH_INTENSITY" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_HIT_PUSH", graphics, translate_marker( "Hit kickback distance" ),
         translate_marker( "Pixels a sprite is knocked back when hit." ),
         0.0, 20.0, 6.0, 1.0
       );
    get_option( "SPRITE_HIT_PUSH" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_HIT_DURATION", graphics, translate_marker( "Hit reaction duration" ),
         translate_marker( "Seconds a single hit reaction takes to settle." ),
         0.1, 1.0, 0.25, 0.05
       );
    get_option( "SPRITE_HIT_DURATION" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_ATTACK_LUNGE", graphics, translate_marker( "Sprite attack lunge" ),
         translate_marker( "If true, sprites lunge forward on melee and recoil on ranged attacks." ),
         true
       );
    get_option( "SPRITE_ATTACK_LUNGE" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_ATTACK_AMPLITUDE", graphics, translate_marker( "Attack lunge amplitude" ),
         translate_marker( "Pixels a sprite lunges when attacking." ),
         0.0, 15.0, 4.0, 1.0
       );
    get_option( "SPRITE_ATTACK_AMPLITUDE" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_ATTACK_DURATION", graphics, translate_marker( "Attack lunge duration" ),
         translate_marker( "Seconds the attack lunge takes to settle." ),
         0.1, 1.0, 0.2, 0.05
       );
    get_option( "SPRITE_ATTACK_DURATION" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "SPRITE_TILE_HIT", graphics, translate_marker( "Tile bash shake" ),
         translate_marker( "If true, furniture and terrain shake briefly when smashed." ),
         true
       );
    get_option( "SPRITE_TILE_HIT" ).setPrerequisite( "SPRITE_ANIMATIONS" );

    add( "BULLETS_AS_LASERS", graphics, translate_marker( "Draw bullets as lines" ),
         translate_marker( "If true, bullets are drawn as lines of images, and the animation lasts only one frame." ),
         true
       );

    add( "BLINK_SPEED", graphics, translate_marker( "Blinking effects speed" ),
         translate_marker( "The speed of every blinking effects in ms." ),
         100, 5000, 800
       );

    add( "FORCE_REDRAW", graphics, translate_marker( "Force redraw" ),
         translate_marker( "If true, forces the game to redraw at least once per turn." ),
         true
       );

    add( "NIGHT_VISION_DEFAULT_COLOR", graphics, translate_marker( "Night Vision Default Colors" ),
    translate_marker( "Choose from default night vision colors." ), {
        { "#2eab01", translate_marker( "Green" ) },
        { "#ff141c", translate_marker( "Red" ) },
        { "#888888", translate_marker( "Gray" ) },
        { "custom", translate_marker( "Custom" ) }
    }, "#2eab01" );

    add( "NIGHT_VISION_COLOR", graphics, translate_marker( "Night Vision Color" ),
         translate_marker( "Sets custom night vision color." ), "#2eab01", 60 );

    get_option( "NIGHT_VISION_COLOR" ).setPrerequisite( "NIGHT_VISION_DEFAULT_COLOR", "custom" );

    add( "ENHANCED_NIGHT_VISION_DEFAULT_COLOR", graphics,
         translate_marker( "Enhanced Night Vision Default Colors" ),
    translate_marker( "Choose from default night vision colors." ), {
        { "#6cf5e7", translate_marker( "White Phosphor" ) },
        { "#33e84e", translate_marker( "Green Phosphor" ) },
        { "#888888", translate_marker( "Gray" ) },
        { "custom", translate_marker( "Custom" ) }
    }, "#6cf5e7" );

    add( "ENHANCED_NIGHT_VISION_COLOR", graphics, translate_marker( "Enhanced Night Vision Color" ),
         translate_marker( "Sets custom night vision color." ), "#6cf5e7", 60 );

    get_option( "ENHANCED_NIGHT_VISION_COLOR" ).setPrerequisite( "ENHANCED_NIGHT_VISION_DEFAULT_COLOR",
            "custom" );

    add_empty_line();

    add( "TERMINAL_X", graphics, translate_marker( "Terminal width" ),
         translate_marker( "Set the size of the terminal along the X axis." ),
         80, 960, 80, COPT_POSIX_CURSES_HIDE
       );

    add( "TERMINAL_Y", graphics, translate_marker( "Terminal height" ),
         translate_marker( "Set the size of the terminal along the Y axis." ),
         24, 270, 24, COPT_POSIX_CURSES_HIDE
       );

    add_empty_line();

    add_option_group( graphics, Group( "font_params", to_translation( "Font settings" ),
                                       to_translation( "Font display settings.  To change font type or source file, edit fonts.json in config directory." ) ),
    [&]( auto & page_id ) {
        add( "USE_DRAW_ASCII_LINES_ROUTINE", page_id, translate_marker( "SDL ASCII lines" ),
             translate_marker( "Use SDL ASCII line drawing routine instead of Unicode Line Drawing characters.  Use this option when your selected font doesn't contain necessary glyphs." ),
             true, COPT_CURSES_HIDE );

        add( "FONT_BLENDING", page_id, translate_marker( "Font blending" ),
             translate_marker( "If true, fonts will look better." ),
             false, COPT_CURSES_HIDE );

        add( "FONT_WIDTH", page_id, translate_marker( "Font width" ),
             translate_marker( "Set the font width.  Requires restart." ),
             8, 100, 8, COPT_CURSES_HIDE );

        static auto font_size_options = std::array<std::array<std::string, 3>, 8> {{
                {"FONT_HEIGHT",         translate_marker( "Font height" ),         translate_marker( "Set the font height.  Requires restart." )},
                {"FONT_SIZE",           translate_marker( "Font size" ),           translate_marker( "Set the font size.  Requires restart." )},
                {"MAP_FONT_WIDTH",      translate_marker( "Map font width" ),      translate_marker( "Set the map font width.  Requires restart." )},
                {"MAP_FONT_HEIGHT",     translate_marker( "Map font height" ),     translate_marker( "Set the map font height.  Requires restart." )},
                {"MAP_FONT_SIZE",       translate_marker( "Map font size" ),       translate_marker( "Set the map font size.  Requires restart." )},
                {"OVERMAP_FONT_WIDTH",  translate_marker( "Overmap font width" ),  translate_marker( "Set the overmap font width.  Requires restart." )},
                {"OVERMAP_FONT_HEIGHT", translate_marker( "Overmap font height" ), translate_marker( "Set the overmap font height.  Requires restart." )},
                {"OVERMAP_FONT_SIZE",   translate_marker( "Overmap font size" ),   translate_marker( "Set the overmap font size.  Requires restart." )}
            }
        };
        for( auto &&[option, option_name, option_desc] : font_size_options ) {
            add( option, page_id, option_name, option_desc, 8, 100, 16, COPT_CURSES_HIDE );
        }
    } );

    add( "ENABLE_ASCII_ART_ITEM", graphics,
         translate_marker( "Enable ASCII art in item descriptions" ),
         translate_marker( "When available item description will show a picture of the item in ascii art." ),
         true, COPT_NO_HIDE
       );

    add_empty_line();

    // Tiles-only fork: the USE_TILES / USE_TILES_OVERMAP options are removed
    // (use_tiles / use_tiles_overmap are forced true in cached_options.cpp). The
    // tileset selectors and tiles-dependent options stay but no longer carry a
    // USE_TILES prerequisite. LOADING_SCREEN_IMAGES is dropped (its splash feature
    // was removed).
    add( "TILES", graphics, translate_marker( "Choose tileset" ),
         translate_marker( "Choose the tileset you want to use." ),
         build_tilesets_list(), "UNDEAD_PEOPLE_BASE", COPT_CURSES_HIDE
       ); // populate the options dynamically

    add( "OVERMAP_TILES", graphics, translate_marker( "Choose overmap tileset" ),
         translate_marker( "Choose the overmap tileset you want to use." ),
         build_tilesets_list(), "UNDEAD_PEOPLE_BASE", COPT_CURSES_HIDE
       ); // populate the options dynamically

    add( "VEHICLE_EDIT_TILES", graphics, translate_marker( "Graphical vehicle display" ),
         translate_marker( "If true, the vehicle interaction screen will display vehicle parts using graphical tiles instead of ASCII symbols." ),
         true, COPT_CURSES_HIDE
       );

    add( "USE_CHARACTER_PREVIEW", graphics, translate_marker( "Enable character preview window" ),
         translate_marker( "If true, shows character preview window in traits tab on character creation.  "
                           "While having a window press 'z'/'Z' to perform zoom-in/zoom-out.  "
                           "Press 'C' to toggle clothes preview" ),
         true, COPT_CURSES_HIDE
       );

    add_empty_line();

    add( "MEMORY_MAP_MODE", graphics, translate_marker( "Memory map drawing mode" ),
    translate_marker( "Specified the mode in which the memory map is drawn." ), {
        { "color_pixel_darken", translate_marker( "Darkened" ) },
        { "color_pixel_sepia", translate_marker( "Sepia" ) }
    }, "color_pixel_sepia", COPT_CURSES_HIDE
       );

    add( "STATICZEFFECT", graphics, translate_marker( "Static z level effect" ),
         translate_marker( "If true, lower z levels will look the same no matter how far down they are.  Increases rendering performance." ),
         false, COPT_CURSES_HIDE
       );

    add( "OVERMAP_TRANSPARENCY", graphics, translate_marker( "Overmap air transparent" ),
         translate_marker( "If true, overmap z levels with air are transparent, lower layers are rendered. Decreases rendering perfomance." ),
         true, COPT_CURSES_HIDE
       );

    add( "STATE_MODIFIERS", graphics, translate_marker( "Character state modifiers" ),
         translate_marker( "If true, enables tileset-defined character sprite modifications based on movement state (crouching, running, etc.)." ),
         true, COPT_CURSES_HIDE
       );


    add_empty_line();

    add( "PIXEL_MINIMAP", graphics, translate_marker( "Pixel minimap" ),
         translate_marker( "If true, shows the pixel-detail minimap in game after the save is loaded.  Use the 'Toggle Pixel Minimap' action key to change its visibility during gameplay." ),
         true, COPT_CURSES_HIDE
       );

    add( "PIXEL_MINIMAP_MODE", graphics, translate_marker( "Pixel minimap drawing mode" ),
    translate_marker( "Specified the mode in which the minimap drawn." ), {
        { "solid", translate_marker( "Solid" ) },
        { "squares", translate_marker( "Squares" ) },
        { "dots", translate_marker( "Dots" ) }
    }, "dots", COPT_CURSES_HIDE
       );

    get_option( "PIXEL_MINIMAP_MODE" ).setPrerequisite( "PIXEL_MINIMAP" );

    add( "PIXEL_MINIMAP_BRIGHTNESS", graphics, translate_marker( "Pixel minimap brightness" ),
         translate_marker( "Overall brightness of pixel-detail minimap." ),
         10, 300, 100, COPT_CURSES_HIDE
       );

    get_option( "PIXEL_MINIMAP_BRIGHTNESS" ).setPrerequisite( "PIXEL_MINIMAP" );

    add( "PIXEL_MINIMAP_HEIGHT", graphics, translate_marker( "Pixel minimap height" ),
         translate_marker( "Height of pixel-detail minimap, measured in terminal rows.  Set to 0 for default spacing." ),
         0, 100, 0, COPT_CURSES_HIDE
       );

    get_option( "PIXEL_MINIMAP_HEIGHT" ).setPrerequisite( "PIXEL_MINIMAP" );

    add( "PIXEL_MINIMAP_SCALE_TO_FIT", graphics, translate_marker( "Scale pixel minimap" ),
         translate_marker( "Scale pixel minimap to fit its surroundings.  May produce crappy results, especially in modes other than \"Solid\"." ),
         false, COPT_CURSES_HIDE
       );

    get_option( "PIXEL_MINIMAP_SCALE_TO_FIT" ).setPrerequisite( "PIXEL_MINIMAP" );

    add( "PIXEL_MINIMAP_RATIO", graphics, translate_marker( "Maintain pixel minimap aspect ratio" ),
         translate_marker( "Preserves the square shape of tiles shown on the pixel minimap." ),
         true, COPT_CURSES_HIDE
       );

    get_option( "PIXEL_MINIMAP_RATIO" ).setPrerequisite( "PIXEL_MINIMAP" );

    add( "PIXEL_MINIMAP_BEACON_SIZE", graphics,
         translate_marker( "Creature beacon size" ),
         translate_marker( "Controls how big the creature beacons are.  Value is in minimap tiles." ),
         1, 4, 2, COPT_CURSES_HIDE
       );

    get_option( "PIXEL_MINIMAP_BEACON_SIZE" ).setPrerequisite( "PIXEL_MINIMAP" );

    add( "PIXEL_MINIMAP_BLINK", graphics, translate_marker( "Hostile creature beacon blink speed" ),
         translate_marker( "Controls how fast the hostile creature beacons blink on the pixel minimap.  Value is multiplied by 200 ms.  Set to 0 to disable." ),
         0, 50, 10, COPT_CURSES_HIDE
       );

    get_option( "PIXEL_MINIMAP_BLINK" ).setPrerequisite( "PIXEL_MINIMAP" );

    add( "ZOOM_STEP_COUNT", graphics, translate_marker( "Zoom steps" ),
         translate_marker( "Number of steps between zoom levels." ),
         1, 7, 1, COPT_CURSES_HIDE );

    add_empty_line();

    std::vector<options_manager::id_and_option> display_list = cata_tiles::build_display_list();
    add( "DISPLAY", graphics, translate_marker( "Display" ),
         translate_marker( "Sets which video display will be used to show the game.  Requires restart." ),
         display_list,
         display_list.front().first, COPT_CURSES_HIDE );

    add( "FULLSCREEN", graphics, translate_marker( "Fullscreen" ),
         translate_marker( "Starts Cataclysm in one of the fullscreen modes.  Requires restart." ),
    { { "no", translate_marker( "No" ) }, { "maximized", translate_marker( "Maximized" ) }, { "fullscreen", translate_marker( "Fullscreen" ) }, { "windowedbl", translate_marker( "Windowed borderless" ) } },
    "windowedbl", COPT_CURSES_HIDE
       );

    add( "MINIMIZE_ON_FOCUS_LOSS", graphics,
         translate_marker( "Minimize on focus loss" ),
         translate_marker( "Minimize fullscreen window when it loses focus.  Requires restart." ), false );

    std::vector<options_manager::id_and_option> renderer_list = cata_tiles::build_renderer_list();
    std::string default_renderer = renderer_list.front().first;
#   if defined(_WIN32)
    for( const id_and_option &renderer : renderer_list ) {
        if( renderer.first == "direct3d11" ) {
            default_renderer = renderer.first;
            break;
        }
    }
#   endif
    add( "RENDERER", graphics, translate_marker( "Renderer" ),
         translate_marker( "Set which renderer to use.  Requires restart." ), renderer_list,
         default_renderer, COPT_CURSES_HIDE );

    // SDL_GPU backend for the lighting renderer (distinct from RENDERER above,
    // which is the legacy 2D mirror SDL_Renderer). "auto" lets SDL pick the
    // platform default (Direct3D 12 on Windows, Metal on macOS). Windows defaults
    // to Vulkan because some Direct3D 12 drivers reject the lighting shaders'
    // pipelines (SDL_shadercross reflection / root-signature mismatch); Vulkan is
    // unaffected. An unavailable choice (e.g. Vulkan on macOS) falls back to auto.
    std::string gpu_driver_default = "auto";
#   if defined(_WIN32)
    gpu_driver_default = "vulkan";
#   endif
    add( "GPU_DRIVER", graphics, translate_marker( "GPU rendering backend" ),
         translate_marker( "Backend for the SDL_GPU lighting renderer.  'auto' lets SDL choose.  Vulkan avoids some Direct3D 12 driver issues on Windows.  Requires restart." ),
    {
        { "auto", translate_marker( "Auto" ) },
        { "vulkan", translate_marker( "Vulkan" ) },
        { "direct3d12", translate_marker( "Direct3D 12" ) },
        { "metal", translate_marker( "Metal" ) },
    },
    gpu_driver_default, COPT_CURSES_HIDE );

#if defined(SDL_HINT_RENDER_BATCHING)
    add( "RENDER_BATCHING", graphics, translate_marker( "Allow render batching" ),
         translate_marker( "Use render batching for 2D render API to make it more efficient.  Requires restart." ),
         true, COPT_CURSES_HIDE
       );
#endif
    add( "FRAMEBUFFER_ACCEL", graphics, translate_marker( "Software framebuffer acceleration" ),
         translate_marker( "Use hardware acceleration for the framebuffer when using software rendering.  Requires restart." ),
         false, COPT_CURSES_HIDE
       );

#if defined(SDL_HINT_RENDER_VSYNC)
    add( "VSYNC", graphics, translate_marker( "Use VSync" ),
         translate_marker( "Enable vertical synchronization to prevent screen tearing.  VSync can slow the game down a lot.  Requires restart." ),
         true, COPT_CURSES_HIDE
       );
#endif

    get_option( "FRAMEBUFFER_ACCEL" ).setPrerequisite( "RENDERER", "software" );

    add( "USE_COLOR_MODULATED_TEXTURES", graphics, translate_marker( "Use color modulated textures" ),
         translate_marker( "If true, tries to use color modulated textures to speed-up ASCII drawing.  Requires restart." ),
         false, COPT_CURSES_HIDE
       );

    add( "SCALING_FACTOR", graphics, translate_marker( "Display scaling factor" ),
    translate_marker( "Factor by which to scale the game display, 1x means no scaling.  Requires restart." ), {
        { "1", translate_marker( "1x" ) },
        { "2", translate_marker( "2x" ) },
        { "4", translate_marker( "4x" ) }
    },
    "1", COPT_CURSES_HIDE );

}

void options_manager::add_options_performance()
{
    const auto add_empty_line = [&]() {
        this->add_empty_line( performance );
    };
    const static bool is_android = false;
    add_option_group( performance, Group( "rem_act_perf", to_translation( "Sleep Boost" ),
                                          to_translation( "Skip expensive processing while the player sleeps." ) ),
    [&]( auto & page_id ) {
        add( "SLEEP_SKIP_VEH", page_id, translate_marker( "Skip Vehicle Movement" ),
             translate_marker( "Turns off vehicle movement and autodrive while sleeping" ),
             true );
        add( "SLEEP_SKIP_SOUND", page_id, translate_marker( "Skip Sound Processing On Sleep" ),
             translate_marker( "Sounds are not processed while sleeping" ),
             false );
        add( "SLEEP_SKIP_MON", page_id, translate_marker( "Skip Monster Movement" ),
             translate_marker( "Monsters do not move while the player is sleeping" ),
             is_android ? false : true );
        add( "SLEEP_SKIP_NPC", page_id, translate_marker( "Skip NPC Movement" ),
             translate_marker( "NPCs are forced to sleep alongside the player, skipping movement "
                               "but still processing rest recovery (fatigue reduction, healing, etc.).  "
                               "NPCs with non-interruptible activities (e.g. surgery) are frozen "
                               "for the turn instead." ),
             is_android ? false : true );
    } );

    add_empty_line();

    add_option_group( performance, Group( "lod_monster", to_translation( "Monster LOD" ),
                                          to_translation( "Configure level-of-detail thresholds for monster AI." ) ),
    [&]( auto & page_id ) {
        add( "MONSTER_LOD_ENABLED", page_id,
             translate_marker( "Enable Monster LOD" ),
             translate_marker( "Enable level-of-detail processing for monsters.  "
                               "When enabled, distant or wandering monsters are assigned "
                               "AI tiers. Higher tiers are processed less often and skip certain functions.  "
                               "When disabled, every monster runs full AI every turn regardless of distance." ),
             true );
        add( "LOD_ACTION_BUDGET", page_id,
             translate_marker( "Action Budget" ),
             translate_marker( "Minimum number of monsters that enter the move loop per turn.  "
                               "The actual budget is the larger of this value and the current Tier-0 "
                               "(full-AI) monster count, so full-AI monsters are never skipped.  "
                               "Higher values process more distant monsters each turn.  "
                               "0 means only Tier-0 monsters run (no extra Tier-1 budget)." ),
             32, 2048, is_android ? 96 : 128 );
        add( "LOD_MACRO_INTERVAL", page_id,
             translate_marker( "Macro Step Interval" ),
             translate_marker( "How many turns elapse between movement steps for Tier-2 (distant wandering) "
                               "monsters.  At 1 they step every turn; at 3 (default) they step once every "
                               "3 turns.  Higher values reduce CPU cost for distant hordes." ),
             1, 8, is_android ? 3 : 4 );
        add( "LOD_TIER_FULL_DIST", page_id,
             translate_marker( "Full AI Radius" ),
             translate_marker( "Monsters within this radius run the complete AI every turn.  "
                               "Must be less than the Coarse AI Radius." ),
             5, 208, is_android ? 20 : 30 );
        add( "LOD_TIER_COARSE_DIST", page_id,
             translate_marker( "Coarse AI Radius" ),
             translate_marker( "Monsters between the Full AI Radius and this distance use cached "
                               "paths and skip expensive faction queries.  Monsters beyond this "
                               "distance are Tier-2 (macro step only)." ),
             10, 208, is_android ? 40 : 75 );
        add( "LOD_DEMOTION_COOLDOWN", page_id,
             translate_marker( "Demotion Cooldown" ),
             translate_marker( "Turns a monster must wait after being promoted to a higher-fidelity "
                               "tier before it can be demoted again.  Prevents rapid tier oscillation "
                               "at distance boundaries.  0 disables the cooldown." ),
             0, 10, 3 );
        add( "LOD_COARSE_SCENT_INTERVAL", page_id,
             translate_marker( "Coarse Scent Check Interval" ),
             translate_marker( "How many turns elapse between scent-tracking checks for Tier-1 (coarse) "
                               "monsters.  At 1 they check scent every turn (full fidelity); at 3 (default) "
                               "only once every 3 turns. " ),
             1, 5, is_android ? 3 : 4 );
        add( "LOD_GROUP_MORALE_MAX_TIER", page_id,
             translate_marker( "Group Morale Max Tier" ),
             translate_marker( "Highest LOD tier that participates in group-morale and swarming calculations.  "
                               "0 = Tier-0 only (default, cheapest).  1 = Tier-0 and Tier-1 monsters also "
                               "run group-morale/swarm checks. " ),
             0, 1, 0 );
        add( "ACTIVITY_SKIP_MONSTER_LOD_GATE", page_id,
             translate_marker( "Activity Skip Monster Gate" ),
             translate_marker( "Highest real monster LOD tier allowed to run activity-skip AI.  "
                               "Allowed monsters act one LOD tier less detailed than normal.  "
                               "0 lets only Tier-0 monsters act as Tier-1.  "
                               "1 lets Tier-0 and Tier-1 monsters act as Tier-1 and Tier-2, "
                               "which is the default.  2 also lets Tier-2 monsters run macro AI." ),
             0, 2, 1 );
        add( "LOD_Z_PENALTY", page_id,
             translate_marker( "Z-Level Distance Penalty" ),
             translate_marker( "Extra distance added per z-level when assigning monster AI tiers.  "
                               "Higher values push off-z monsters into coarser tiers faster.  "
                               "Monsters one floor away always keep full AI regardless of this value." ),
             0, 100, 16 );
        add( "LOD_LIFECYCLE_STRIDE", page_id,
             translate_marker( "Lifecycle Stride" ),
             translate_marker( "How many turns between lifecycle processing (item processing, effects, "
                               "field damage) for distant off-z Tier-2 monsters.  At 1 they process every "
                               "turn (disabled).  Higher values reduce CPU cost for off-z hordes on "
                               "field-free submaps.  Off-z monsters on field-containing submaps always "
                               "process every turn regardless of this setting." ),
             1, 10, is_android ? 6 : 4 );
    } );

    get_option( "LOD_ACTION_BUDGET" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "LOD_MACRO_INTERVAL" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "LOD_TIER_FULL_DIST" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "LOD_TIER_COARSE_DIST" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "LOD_DEMOTION_COOLDOWN" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "LOD_COARSE_SCENT_INTERVAL" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "LOD_GROUP_MORALE_MAX_TIER" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "ACTIVITY_SKIP_MONSTER_LOD_GATE" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "LOD_Z_PENALTY" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "LOD_LIFECYCLE_STRIDE" ).setPrerequisite( "MONSTER_LOD_ENABLED" );

    add_option_group( performance, Group( "lod_npc", to_translation( "NPC LOD" ),
                                          to_translation( "Configure level-of-detail thresholds for NPC AI." ) ),
    [&]( auto & page_id ) {
        add( "NPC_LOD_ENABLED", page_id,
             translate_marker( "NPC LOD Enabled" ),
             translate_marker( "If true, NPC AI fidelity decreases with distance from the player.  "
                               "Close NPCs run full AI; distant NPCs run progressively coarser AI.  "
                               "Companions and visible NPCs always run full AI regardless." ),
             true
           );
        add( "NPC_TIER0_DIST", page_id,
             translate_marker( "Full AI Radius" ),
             translate_marker( "NPCs within this radius run the complete AI every turn.  "
                               "Must be less than the Coarse AI Radius.  "
                               "Companions always run full AI regardless of distance." ),
             5, 208, is_android ? 20 : 30 );
        add( "NPC_TIER1_DIST", page_id,
             translate_marker( "Coarse AI Radius" ),
             translate_marker( "NPCs between the Full AI Radius and this distance run coarse AI: "
                               "process_turn and move loop every turn, but monster-danger scanning "
                               "runs less frequently (see NPC Coarse Danger Interval).  "
                               "NPCs beyond this distance are Tier-2 (process_turn only, no move loop)." ),
             10, 208, is_android ? 40 : 75 );
        add( "NPC_DEMOTION_COOLDOWN", page_id,
             translate_marker( "Demotion Cooldown" ),
             translate_marker( "Turns an NPC must wait after being promoted to a higher-fidelity "
                               "tier before it can be demoted again.  Prevents rapid tier oscillation "
                               "at distance boundaries.  0 disables the cooldown." ),
             0, 10, 3 );
        add( "NPC_ACTION_BUDGET", page_id,
             translate_marker( "Action Budget" ),
             translate_marker( "Maximum number of non-follower NPCs that can enter the full move loop "
                               "each turn.  When the budget is exceeded, the farthest NPCs are deferred "
                               "to the next turn.  Followers and visible NPCs are always processed "
                               "regardless of budget.  0 disables the budget cap." ),
             0, 128, is_android ? 8 : 16 );
        add( "NPC_COARSE_DANGER_INTERVAL", page_id,
             translate_marker( "Coarse Danger Scan Interval" ),
             translate_marker( "How many turns between full monster-danger scans for Tier-1 (coarse) "
                               "NPCs.  At 1 they scan every turn (full fidelity); at 5 (default) they "
                               "scan only once every 5 turns, reusing cached danger between scans." ),
             1, 20, is_android ? 8 : 5 );
        add( "NPC_MACRO_INTERVAL", page_id,
             translate_marker( "Macro Step Interval" ),
             translate_marker( "How many turns between macro-steps for Tier-2 NPCs.  "
                               "At 1 they step every turn (disabled macro AI); at 3 (default) they "
                               "take a single reposition step once every 3 turns.  "
                               "Higher values reduce CPU cost for distant NPCs." ),
             1, 10, is_android ? 4 : 3 );
    } );

    get_option( "NPC_TIER0_DIST" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_TIER1_DIST" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_DEMOTION_COOLDOWN" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_ACTION_BUDGET" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_COARSE_DANGER_INTERVAL" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_MACRO_INTERVAL" ).setPrerequisite( "NPC_LOD_ENABLED" );

    add_empty_line();

    add_option_group( performance, Group( "vehicle", to_translation( "Vehicle Throttling" ),
                                          to_translation( "Configure vehicle processing stride to reduce CPU cost "
                                                  "for parked and off-z vehicles." ) ),
    [&]( auto & page_id ) {
        add( "VEHICLE_IDLE_STRIDE", page_id,
             translate_marker( "Idle Stride" ),
             translate_marker( "How many turns between idle() calls for parked vehicles "
                               "(engine off, not moving, no reactor, not player-controlled).  "
                               "At 1 they process every turn (disabled).  Higher values reduce "
                               "CPU cost for many parked vehicles at the cost of battery-level "
                               "precision, which lags by up to K-1 turns." ),
             1, 20, 5 );
        add( "VEHICLE_OUTER_STRIDE", page_id,
             translate_marker( "Outer Loop Stride" ),
             translate_marker( "How many turns between gain_moves/slow_leak processing for "
                               "off-z parked vehicles (engine off, not moving, on a different "
                               "z-level than the player).  At 1 they process every turn "
                               "(disabled).  Higher values reduce CPU cost for many off-z "
                               "parked vehicles." ),
             1, 10, 2 );
    } );

    get_option( "VEHICLE_IDLE_STRIDE" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
    get_option( "VEHICLE_OUTER_STRIDE" ).setPrerequisite( "MONSTER_LOD_ENABLED" );

    add_empty_line();

    add_option_group( performance, Group( "item_processing", to_translation( "Item Processing" ),
                                          to_translation( "Configure item processing stride to reduce CPU cost "
                                                  "for off-z-level items." ) ),
    [&]( auto & page_id ) {
        add( "ITEM_PROCESS_STRIDE", page_id,
             translate_marker( "Item Process Stride" ),
             translate_marker( "How many turns between processing off-z submap active items.  "
                               "At 1 they process every turn (disabled).  Higher values reduce "
                               "CPU cost for off-z items at the cost of delayed per-turn side "
                               "effects (emissions, tool drain, LITCIG).  "
                               "Time-critical items (explosives, countdown items) always process "
                               "every turn regardless of stride." ),
             1, 10, 1 );
    } );

    get_option( "ITEM_PROCESS_STRIDE" ).setPrerequisite( "MONSTER_LOD_ENABLED" );

    add_empty_line();

    add_option_group( performance, Group( "fov_3d", to_translation( "3D Field of Vision" ),
                                          to_translation( "Configure three-dimensional visibility across z-levels." ) ),
    [&]( auto & page_id ) {
        add( "FOV_3D", page_id, translate_marker( "3D field of vision" ),
             translate_marker( "If false, vision is limited to current z-level. If true and the world is in z-level mode, the vision will extend beyond current z-level." ),
             true
           );
        add( "FOV_3D_Z_RANGE", page_id, translate_marker( "Vertical range of 3D field of vision" ),
             translate_marker( "How many levels up and down the experimental 3D field of vision reaches. (This many levels up, this many levels down.)  3D vision of the full height of the world can slow the game down a lot.  Seeing fewer Z-levels is faster." ),
             0, OVERMAP_LAYERS, is_android ? 3 : 5
           );
        add( "FOV_3D_OCCLUSION", page_id, translate_marker( "3D FoV shadow casting" ),
             translate_marker( "When enabled, obstacles at other z-levels correctly cast 3D shadows. Requires 3D FoV. Significantly slower than disabled." ),
             false
           );
        add( "PREVENT_OCCLUSION", page_id, translate_marker( "Handle occlusion by high sprites" ),
             translate_marker( "Draw tall sprites normal (Off), retracted/transparent (On), or automatically retracting/transparent near the player (Auto)." ),
        {
            { "off", translate_marker( "Off" ) },
            { "on", translate_marker( "On" ) },
            { "auto", translate_marker( "Auto" ) }
        },
        "auto" );
        add( "PREVENT_OCCLUSION_TRANSP", page_id, translate_marker( "Prevent occlusion via transparency" ),
             translate_marker( "Prevent high-sprite occlusion by using semi-transparent *_transparent tile variants when available." ),
             true
           );
        add( "PREVENT_OCCLUSION_RETRACT", page_id, translate_marker( "Prevent occlusion via retraction" ),
             translate_marker( "Prevent high-sprite occlusion by retracting sprites that define retracted offsets." ),
             true
           );
        add( "PREVENT_OCCLUSION_MIN_DIST", page_id,
             translate_marker( "Minimum distance for automatic occlusion handling" ),
             translate_marker( "Minimum distance for automatic occlusion handling. Values above zero override tileset settings." ),
             0.0, 60.0, 0.0, 0.1
           );
        add( "PREVENT_OCCLUSION_MAX_DIST", page_id,
             translate_marker( "Maximum distance for automatic occlusion handling" ),
             translate_marker( "Maximum distance for automatic occlusion handling. Values above zero override tileset settings." ),
             0.0, 60.0, 0.0, 0.1
           );
    } );

    get_option( "FOV_3D_Z_RANGE" ).setPrerequisite( "FOV_3D" );
    get_option( "FOV_3D_OCCLUSION" ).setPrerequisite( "FOV_3D" );

    add_empty_line();

    add( "SKEW_VISION_CACHE_SIZE", performance,
         translate_marker( "LOS Cache Size" ),
         translate_marker( "Maximum number of line-of-sight results kept in the skew-vision LRU cache.  "
                           "Higher values reduce redundant ray traces at the cost of more RAM.  "
                           "Reduce if memory is tight; increase on machines with spare RAM and many "
                           "on-screen creatures." ),
         1024, 4194304, is_android ? 65536 : 262144 );

    add_empty_line();

    add_option_group( performance, Group( "multithreading", to_translation( "Multithreading" ),
                                          to_translation( "Configure worker-thread parallelism for expensive per-turn computations." ) ),
    [&]( auto & page_id ) {
        add( "MULTITHREADING_ENABLED", page_id,
             translate_marker( "Enable Multithreading" ),
             translate_marker( "Enable worker-thread parallelism for expensive per-turn computations "
                               "(monster planning, map-cache building, scent map updates, etc).  "
                               "Disable to run everything on the main thread — useful for debugging, "
                               "reproducibility testing, or machines where thread overhead exceeds gain.  "
                               "Requires restart." ),
             !is_android );
        add( "THREAD_POOL_WORKERS", page_id,
             translate_marker( "Thread Pool Worker Count" ),
             translate_marker( "Number of worker threads in the persistent thread pool.  "
                               "0 means automatic (hardware concurrency minus 1, leaving one core for "
                               "the main/SDL thread).  Set to a lower value to cap CPU usage, e.g. when "
                               "streaming or running other CPU-heavy applications alongside the game.  "
                               "Requires restart." ),
             0, 64, 0 );
        add( "PARALLEL_MONSTER_PLANNING", page_id,
             translate_marker( "Parallel Monster Planning" ),
             translate_marker( "Compute monster AI plans (pathfinding target selection, LOS queries) in "
                               "parallel across worker threads each turn.  Disable if monsters behave "
                               "unexpectedly or for reproducible save-file testing.  Requires restart." ),
             true );
        add( "MONSTER_PLAN_CHUNK_SIZE", page_id,
             translate_marker( "Monster Plan Chunk Size" ),
             translate_marker( "Number of monsters batched into a single worker-thread task during the "
                               "parallel planning pass.  Smaller values improve load balancing when "
                               "planning cost varies widely (large hordes with mixed sight ranges); "
                               "larger values reduce task-dispatch overhead.  Requires restart." ),
             1, 64, 8 );
        add( "PARALLEL_MAP_CACHE", page_id,
             translate_marker( "Parallel Map Cache Build" ),
             translate_marker( "Build per-z-level map caches (transparency, outside, floor, "
                               "vehicle-obscured) in parallel across worker threads.  Disable on "
                               "machines where the thread-dispatch overhead exceeds the benefit "
                               "(typically dual-core systems or when z-levels are disabled).  "
                               "Requires restart." ),
             true );
        add( "PARALLEL_SCENT_UPDATE", page_id,
             translate_marker( "Parallel Scent Update" ),
             translate_marker( "Compute the scent-diffusion Y-pass and X-pass across worker threads.  "
                               "Disable on machines where the ~70 k-cell work unit is too small to "
                               "amortize dispatch latency.  Requires restart." ),
             true );
    } );

    get_option( "THREAD_POOL_WORKERS" ).setPrerequisite( "MULTITHREADING_ENABLED" );
    get_option( "PARALLEL_MONSTER_PLANNING" ).setPrerequisite( "MULTITHREADING_ENABLED" );
    get_option( "MONSTER_PLAN_CHUNK_SIZE" ).setPrerequisite( "MULTITHREADING_ENABLED" );
    get_option( "PARALLEL_MAP_CACHE" ).setPrerequisite( "MULTITHREADING_ENABLED" );
    get_option( "PARALLEL_SCENT_UPDATE" ).setPrerequisite( "MULTITHREADING_ENABLED" );

    add_empty_line();

    add_option_group( performance, Group( "reality_bubble", to_translation( "Reality Bubble" ),
                                          to_translation( "Configure how the reality bubble functions." ) ),
    [&]( auto & page_id ) {
        add( "REALITY_BUBBLE_SIZE", page_id,
             translate_marker( "Reality Bubble Size" ),
             translate_marker( "Submap radius of the reality bubble (submaps visible beyond your position). "
                               "Grid size = 2 × size + 3 submaps per side (size 4 → 11×11, legacy default). "
                               "Maximum player sight range = 12 × (size + 1) tiles.  "
                               "Larger values increase the loaded area and memory usage; "
                               "smaller values reduce both. " ),
             0, REALITY_BUBBLE_SIZE_MAX, is_android ? 4 : 6 );
        add( "LAZY_BORDER", page_id,
             translate_marker( "Pre-load Border" ),
             translate_marker( "Preload a one-overmap-tile border around the reality bubble over several turns.  "
                               "This reduces map-shift hitches at the cost of extra per-turn loading work and    "
                               "some additional memory usage." ),
             !is_android );
        add( "ACTIVITY_MOBILE_BUBBLE_SIZE", page_id,
             translate_marker( "Mobile Activity Bubble Size" ),
             translate_marker( "Shrink the reality bubble to this radius while the player is performing a "
                               "mobile activity (crafting, construction, etc.).  "
                               "0 disables the feature.  Must be smaller than Reality Bubble Size to take effect." ),
             0, REALITY_BUBBLE_SIZE_MAX, is_android ? 3 : 4 );
        add( "ACTIVITY_IDLE_BUBBLE_SIZE", page_id,
             translate_marker( "Idle Activity Bubble Size" ),
             translate_marker( "Shrink the reality bubble to this radius while the player is performing an "
                               "idle activity (sleeping, reading, waiting, etc.).  "
                               "0 disables the feature.  Must be smaller than Reality Bubble Size to take effect." ),
             0, REALITY_BUBBLE_SIZE_MAX, is_android ? 2 : 3 );
        add( "UNDERGROUND_BUBBLE_SIZE", page_id,
             translate_marker( "Underground Reality Bubble Size" ),
             translate_marker( "Shrink the reality bubble to this radius while the player is underground "
                               "and indoors (no sky visible).  "
                               "0 disables the feature.  Must be smaller than Reality Bubble Size to take effect." ),
             0, REALITY_BUBBLE_SIZE_MAX, is_android ? 2 : 4 );
        add( "VEHICLE_BUBBLE_SIZE", page_id,
             translate_marker( "Vehicle Reality Bubble Size" ),
             translate_marker( "Shrink the reality bubble to this radius while the player is actively driving a vehicle  "
                               "or mounted on a creature. Useful with a high render distance to reduce lag at speed.  "
                               "0 disables the feature.  Must be smaller than Reality Bubble Size to take effect." ),
             0, REALITY_BUBBLE_SIZE_MAX, is_android ? 3 : 0 );
        add( "COMBAT_BUBBLE_SIZE", page_id,
             translate_marker( "Combat Reality Bubble Size" ),
             translate_marker( "Shrink the reality bubble to this radius while hostile creatures are visible nearby.  "
                               "Uses the same detection range as safe mode.  "
                               "0 disables the feature.  Must be smaller than Reality Bubble Size to take effect." ),
             0, REALITY_BUBBLE_SIZE_MAX, 0 );
        add( "ACTIVITY_BUBBLE_GRACE", page_id,
             translate_marker( "Activity Bubble Grace Period" ),
             translate_marker( "Minimum length of activity in minutes before the reality bubble shrinks.  "
                               "Acts as a safety net to avoid unnecessary resizes for short tasks.  "
                               "Default is 5 minutes." ),
             1, 60, 5 );
        add( "DYNAMIC_BUBBLE_GRACE", page_id,
             translate_marker( "Dynamic Bubble Grace Period" ),
             translate_marker( "Consecutive turns a condition must be met before the reality bubble shrinks "
                               "for underground, vehicle, and combat modes.  "
                               "Prevents rapid resizing when briefly entering or leaving a trigger zone.  "
                               "Default is 5 turns." ),
             1, 30, 5 );
    } );

    add_empty_line();

    add_option_group( performance, Group( "submap_loading", to_translation( "Submap Loading" ),
                                          to_translation( "Configure how submaps are loaded and "
                                                  "processed outside of the reality bubble." ) ),
    [&]( auto & page_id ) {
        // Temporary fix for #8726: disable out-of-bubble fire spread until
        // fire-loaded submaps can safely handle vehicle state.
        // add( "REALITY_BUBBLE_FIRE_SPREAD", page_id,
        //      translate_marker( "Out-of-Bubble Fire Spread" ),
        //      translate_marker( "Controls whether fire can keep areas loaded outside of render "
        //                        "distance. 'None': fire burns out in place. "
        //                        "'Adjacent': fire can spread into unloaded areas, and keeps "
        //                        "close enough." ), {
        //     { "none", translate_marker( "None (pause spread)" ) },
        //     { "adjacent", translate_marker( "Adjacent (one layer)" ) }
        // },
        // is_android ? "none" : "adjacent"
        //    );
        // add( "FIRE_SPREAD_SUBMAP_CAP", page_id,
        //      translate_marker( "Fire Spread Submap Cap" ),
        //      translate_marker( "Maximum number of submaps that fire spread may keep loaded "
        //                        "simultaneously across all dimensions. Higher values allow larger "
        //                        "fires to be simulated correctly. "
        //                        "0 disables out-of-bubble fire spread loading entirely. " ),
        //      0, 250, 25 );
        add( "RETAINED_OMT_CACHE_MULTIPLIER", page_id,
             translate_marker( "Retained Map Cache" ),
             translate_marker( "Keep more map data loaded to reduce lag when moving around the same general area, "
                               "at the cost of memory usage." ),
             1, 20, is_android ? 1 : 3 );
        add( "POWER_PORTAL_LOAD_RADIUS", page_id,
             translate_marker( "Power portal load radius (submaps)" ),
             translate_marker( "Radius in submaps around each end of a power-portal link that is "
                               "force-loaded while the link is active." ),
             0, static_cast<int>( REALITY_BUBBLE_SIZE_MAX ) + 1, is_android ? 2 : 3
           );
    } );

    // get_option( "FIRE_SPREAD_SUBMAP_CAP" ).setPrerequisite( "REALITY_BUBBLE_FIRE_SPREAD", "adjacent" );
}

void options_manager::add_options_debug()
{
    const auto add_empty_line = [&]() {
        this->add_empty_line( debug );
    };

    add( "STRICT_JSON_CHECKS", debug, translate_marker( "Strict JSON checks" ),
         translate_marker( "If true, will show additional warnings for JSON data correctness." ),
         true
       );

    add( "FORCE_TILESET_RELOAD", debug, translate_marker( "Force tileset reload" ),
         translate_marker( "If false, the game will keep tileset in memory after first load to speed up subsequent loadings of game data.  Enable this if you're working on a tileset for the game or a mod." ),
         false
       );

    add_empty_line();

    add( "MOD_SOURCE", debug, translate_marker( "Display Mod Source" ),
         translate_marker( "Displays what content pack a piece of furniture, terrain, item or monster comes from or is affected by.  Disable if it's annoying." ),
         true
       );

    add( "SHOW_IDS", debug, translate_marker( "Display Object IDs" ),
         translate_marker( "Displays internal IDs of game objects and creatures.  Warning: IDs may contain spoilers." ),
         false
       );

    add_empty_line();

    add_option_group( debug, Group( "debug_log", to_translation( "Logging" ),
                                    to_translation( "Configure debug.log verbosity." ) ),
    [&]( auto & page_id ) {
        for( const debug_log_level &e : debug_log_levels ) {
            add( e.opt_id, page_id, e.opt_name, e.opt_descr, e.opt_default );
        }

        add_empty_line();

        for( const debug_log_class &e : debug_log_classes ) {
            add( e.opt_id, page_id, e.opt_name, e.opt_descr, e.opt_default );
        }
    } );

    add_empty_line();

    add( "DISTANCE_INITIAL_VISIBILITY", debug, translate_marker( "Distance initial visibility" ),
         translate_marker( "Determines the scope, which is known in the beginning of the game." ),
         3, 20, 15
       );

    add( "INITIAL_STAT_POINTS", debug, translate_marker( "Initial stat points" ),
         translate_marker( "Initial points available to spend on stats on character generation." ),
         0, 1000, 6
       );

    add( "INITIAL_TRAIT_POINTS", debug, translate_marker( "Initial trait points" ),
         translate_marker( "Initial points available to spend on traits on character generation." ),
         0, 1000, 0
       );

    add( "INITIAL_SKILL_POINTS", debug, translate_marker( "Initial skill points" ),
         translate_marker( "Initial points available to spend on skills on character generation." ),
         0, 1000, 2
       );

    add( "MAX_TRAIT_POINTS", debug, translate_marker( "Maximum trait points" ),
         translate_marker( "Maximum trait points available for character generation." ),
         0, 1000, 12
       );

    add_empty_line();

    add( "SKILL_TRAINING_SPEED", debug, translate_marker( "Skill training speed" ),
         translate_marker( "Scales experience gained from practicing skills and reading books.  0.5 is half as fast as default, 2.0 is twice as fast, 0.0 disables skill training except for NPC training." ),
         0.0, 100.0, 1.0, 0.1
       );

    add( "SKILL_RUST", debug, translate_marker( "Skill rust" ),
         translate_marker( "Set the level of skill rust.  Vanilla: Vanilla Cataclysm - Capped: Capped at skill levels 2 - Int: Intelligence dependent - IntCap: Intelligence dependent, capped - Off: None at all." ),
         //~ plain, default, normal
    {   { "vanilla", translate_marker( "Vanilla" ) },
        //~ capped at a value
        { "capped", translate_marker( "Capped" ) },
        //~ based on intelligence
        { "int", translate_marker( "Int" ) },
        //~ based on intelligence and capped
        { "intcap", translate_marker( "IntCap" ) },
        { "off", translate_marker( "Off" ) }
    },
    "off" );

    add_empty_line();

    add( "PICKUP_RANGE", debug, translate_marker( "Crafting range" ),
         translate_marker( "Maximum distance at which items are considered available for crafting (or some other actions)." ),
         1, 30, 6
       );

    add( "ENABLE_EVENTS", debug, translate_marker( "Event bus system" ),
         translate_marker( "If false, achievements and some Magiclysm functionality won't work, but performance will be better." ),
         true
       );

    add( "ELECTRIC_GRID", debug, translate_marker( "Electric grid testing" ),
         translate_marker( "If true, enables somewhat unfinished electric grid system that may slow the game down." ),
         true
       );

    add( "MADE_OF_EXPLODIUM", debug, translate_marker( "Made of explodium" ),
         translate_marker( "Explosive items and traps will detonate when hit by damage exceeding the threshold.  A higher number means more damage is required to detonate.  Set to 0 to disable." ),
         0, 1000, 30 );
    add( "ITEM_DAMAGE_ON_FLYING_IMPACT", debug, translate_marker( "Item damage on flying impact" ),
         translate_marker( "If true, items flung by explosions will deal (lethal) damage to whatever they hit." ),
         true );

    add( "OLD_EXPLOSIONS", debug, translate_marker( "Old explosions system" ),
         translate_marker( "If true, disables new raycasting based explosive system in favor of old system.  With new system obstacles (impassable terrain, furniture or vehicle parts) will block shrapnel, while blast will bash obstacles and throw creatures outward.  If obstacles are destroyed, blast continues outward." ),
         false );

    get_option( "MADE_OF_EXPLODIUM" ).setPrerequisite( "OLD_EXPLOSIONS", "false" );
    get_option( "ITEM_DAMAGE_ON_FLYING_IMPACT" ).setPrerequisite( "OLD_EXPLOSIONS", "false" );

    add( "CHRONIC_PAIN", debug, translate_marker( "Chronic pain" ),
         translate_marker( "If true, injuries cause persistent pain until they are healed." ), false );

    add_empty_line();

    add( "LIMITED_BAYONETS", debug, translate_marker( "New bayonet system" ),
         translate_marker( "If true, bayonets replace weapon attack instead of adding to it.  WIP feature, weakens bayonets heavily at the moment." ),
         false );

    add_empty_line();

    add( "USE_LEGACY_PATHFINDING", debug,
         translate_marker( "Use legacy pathfinding" ),
         translate_marker( "If true, opt out of new pathfinding in favor of legacy one. This makes pathfinding mods not work." ),
         false );
    add( "PATHFINDING_MAX_DIST", debug,
         translate_marker( "Legacy Pathfinder Distance Cap" ),
         translate_marker( "Hard cap on straight-line pathfinding distance (in tiles) for the legacy pathfinder.  "
                           "Monsters and NPCs whose configured range exceeds this value are limited to it.  "
                           "The old fixed map allowed at most 120 tiles end-to-end; "
                           "the default of 96 is 50%% larger than the old per-side maximum of 60.  "
                           "Raise this if mods require longer paths; lower it to reduce pathfinding cost at large bubble sizes." ),
         16, 1000, 96 );
    get_option( "PATHFINDING_MAX_DIST" ).setPrerequisite( "USE_LEGACY_PATHFINDING" );
}

void options_manager::add_options_world_default()
{
    const auto add_empty_line = [&]() {
        this->add_empty_line( world_default );
    };

    add_empty_line();

    add( "WORLD_END", world_default, translate_marker( "World end handling" ),
    translate_marker( "Handling of game world when last character dies." ), {
        { "reset", translate_marker( "Reset" ) }, { "delete", translate_marker( "Delete" ) },
        { "query", translate_marker( "Query" ) }, { "keep", translate_marker( "Keep" ) }
    }, "reset"
       );

    add_empty_line();

    add( "CITY_SIZE", world_default, translate_marker( "Size of cities" ),
         translate_marker( "A number determining how large cities are.  0 disables cities, roads and any scenario requiring a city start." ),
         0, 16, 8
       );

    add( "CITY_SPACING", world_default, translate_marker( "City spacing" ),
         translate_marker( "A number determining how far apart cities are.  Warning, small numbers lead to very slow mapgen." ),
         0, 8, 4
       );

    add( "SPECIALS_DENSITY", world_default, translate_marker( "Overmap specials density factor" ),
         translate_marker( "A scaling factor that determines density of overmap specials." ),
         0.01, 10.0, 1, 0.1
       );

    add( "SPECIALS_SPACING", world_default, translate_marker( "Overmap specials spacing" ),
         translate_marker( "A number determing minimum distance between overmap specials.  -1 allows intersections of specials." ),
         -1, 72, 6
       );

    add( "VEHICLE_DAMAGE", world_default, translate_marker( "Vehicle damage scaling factor" ),
         translate_marker( "A scaling factor that determines how damaged vehicles are." ),
         0.0, 10.0, 1, 0.1
       );

    add( "VEHICLE_LOCKS", world_default, translate_marker( "Vehicle door locks" ),
         translate_marker( "Determines if new vehicles can spawn with locked doors." ), true
       );

    add( "VEHICLE_SPAWNRATE", world_default, translate_marker( "Vehicle spawn rate scaling factor" ),
         translate_marker( "A scaling factor that determines density of vehicle spawns." ),
         0.0, 5.0, 1.0, 0.01
       );

    add( "VEHICLE_GUN_RECOIL_FACTOR", world_default,
         translate_marker( "Vehicle gun recoil scaling factor" ),
         translate_marker( "A scaling factor that determines how strongly firing guns pushes the vehicle you are on.  0.0 disables this behavior, 1.0 uses the default mass-based recoil propulsion, and higher values exaggerate it." ),
         0.0, 100.0, 1.0, 0.1
       );

    add( "SPAWN_DENSITY", world_default, translate_marker( "Spawn rate scaling factor" ),
         translate_marker( "A scaling factor that determines density of monster spawns." ),
         0.0, 50.0, 1.0, 0.1
       );
    add( "SPAWN_ANIMAL_DENSITY", world_default, translate_marker( "Animal spawn rate scaling factor" ),
         translate_marker( "A scaling factor that determines density of animal spawns." ),
         0.0, 50.0, 1.0, 0.1
       );

    add( "CARRION_SPAWNRATE", world_default, translate_marker( "Carrion spawn rate scaling factor" ),
         translate_marker( "A scaling factor that determines how often creatures spawn from rotting material." ),
         0.0, 10.0, 1.0, 0.01, COPT_NO_HIDE
       );

    add( "NPC_DENSITY", world_default, translate_marker( "NPC spawn rate scaling factor" ),
         translate_marker( "A scaling factor that determines density of dynamic NPC spawns." ),
         0.0, 100.0, 0.1, 0.01
       );

    add( "MONSTER_UPGRADE_FACTOR", world_default,
         translate_marker( "Monster evolution scaling factor" ),
         translate_marker( "A scaling factor that determines the time between monster upgrades.  A higher number means slower evolution.  Set to 0.00 to turn off monster upgrades." ),
         0.0, 100, 2.0, 0.01
       );

    add( "EVOLVE_MAX_ITERS", world_default,
         translate_marker( "Maximum Evolution Half Lives" ),
         translate_marker( "The maximum number of attempts for a zombie to evolve at the next half life for one evolution stage" ),
         0, 200, 5 );

    add( "ALWAYS_EVOLVE", world_default,
         translate_marker( "Zombies Always Evolve" ),
         translate_marker( "When reaching the maximum half lives, instead of never evolving they will evolve at that time." ),
         false );

    add_empty_line();

    add( "RESTOCK_DELAY_MULT", world_default, translate_marker( "Merchant restock scaling factor" ),
         translate_marker( "A scaling factor that determines restock rate of merchants." ),
         0.01, 10.0, 1.0, 0.01
       );

    add( "CROSS_Z_LEVEL_MELEE_DIFFICULTY_MODIFIER", world_default,
         translate_marker( "Cross z-level melee difficulty modifier" ),
         translate_marker( "A scaling factor that determines additional move and stamina cost for melee attacks against a target above or below you.  1.00 disables the modifier." ),
         1.00, 3.00, 1.20, 0.01
       );

    add_empty_line();

    add_option_group( world_default, Group( "skill_buff_category",
                                            to_translation( "Enabled Skill Buffs" ),
                                            to_translation( "Enable or disable major skill buffs" ) ),
    [&]( const std::string & page_id ) {
        add( "cooking_kcal_buff", page_id, "Cooking Calories Buff",
             "Include the scaling calories from cooking buff?",
             true );
        add( "althletics_encumbrance_buff", page_id, "Althletics Encumbrance Buff",
             "Include the reduce all encumbrance per level of althletics buff?",
             true );
    }
                    );

    add_empty_line();

    add( "canmutprofmut", world_default, "Starting Trait Cancelling",
         "Allow starting traits to be cancelled and effected by purifiers?",
         false );

    add_empty_line();

    add( "ITEM_SPAWNRATE", world_default,
         "Item spawn scaling factor",
         "A scaling factor that determines density of item spawns. A higher number means more items. Affects both map generation and monster death drops.",
         0.01, 10.0, 1.0, 0.01 );

    add_option_group( world_default, Group( "item_category_spawn_rate",
                                            to_translation( "Item category scaling factors" ),
                                            to_translation( "Spawn rate for item categories. For map generation: values ≤ 1.0 represent a chance to spawn, >1.0 means extra spawns. For monster drops: values >1.0 increase spawn probability (capped at 100%). Set to 0.0 to disable spawning items from that category." ) ),
    [&]( const std::string & page_id ) {

        add( "SPAWN_RATE_ammo", page_id, "AMMO",
             "Spawn rate for items from AMMO category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_artifacts", page_id, "ARTIFACTS",
             "Spawn rate for items from ARTIFACTS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_battery", page_id, "BATTERIES",
             "Spawn rate for items from BATTERIES category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_bionics", page_id, "BIONICS",
             "Spawn rate for items from BIONICS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_books", page_id, "BOOKS",
             "Spawn rate for items from BOOKS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_perishables_canned", page_id, "CANNED PERISHABLES",
             "Spawn rate for items from CANNED PERISHABLES category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_ceramics", page_id, "CERAMIC SCRAP",
             "Spawn rate for items from CERAMIC SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_chems", page_id, "CHEMICAL STUFF",
             "Spawn rate for items from CHEMICAL STUFF category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_tools_chemistry", page_id, "CHEMISTRY TOOLS",
             "Spawn rate for items from CHEMISTRY TOOLS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_clothing", page_id, "CLOTHING",
             "Spawn rate for items from CLOTHING category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_container", page_id, "CONTAINER",
             "Spawn rate for items from CONTAINER category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_cooking_ingredients", page_id, "COOKING INGREDIENTS",
             "Spawn rate for items from COOKING INGREDIENTS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_tools_cooking", page_id, "COOKING TOOLS",
             "Spawn rate for items from COOKING TOOLS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_deployables", page_id, "DEPLOYABLES",
             "Spawn rate for items from DEPLOYABLES category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_drugs", page_id, "DRUGS",
             "Spawn rate for items from DRUGS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_electronics", page_id, "ELECTRONIC SCRAP",
             "Spawn rate for items from ELECTRONIC SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_electronics", page_id, "ELECTRONICS",
             "Spawn rate for items from ELECTRONICS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_tools_entry", page_id, "ENTRY TOOLS",
             "Spawn rate for items from ENTRY TOOLS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_fabric", page_id, "FABRICS",
             "Spawn rate for items from FABRICS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_tools_farming", page_id, "FARM TOOLS",
             "Spawn rate for items from FARM TOOLS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_food", page_id, "FOOD",
             "Spawn rate for items from FOOD category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_fuel", page_id, "FUEL",
             "Spawn rate for items from FUEL category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_glass", page_id, "GLASS SCRAP",
             "Spawn rate for items from GLASS SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_guns", page_id, "GUNS",
             "Spawn rate for items from GUNS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_leather", page_id, "LEATHER SCRAP",
             "Spawn rate for items from LEATHER SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_magazines", page_id, "MAGAZINES",
             "Spawn rate for items from MAGAZINES category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_maps", page_id, "MAPS",
             "Spawn rate for items from MAPS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_metal", page_id, "METAL SCRAP",
             "Spawn rate for items from METAL SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_misc", page_id, "MISC SCRAP",
             "Spawn rate for items from MISC SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_mods", page_id, "MODS",
             "Spawn rate for items from MODS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_mutagen", page_id, "MUTAGENS",
             "Spawn rate for items from MUTAGENS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_other", page_id, "OTHER",
             "Spawn rate for items from OTHER category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_tools", page_id, "OTHER TOOLS",
             "Spawn rate for items from OTHER TOOLS category.",
             0.0, 20.0, 1.0, 0.01 );

        // this needs special handling since it uses .goes_bad() instead of an actual group
        add( "SPAWN_RATE_perishables", page_id, "PERISHABLES",
             "Spawn rate for items from PERISHABLES category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_plastic", page_id, "PLASTIC SCRAP",
             "Spawn rate for items from PLASTIC SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_rocks", page_id, "ROCKS",
             "Spawn rate for items from ROCKS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_rubber", page_id, "RUBBER SCRAP",
             "Spawn rate for items from RUBBER SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_seeds", page_id, "SEEDS",
             "Spawn rate for items from SEEDS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_soil", page_id, "SOIL",
             "Spawn rate for items from SOIL category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_spare_parts", page_id, "SPARE PARTS",
             "Spawn rate for items from SPARE PARTS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_spellbooks", page_id, "SPELLBOOKS",
             "Spawn rate for items from SPELLBOOKS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_valuables", page_id, "VALUABLES",
             "Spawn rate for items from VALUABLES category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_veh_parts", page_id, "VEHICLE PARTS",
             "Spawn rate for items from VEHICLE PARTS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_weapons", page_id, "WEAPONS",
             "Spawn rate for items from WEAPONS category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_scrap_wood", page_id, "WOOD SCRAP",
             "Spawn rate for items from WOOD SCRAP category.",
             0.0, 20.0, 1.0, 0.01 );

        add( "SPAWN_RATE_tools_workshop", page_id, "WORKSHOP TOOLS",
             "Spawn rate for items from WORKSHOP TOOLS category.",
             0.0, 20.0, 1.0, 0.01 );


    } );

    add_empty_line();

    add( "MONSTER_SPEED", world_default, translate_marker( "Monster speed percentage" ),
         translate_marker( "Determines the movement rate of monsters.  A higher value increases monster speed and a lower reduces it.  Requires world reset." ),
         1, 1000, 100, COPT_NO_HIDE, "%i%%"
       );

    add( "MONSTER_RESILIENCE", world_default,
         translate_marker( "Monster resilience percentage" ),
         translate_marker( "Determines how much damage monsters can take.  A higher value makes monsters more resilient and a lower makes them more flimsy.  Requires world reset." ),
         1, 1000, 100, COPT_NO_HIDE, "%i%%"
       );

    add_empty_line();

    add( "DEFAULT_REGION", world_default, translate_marker( "Default region type" ),
         translate_marker( "( WIP feature ) Determines terrain, shops, plants, and more." ),
    { { "default", "default" } }, "default"
       );

    add_empty_line();

    add( "INITIAL_DAY", world_default, translate_marker( "Initial day" ),
         translate_marker( "How many days into the year the cataclysm occurred.  Day 0 is Spring 1.  Day -1 randomizes the start date.  Can be overridden by scenarios.  This does not advance food rot or monster evolution." ),
         -1, 999, 15
       );

    add( "SPAWN_DELAY", world_default, translate_marker( "Spawn delay" ),
         translate_marker( "How many days after the cataclysm the player spawns.  Day 0 is the day of the cataclysm.  Can be overridden by scenarios.  Increasing this will cause food rot and monster evolution to advance." ),
         0, 9999, 0
       );

    add( "INITIAL_TIME", world_default, translate_marker( "Initial time" ),
         translate_marker( "Hour of the day at which the player starts the game." ),
         0, 23, 8
       );

    add( "SEASON_LENGTH", world_default, translate_marker( "Season length" ),
         translate_marker( "Season length, in days." ),
         14, 127, 30
       );

    add( "SUNRISE_SUMMER", world_default, translate_marker( "Sunrise hour (summer)" ),
         translate_marker( "Hour of sunrise at the summer solstice for the base dimension.  Per-dimension overrides can be set in world_types JSON." ),
         0, 12, 5
       );
    add( "SUNRISE_WINTER", world_default, translate_marker( "Sunrise hour (winter)" ),
         translate_marker( "Hour of sunrise at the winter solstice for the base dimension." ),
         0, 12, 7
       );
    add( "SUNRISE_EQUINOX", world_default, translate_marker( "Sunrise hour (equinox)" ),
         translate_marker( "Hour of sunrise at the spring and autumn equinox for the base dimension." ),
         0, 12, 6
       );
    add( "SUNSET_SUMMER", world_default, translate_marker( "Sunset hour (summer)" ),
         translate_marker( "Hour of sunset at the summer solstice for the base dimension." ),
         12, 23, 21
       );
    add( "SUNSET_WINTER", world_default, translate_marker( "Sunset hour (winter)" ),
         translate_marker( "Hour of sunset at the winter solstice for the base dimension." ),
         12, 23, 17
       );
    add( "SUNSET_EQUINOX", world_default, translate_marker( "Sunset hour (equinox)" ),
         translate_marker( "Hour of sunset at the spring and autumn equinox for the base dimension." ),
         12, 23, 19
       );

    add( "CONSTRUCTION_SCALING", world_default,
         translate_marker( "Construction speed percentage" ),
         translate_marker( "Sets the time of construction in percents.  '50' is two times faster than default, '200' is two times longer.  '0' makes construction instant." ),
         0, 1000, 100, COPT_NO_HIDE, "%i%%"
       );


    add( "CRAFTING_SPEED_MULT", world_default, translate_marker( "Crafting speed percentage" ),
         translate_marker( "Sets default crafting speed in percents.  '50' is two times faster than default, '200' is two times longer.  '0' makes crafting instant." ),
         0, 1000, 100, COPT_NO_HIDE, "%i%%"
       );

    add( "GROWTH_SCALING", world_default, translate_marker( "Growth scaling percentage" ),
         translate_marker( "Sets the time of crop growth in percents.  '50' is two times faster than default, '200' is two times longer.  '0' automatically scales growth time to match the world's season length." ),
         0, 1000, 0, COPT_NO_HIDE, "%i%%"
       );

    add( "ETERNAL_SEASON", world_default, translate_marker( "Eternal season" ),
         translate_marker( "Keep the initial season for ever." ),
         false
       );

    add_empty_line();

    add( "WANDER_SPAWNS", world_default, translate_marker( "Wander spawns" ),
         translate_marker( "Emulation of zombie hordes.  Zombie spawn points wander around cities and may go to noise.  Must reset world directory after changing for it to take effect." ),
         false
       );

    add( "BLACK_ROAD", world_default, translate_marker( "Surrounded start" ),
         translate_marker( "If true, spawn zombies at shelters.  Makes the starting game a lot harder." ),
         false
       );

    add_empty_line();

    add( "STATIC_NPC", world_default, translate_marker( "Static NPCs" ),
         translate_marker( "If true, static NPCs will spawn at pre-defined locations.  Requires world reset." ),
         true
       );

    add( "STARTING_NPC", world_default, translate_marker( "Starting NPCs spawn" ),
         translate_marker( "Determines whether starting NPCs should spawn, and if they do, how exactly." ),
    { { "never", translate_marker( "Never" ) }, { "always", translate_marker( "Always" ) }, { "scenario", translate_marker( "Scenario-based" ) } },
    "scenario"
       );

    get_option( "STARTING_NPC" ).setPrerequisite( "STATIC_NPC" );

    add( "RANDOM_NPC", world_default, translate_marker( "Random NPCs" ),
         translate_marker( "If true, the game will randomly spawn NPCs during gameplay." ),
         false
       );

    add_empty_line();

    add( "RAD_MUTATION", world_default, translate_marker( "Mutations by radiation" ),
         translate_marker( "If true, radiation causes the player to mutate." ),
         true
       );

    add_empty_line();

    add( "POCKET_SIMULATION_LEVEL", world_default, translate_marker( "Pocket Dimension Simulation" ),
         translate_marker( "How to handle the last visited pocket dimension. "
                           "'Off' unloads normally. 'None' keeps loaded but frozen for fast travel. "
                           "'Minimal' simulates fields only (fire, gas). "
                           "'Moderate' adds vehicle systems (solar charging). "
    "'Full' simulates everything including off-screen combat." ), {
        { "off", translate_marker( "Off" ) },
        { "none", translate_marker( "None (Fast Travel)" ) },
        { "minimal", translate_marker( "Minimal (Fields)" ) },
        { "moderate", translate_marker( "Moderate (Fields + Vehicles)" ) },
        { "full", translate_marker( "Full (Everything)" ) }
    },
    "off" );

    add_empty_line();

    add( "CHARACTER_POINT_POOLS", world_default, translate_marker( "Character point pools" ),
         translate_marker( "Allowed point pools for character generation." ),
    { { "any", translate_marker( "Any" ) }, { "multi_pool", translate_marker( "Multi-pool only" ) }, { "no_freeform", translate_marker( "No freeform" ) } },
    "any"
       );

    add( "ENFORCE_PROFESSION_AGE_RANGE", world_default,
         translate_marker( "Enforce profession age ranges" ),
         translate_marker( "When enabled, character ages are constrained by profession-defined age ranges when available." ),
         true );

    add( "DISABLE_LIFTING", world_default,
         translate_marker( "Disables lifting requirements for vehicle parts." ),
         translate_marker( "If true, strength checks and/or lifting qualities no longer need to be met in order to change parts." ),
         false, COPT_ALWAYS_HIDE
       );
}

void options_manager::add_options_android()
{
}


void options_manager::add_options_coop()
{
    add_option_group( coop, Group( "coop",
                                   to_translation( "Co-op" ),
                                   to_translation( "Settings for cooperative multiplayer." ) ),
    [&]( const std::string & page_id ) {
        add( "COOP_PORT", page_id, translate_marker( "Co-op Port" ),
             translate_marker( "TCP port used when hosting or joining a co-op game.  Both players must use the same port." ),
             1024, 65535, 8080 );
    } );
}
