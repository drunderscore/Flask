#pragma once

#include "../Badge.h"
#include "../Forward.h"
#include "../ManagedConCommand.h"
#include <cinttypes>
#include <exception>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#include <sentry.h>
#endif

namespace Flask::Modules
{
class ErrorReporting
{
public:
    explicit ErrorReporting(Plugin& plugin);
    ~ErrorReporting();

    void level_init_post_entity(Badge<Plugin>);
    void level_shutdown_pre_entity(Badge<Plugin>);

private:
    Plugin& m_plugin;
    std::terminate_handler m_original_terminate_handler{};

    static std::string_view s_sentry_dsn;

#ifdef _WIN32
    static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS*);

    static sentry_value_t on_crash(const sentry_ucontext_t*, sentry_value_t event, void* closure);
    sentry_value_t did_crash(const sentry_ucontext_t*, sentry_value_t event);
#endif

    static void termination_handler();

    static void flask_debug_crash_dereference_null(const CCommand&)
    {
        volatile int foo;
        foo = *static_cast<int*>(nullptr);
    }

    static void flask_debug_crash_throw_exception(const CCommand&)
    {
        throw std::runtime_error("Intentional user-initiated exception thrown");
    }

    ManagedConCommand m_flask_debug_crash_dereference_null{"flask_debug_crash_dereference_null",
                                                           flask_debug_crash_dereference_null,
                                                           "Intentionally dereference null", FCVAR_HIDDEN};
    ManagedConCommand m_flask_debug_crash_throw_exception{"flask_debug_crash_throw_exception",
                                                          flask_debug_crash_throw_exception,
                                                          "Intentionally throw exception", FCVAR_HIDDEN};

#ifdef _WIN32
    LPTOP_LEVEL_EXCEPTION_FILTER m_sentry_unhandled_exception_filter{};
#endif

    static constexpr uint32_t s_msvc_cpp_exception_code = 0xE06D7363;

    // Error reporting isn't enabled if we are in development.
#ifdef FLASK_DEVELOPMENT
    static constexpr bool s_enabled = false;
#else
    static constexpr bool s_enabled = true;
#endif
};
}