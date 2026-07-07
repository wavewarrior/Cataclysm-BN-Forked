#include "hud_manager.h"

#include "avatar.h"
#include "hud_top_bar.h"
#include "hud_health_stamina.h"
#include "hud_message_log.h"
#include "hud_minimap.h"

hud_manager::hud_manager()
    : top_bar( new hud_top_bar() ),
      health_stamina( new hud_health_stamina() ),
      message_log( new hud_message_log() ),
      minimap( new hud_minimap() )
{
}

hud_manager::~hud_manager()
{
    close_all();
}

auto hud_manager::instance() -> hud_manager &
{
    static hud_manager inst;
    return inst;
}

void hud_manager::open_all()
{
    // Open panels in z-order (back to front)
    health_stamina->open();
    top_bar->open();
    minimap->open();
    message_log->open(); // Message log on top so it's visible
}

void hud_manager::close_all()
{
    // Close in reverse order
    message_log->close();
    minimap->close();
    top_bar->close();
    health_stamina->close();
}

void hud_manager::update( avatar &u )
{
    // Update all panels
    top_bar->update( u );
    health_stamina->update( u );
    message_log->update( u );
    minimap->update( u );
}

auto hud_manager::is_active() const -> bool
{
    return top_bar->is_open() || health_stamina->is_open() ||
           message_log->is_open() || minimap->is_open();
}

auto hud_manager::get_top_bar() -> hud_top_bar *
{
    return top_bar.get();
}

auto hud_manager::get_health_stamina() -> hud_health_stamina *
{
    return health_stamina.get();
}

auto hud_manager::get_message_log() -> hud_message_log *
{
    return message_log.get();
}

auto hud_manager::get_minimap() -> hud_minimap *
{
    return minimap.get();
}
