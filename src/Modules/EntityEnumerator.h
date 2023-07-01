#pragma once

#include "../Forward.h"
#include <cstdint>
#include <functional>
#include <ispatialpartition.h>

class IClientEntity;

namespace Flask::Modules
{
// Although we are only given IHandleEntity whilst iterating partitions, this is inherited by IClientUnknown (which
// itself is inherited by IClientEntity as it's first inheritor, which is significant due to multiple inheritance),
// which means it's safe to statically cast it to it's base class, no need for a dynamic cast even.
//
// This is done to make it much easier to work with when interfacing.
class EntityEnumerator
{
public:
    enum class IterationDecision
    {
        Continue = ITERATION_CONTINUE,
        Stop = ITERATION_STOP
    };

    enum class CollectionDecision
    {
        Include,
        DoNotInclude,
        Stop
    };

    explicit EntityEnumerator(Plugin& plugin) : m_plugin(plugin) {}

    void all(std::function<IterationDecision(IClientEntity*)>, uint32_t starting_from_entity_index = 0);
    std::vector<IClientEntity*> collect_all(std::function<CollectionDecision(IClientEntity*)>,
                                            uint32_t starting_from_entity_index = 0);

    void in_sphere(std::function<IterationDecision(IClientEntity*)>, const Vector& origin, float radius);
    std::vector<IClientEntity*> collect_in_sphere(std::function<CollectionDecision(IClientEntity*)>,
                                                  const Vector& origin, float radius);

private:
    class FunctionalPartitionEnumerator : public IPartitionEnumerator
    {
    public:
        explicit FunctionalPartitionEnumerator(std::function<IterationDecision(IClientEntity*)> callback)
            : m_function(std::move(callback))
        {
        }

        IterationRetval_t EnumElement(IHandleEntity* entity) override
        {
            return static_cast<IterationRetval_t>(m_function(reinterpret_cast<IClientEntity*>(entity)));
        };

    private:
        std::function<IterationDecision(IClientEntity*)> m_function;
    };

    Plugin& m_plugin;
};
}
