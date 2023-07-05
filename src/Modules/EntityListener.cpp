#include "EntityListener.h"
#include "../Flask.h"
#include "AdditionalPointsOfInterest.h"
#include "HideRespawnRoomVisualizers.h"
#include "Interfaces.h"

using namespace std::string_view_literals;

namespace Flask::Modules
{
EntityListener::EntityListener(Plugin& plugin)
{
    m_vtable = calculate_client_entity_list_vtable(plugin.interfaces().client_entity_list());

    m_client_entity_list_on_add_entity_function = *m_vtable->on_add_entity;
    m_vtable->on_add_entity = on_add_entity;

    m_client_entity_list_on_remove_entity_function = *m_vtable->on_remove_entity;
    m_vtable->on_remove_entity = on_remove_entity;
}

EntityListener::~EntityListener()
{
    m_vtable->on_add_entity = m_client_entity_list_on_add_entity_function;
    m_vtable->on_remove_entity = m_client_entity_list_on_remove_entity_function;

    m_client_entity_list_on_add_entity_function = nullptr;
    m_client_entity_list_on_remove_entity_function = nullptr;
    m_vtable = nullptr;
}

void EntityListener::on_add_entity(CClientEntityList* self, IHandleEntity* entity, CBaseHandle handle)
{
    Plugin::the().hide_respawn_room_visualizers().on_add_entity({}, *entity, handle);
    Plugin::the().entity_listener().m_client_entity_list_on_add_entity_function(self, entity, handle);
}

void EntityListener::on_remove_entity(CClientEntityList* self, IHandleEntity* entity, CBaseHandle handle)
{
    // NOTE: This hook isn't very useful, because it seems pretty busted.
    // All entities that come through here appear as CBaseEntity (at least, that's what their client class says)
    // This is likely a code path unused by Valve that has rotted away.
    Plugin::the().entity_listener().m_client_entity_list_on_remove_entity_function(self, entity, handle);
}
}