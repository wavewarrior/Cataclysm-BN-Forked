#pragma once

#include <climits>

class player;
class item;

class item_reload_option
{
    public:
        item_reload_option() = default;

        item_reload_option( const item_reload_option & );
        item_reload_option &operator=( const item_reload_option & );

        item_reload_option( const player* who, item* target, const item* parent, item& ammo );

        const player *who = nullptr;
        item *target = nullptr;
        item *ammo;

        int qty() const { return qty_; }
        void qty( int val );

        int moves() const;

        explicit operator bool() const { return who && target && ammo && qty_ > 0; }

    private:
        int qty_ = 0;
        int max_qty = INT_MAX;
        const item *parent = nullptr;
};
