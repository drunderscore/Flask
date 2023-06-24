#include "EntityListener.h"
#include "../Flask.h"
#include "AdditionalPointsOfInterest.h"
#include "HideRespawnRoomVisualizers.h"
#include "Interfaces.h"
#include <icliententity.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
EntityListener::EntityListener(Plugin& plugin)
{
    m_client_entity_list_on_add_entity_function_vtable_entry =
        calculate_client_entity_list_on_add_entity_vtable_entry(plugin.interfaces().client_entity_list());

    m_client_entity_list_on_add_entity_function = *m_client_entity_list_on_add_entity_function_vtable_entry;
    *m_client_entity_list_on_add_entity_function_vtable_entry = on_add_entity;
}

EntityListener::~EntityListener()
{
    if (m_client_entity_list_on_add_entity_function && m_client_entity_list_on_add_entity_function_vtable_entry)
        *m_client_entity_list_on_add_entity_function_vtable_entry = m_client_entity_list_on_add_entity_function;

    m_client_entity_list_on_add_entity_function = nullptr;
    m_client_entity_list_on_add_entity_function_vtable_entry = nullptr;
}

void EntityListener::on_add_entity(CClientEntityList* self, IHandleEntity* entity, CBaseHandle handle)
{
    Plugin::the().hide_respawn_room_visualizers().on_add_entity({}, *entity, handle);
    Plugin::the().entity_listener().m_client_entity_list_on_add_entity_function(self, entity, handle);
}
}