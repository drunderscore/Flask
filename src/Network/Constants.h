#pragma once

#include <string_view>

#define STRINGIFY_HELPER(s) #s
#define STRINGIFY(s) STRINGIFY_HELPER(s)

// Commands are sent from a client to the server, to perform a specific action.
#define ENUMERATE_FLASK_COMMANDS(M) M(observe_target)

// Events are sent from the server to client(s), in response to some stateful change.
#define ENUMERATE_FLASK_EVENTS(M) M(observe_target)

namespace Flask::Network
{
struct Commands
{
#define FLASK_COMMAND_ENUMERATION_MACRO(name) static constexpr std::string_view name = STRINGIFY(name);
    ENUMERATE_FLASK_COMMANDS(FLASK_COMMAND_ENUMERATION_MACRO)
#undef FLASK_COMMAND_ENUMERATION_MACRO
};

struct Events
{
#define FLASK_EVENT_ENUMERATION_MACRO(name) static constexpr std::string_view name = STRINGIFY(name);
    ENUMERATE_FLASK_EVENTS(FLASK_EVENT_ENUMERATION_MACRO)
#undef FLASK_EVENT_ENUMERATION_MACRO
};
}

#undef STRINGIFY_HELPER
#undef STRINGIFY