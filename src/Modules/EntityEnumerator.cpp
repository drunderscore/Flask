#include "EntityEnumerator.h"
#include "../Flask.h"
#include "Interfaces.h"
#include <icliententity.h>
#include <icliententitylist.h>

namespace Flask::Modules
{
void EntityEnumerator::all(std::function<IterationDecision(IClientEntity*)> callback,
                           uint32_t starting_from_entity_index)
{
    auto highest_entity_index = m_plugin.interfaces().client_entity_list().GetHighestEntityIndex();

    // -1 means there are no entities.
    if (highest_entity_index == -1)
        return;

    for (auto i = starting_from_entity_index; i < highest_entity_index; i++)
    {
        auto entity = m_plugin.interfaces().client_entity_list().GetClientEntity(i);

        if (entity && callback(entity) == IterationDecision::Stop)
            break;
    }
}

std::vector<IClientEntity*> EntityEnumerator::collect_all(std::function<bool(IClientEntity*)> callback,
                                                          uint32_t starting_from_entity_index)
{
    std::vector<IClientEntity*> entities;

    all(
        [&entities, callback = std::move(callback)](auto entity) {
            if (callback(entity))
                entities.push_back(entity);

            return IterationDecision::Continue;
        },
        starting_from_entity_index);

    return entities;
}

void EntityEnumerator::in_sphere(std::function<IterationDecision(IClientEntity*)> callback, const Vector& origin,
                                 float radius)
{
    FunctionalPartitionEnumerator enumerator(std::move(callback));

    m_plugin.interfaces().spatial_partition().EnumerateElementsInSphere(PARTITION_CLIENT_NON_STATIC_EDICTS, origin,
                                                                        radius, false, &enumerator);
}

std::vector<IClientEntity*> EntityEnumerator::collect_in_sphere(std::function<bool(IClientEntity*)> callback,
                                                                const Vector& origin, float radius)
{
    std::vector<IClientEntity*> entities;

    in_sphere(
        [&entities, callback = std::move(callback)](auto entity) {
            if (callback(entity))
                entities.push_back(entity);

            return IterationDecision::Continue;
        },
        origin, radius);

    return entities;
}
}