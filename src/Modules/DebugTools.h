#pragma once

#include "../ManagedConCommand.h"

namespace Flask::Modules
{
class DebugTools
{
private:
    static void flask_debug_entity_release(const CCommand& args);

    ManagedConCommand m_flask_debug_entity_release{
        "flask_debug_entity_release",
        flask_debug_entity_release,
    };
};
}
