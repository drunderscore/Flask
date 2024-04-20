#include "ErrorReporting.h"
#include "../Flask.h"
#include "../Structures/IVEngineClient.h"
#include "Interfaces.h"
#include <cstdio>
#include <icommandline.h>
#include <sentry.h>
#include <spdlog/spdlog.h>
#include <steam/isteamfriends.h>
#include <steam/isteamuser.h>

using namespace std::string_view_literals;

extern "C"
{
    extern ISteamFriends* SteamAPI_SteamFriends_v017();
    extern ISteamUser* SteamAPI_SteamUser_v021();
}

namespace Flask::Modules
{
std::string_view ErrorReporting::s_sentry_dsn =
    "https://0ecba955b71040e5bc8fcfc6670329cf@o1262768.ingest.sentry.io/4504023999315968";

// In MSVC, C++ exceptions are implemented in terms of SEHs, which are just normal exceptions and crashes, akin to
// signals (though Windows can also report some signals.)
// Because breakpad ends up handling all the exceptions (including the C++ ones), we never invoke the termination
// handler that MSVC provided to handle C++ ones, and so the termination handler is never invoked. This will extract the
// exception info from those and then continue into the original unhandled exception filter that sentry (probably) put.
#ifdef _WIN32
LONG ErrorReporting::unhandled_exception_filter(EXCEPTION_POINTERS* exception_info)
{
    // If this is a C++ exception, and we have at least 2 parameters, we can extract the C++ exception.
    if (exception_info->ExceptionRecord->ExceptionCode == s_msvc_cpp_exception_code &&
        exception_info->ExceptionRecord->NumberParameters >= 2)
    {
        // Parameter index 1 is a pointer to the thrown object.
        // FIXME: We assume what was throw was an std::exception, but obviously you can throw any type at all...
        if (auto exception =
                reinterpret_cast<std::exception*>(exception_info->ExceptionRecord->ExceptionInformation[1]))
        {
            auto breadcrumb = sentry_value_new_breadcrumb("error", "Unhandled C++ exception");

            sentry_value_set_by_key(breadcrumb, "level", sentry_value_new_string("error"));
            sentry_value_set_by_key(breadcrumb, "category", sentry_value_new_string("exception"));

            auto data = sentry_value_new_object();

            sentry_value_set_by_key(data, "name", sentry_value_new_string(typeid(*exception).name()));
            sentry_value_set_by_key(data, "message", sentry_value_new_string(exception->what()));

            sentry_value_set_by_key(breadcrumb, "data", data);

            sentry_add_breadcrumb(breadcrumb);
        }
    }

    return Plugin::the().error_reporting().m_sentry_unhandled_exception_filter(exception_info);
}
#endif

ErrorReporting::ErrorReporting(Plugin& plugin) : m_plugin(plugin)
{
    if (s_enabled)
    {
        if (IsWindows() && !CommandLine()->FindParm("-nominidumps"))
            Error("You must add -nominidumps to your launch options to use Flask.\n\nThis allows for Flask error "
                  "reporting to function.");

        if (auto sentry_options = sentry_options_new())
        {
            sentry_options_set_dsn_n(sentry_options, s_sentry_dsn.data(), s_sentry_dsn.length());
            sentry_options_set_environment(sentry_options, "release");
            sentry_options_set_release_n(sentry_options, Plugin::s_git_revision.data(),
                                         Plugin::s_git_revision.length());

            sentry_init(sentry_options);

#ifdef _WIN32
            m_sentry_unhandled_exception_filter =
                SetUnhandledExceptionFilter(ErrorReporting::unhandled_exception_filter);
#endif

            auto user = sentry_value_new_object();

            if (auto steam_user = SteamAPI_SteamUser_v021())
                sentry_value_set_by_key(
                    user, "id",
                    sentry_value_new_string(std::to_string(steam_user->GetSteamID().ConvertToUint64()).c_str()));

            if (auto steam_friends = SteamAPI_SteamFriends_v017())
                sentry_value_set_by_key(user, "username", sentry_value_new_string(steam_friends->GetPersonaName()));

            sentry_set_user(user);

            auto& engine_client = m_plugin.interfaces().engine_client();

            auto app = sentry_value_new_object();
            sentry_value_set_by_key(app, "app_version",
                                    sentry_value_new_string(engine_client.GetProductVersionString()));
            sentry_set_context("app", app);

            m_original_terminate_handler = std::set_terminate(termination_handler);
        }
        else
        {
            spdlog::error("Failed to construct sentry options, not initializing sentry!");
        }
    }
}

ErrorReporting::~ErrorReporting()
{
    if (m_original_terminate_handler)
        std::set_terminate(m_original_terminate_handler);

#ifdef _WIN32
    if (m_sentry_unhandled_exception_filter)
        SetUnhandledExceptionFilter(m_sentry_unhandled_exception_filter);
#endif

    sentry_close();
}

void ErrorReporting::level_init_post_entity(Badge<Plugin>)
{
    auto& engine_client = m_plugin.interfaces().engine_client();
    ConVarRef tv_transmitall("tv_transmitall");

    auto breadcrumb = sentry_value_new_breadcrumb("default", "Level initialized");

    sentry_value_set_by_key(breadcrumb, "level", sentry_value_new_string("info"));
    sentry_value_set_by_key(breadcrumb, "category", sentry_value_new_string("client_state"));

    auto data = sentry_value_new_object();

    sentry_value_set_by_key(data, "map", sentry_value_new_string(engine_client.GetLevelName()));
    sentry_value_set_by_key(data, "is_stv", sentry_value_new_bool(engine_client.IsHLTV()));
    sentry_value_set_by_key(data, "is_demo", sentry_value_new_bool(engine_client.IsPlayingDemo()));
    sentry_value_set_by_key(data, "tv_transmitall", sentry_value_new_bool(tv_transmitall.GetBool()));

    sentry_value_set_by_key(breadcrumb, "data", data);

    sentry_add_breadcrumb(breadcrumb);
}

void ErrorReporting::level_shutdown_pre_entity(Badge<Plugin>)
{
    auto breadcrumb = sentry_value_new_breadcrumb("default", "Level shutdown");

    sentry_value_set_by_key(breadcrumb, "level", sentry_value_new_string("info"));
    sentry_value_set_by_key(breadcrumb, "category", sentry_value_new_string("client_state"));

    sentry_add_breadcrumb(breadcrumb);
}

void ErrorReporting::termination_handler()
{
    auto message = "Unhandled exception"sv;
    auto exception_name = "Unknown unhandled exception"sv;

    auto current_exception = std::current_exception();

    try
    {
        std::rethrow_exception(current_exception);
    }
    catch (const std::exception& ex)
    {
        exception_name = typeid(ex).name();
        message = ex.what();
    }
    catch (...)
    {
    }

    auto exception =
        sentry_value_new_exception_n(exception_name.data(), exception_name.length(), message.data(), message.length());

    auto stack_trace = sentry_value_new_stacktrace(nullptr, 0);
    sentry_value_set_by_key(exception, "stacktrace", stack_trace);

    auto event = sentry_value_new_event();
    sentry_event_add_exception(event, exception);

    sentry_value_set_by_key(event, "level", sentry_value_new_string("fatal"));

    sentry_capture_event(event);
    sentry_close();

    std::abort();
}
}