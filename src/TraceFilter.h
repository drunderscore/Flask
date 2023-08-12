#pragma once

#include "Forward.h"
#include <IEngineTrace.h>
#include <cinttypes>
#include <set>
#include <vector>

namespace Flask
{
class TraceFilter : public ITraceFilter
{
public:
    // FIXME: This should take an std::span<int>, but can't use an initializer list then...
    TraceFilter(Plugin& plugin, const std::vector<int>& entity_indices);

    // FIXME: This would likely benefit from including StandardFilterRules (from the game)
    bool ShouldHitEntity(IHandleEntity* entity, int) override;

    TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }

    void set_entity_ignored(int entity_index) { m_entities_to_ignore.insert(entity_index); }
    void set_ignore_players() { m_ignore_players = true; }

private:
    Plugin& m_plugin;
    std::set<int> m_entities_to_ignore;
    bool m_ignore_players{};
};
}
