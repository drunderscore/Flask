#pragma once

#include <spdlog/sinks/base_sink.h>
#include <tier0/dbg.h>

namespace Flask
{
template<typename TMutex>
class Tier0Logger : public spdlog::sinks::base_sink<TMutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        // Setting the precision for a string format specifier allows us to set the maximum number of characters printed
        // This allows us to prevent copying the string somewhere just to null-terminate it.
        // Technically if your string has a null somewhere in it, it will still get null-terminated... but who would do
        // that?

        switch (msg.level)
        {
            case spdlog::level::trace:
            case spdlog::level::debug:
                ConDColorMsg(m_debug_and_trace_console_color, "%.*s\n", msg.payload.size(), msg.payload.data());
                break;
            case spdlog::level::info:
                Msg("%.*s\n", msg.payload.size(), msg.payload.data());
                break;
            case spdlog::level::warn:
            case spdlog::level::err:
            case spdlog::level::critical: // Perhaps we should use a different color for err and critical?
                Warning("%.*s\n", msg.payload.size(), msg.payload.data());
                break;
            default:
                Warning("Unhandled logging level in Tier0Logger -- you are missing spew output!\n");
        }
    }
    void flush_() override {}

private:
    // This should be a static, but I don't feel like we need a cpp file just for this logger...
    Color m_debug_and_trace_console_color{84, 205, 209, 255};
};

using Tier0LoggerMultiThreaded = Tier0Logger<std::mutex>;
using Tier0LoggerSingleThreaded = Tier0Logger<spdlog::details::null_mutex>;
}