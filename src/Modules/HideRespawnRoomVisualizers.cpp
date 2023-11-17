#include "HideRespawnRoomVisualizers.h"

#include "../Flask.h"
#include "EntityListener.h"
#include <client_class.h>
#include <iclientnetworkable.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
HideRespawnRoomVisualizers::HideRespawnRoomVisualizers(Plugin& plugin) : m_plugin(plugin)
{
    m_plugin.entity_listener().add_create_entity_callback([this](auto entity) { on_create_entity(entity); });
}

HideRespawnRoomVisualizers::~HideRespawnRoomVisualizers()
{
    if (m_respawn_room_visualizer_draw_model_function && m_respawn_room_visualizer_draw_model_function_vtable_entry)
        *m_respawn_room_visualizer_draw_model_function_vtable_entry = m_respawn_room_visualizer_draw_model_function;

    m_respawn_room_visualizer_draw_model_function = nullptr;
    m_respawn_room_visualizer_draw_model_function_vtable_entry = nullptr;
}

void HideRespawnRoomVisualizers::on_create_entity(IClientNetworkable* entity)
{
    if (entity->GetClientClass()->GetName() == "CFuncRespawnRoomVisualizer"sv)
    {
        if (!m_respawn_room_visualizer_draw_model_function)
        {
            auto draw_model_vtable_entry = m_respawn_room_visualizer_draw_model_function_vtable_entry =
                &(*reinterpret_cast<C_FuncRespawnRoomVisualizerDrawModelFn**>(
                    entity->GetIClientUnknown()->GetClientRenderable()))[10];

            m_respawn_room_visualizer_draw_model_function = *draw_model_vtable_entry;
            *draw_model_vtable_entry = respawn_room_visualizer_draw_model;
        }
    }
}

int HideRespawnRoomVisualizers::respawn_room_visualizer_draw_model(C_BaseEntity* self, int flags)
{
    if (Plugin::the().hide_respawn_room_visualizers().m_flask_render_hide_respawn_room_visualizers->GetBool())
        return 1;

    return Plugin::the().hide_respawn_room_visualizers().m_respawn_room_visualizer_draw_model_function(self, flags);
}
}