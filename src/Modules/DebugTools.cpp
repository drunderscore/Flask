#include "DebugTools.h"
#include "../Flask.h"
#include "Interfaces.h"
#include "spdlog/spdlog.h"
#include <icliententity.h>
#include <icliententitylist.h>

namespace Flask::Modules
{
void DebugTools::flask_debug_entity_release(const CCommand& args)
{
    if (args.ArgC() < 2)
        spdlog::error("Usage: flask_debug_entity_release <entity id>");

    auto entity = Plugin::the().interfaces().client_entity_list().GetClientEntity(atoi(args.Arg(1)));
    if (!entity)
    {
        spdlog::error("Unable to find entity");
        return;
    }

    auto handle = entity->GetRefEHandle();
    spdlog::info("Releasing entity {} with serial {}", handle.GetEntryIndex(), handle.GetSerialNumber());
    entity->Release();
}
}
