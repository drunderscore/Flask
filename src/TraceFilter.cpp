#include "TraceFilter.h"
#include "Flask.h"
#include "Modules/Interfaces.h"
#include "Structures/IVEngineClient.h"

namespace Flask
{
TraceFilter::TraceFilter(Plugin& plugin, const std::vector<int>& entity_indices) : m_plugin(plugin)
{
    std::copy(entity_indices.begin(), entity_indices.end(),
              std::inserter(m_entities_to_ignore, m_entities_to_ignore.begin()));
}

bool TraceFilter::ShouldHitEntity(IHandleEntity* entity, int)
{
    auto index = entity->GetRefEHandle().GetEntryIndex();

    if (m_ignore_players && index >= 1 && index <= m_plugin.interfaces().engine_client().GetMaxClients())
        return false;

    return !m_entities_to_ignore.contains(index);
}
}