#pragma once

#include <deque>
#include <string>
#include <vector>

namespace cata
{

constexpr size_t DEFAULT_LUA_LOG_CAPACITY = 100;

enum class LuaLogLevel {
    Input,
    Info,
    Warn,
    Error,
    DebugMsg,
};

struct lua_log_msg {
    LuaLogLevel level;
    std::string text;
};

class lua_log_handler
{
    public:
        lua_log_handler();
        ~lua_log_handler() = default;

        void set_log_capacity( size_t lines );

        void add( LuaLogLevel level, std::string &&text );

        void clear();

        const std::deque<lua_log_msg> &get_entries() const {
            return entries;
        }

        /// Everything added while the object lives, in order, whatever the capacity of the log.
        /// Does not touch the entries the log keeps. At most one is active at a time.
        class capture
        {
            public:
                explicit capture( lua_log_handler &handler );
                ~capture();
                capture( const capture & ) = delete;
                capture &operator=( const capture & ) = delete;

                const std::vector<lua_log_msg> &messages() const {
                    return collected;
                }

            private:
                lua_log_handler &owner;
                std::vector<lua_log_msg> collected;
        };

    private:
        std::deque<lua_log_msg> entries;
        size_t capacity = 0;
        std::vector<lua_log_msg> *capturing = nullptr;
};

lua_log_handler &get_lua_log_instance();

} // namespace cata


