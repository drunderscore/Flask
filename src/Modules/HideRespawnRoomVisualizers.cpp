#include "HideRespawnRoomVisualizers.h"

#include "../Flask.h"
#include "EntityListener.h"
#include "NetworkCache.h"
#include <iclientnetworkable.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
HideRespawnRoomVisualizers::HideRespawnRoomVisualizers(Plugin& plugin) : m_plugin(plugin)
{
    auto func_respawn_room_visualizer_client_class =
        m_plugin.network_cache().find_client_class_by_name("CFuncRespawnRoomVisualizer"sv);

    m_func_respawn_room_visualizer_create_fn_original = func_respawn_room_visualizer_client_class->m_pCreateFn;

    func_respawn_room_visualizer_client_class->m_pCreateFn = [](auto index, auto serial) {
        auto& hide_respawn_room_visualizers = Plugin::the().hide_respawn_room_visualizers();

        auto entity = hide_respawn_room_visualizers.m_func_respawn_room_visualizer_create_fn_original(index, serial);
        hide_respawn_room_visualizers.on_create_entity(entity);

        return entity;
    };
}

HideRespawnRoomVisualizers::~HideRespawnRoomVisualizers()
{
    if (m_respawn_room_visualizer_draw_model_function && m_respawn_room_visualizer_draw_model_function_vtable_entry)
        *m_respawn_room_visualizer_draw_model_function_vtable_entry = m_respawn_room_visualizer_draw_model_function;

    m_respawn_room_visualizer_draw_model_function = nullptr;
    m_respawn_room_visualizer_draw_model_function_vtable_entry = nullptr;

    if (m_func_respawn_room_visualizer_create_fn_original)
    {
        m_plugin.network_cache().find_client_class_by_name("CFuncRespawnRoomVisualizer"sv)->m_pCreateFn =
            m_func_respawn_room_visualizer_create_fn_original;
        m_func_respawn_room_visualizer_create_fn_original = nullptr;
    }
}

void HideRespawnRoomVisualizers::on_create_entity(IClientNetworkable* entity)
{
    if (!m_respawn_room_visualizer_draw_model_function)
    {
        auto draw_model_vtable_entry = m_respawn_room_visualizer_draw_model_function_vtable_entry =
            &(*reinterpret_cast<C_FuncRespawnRoomVisualizerDrawModelFn**>(
                entity->GetIClientUnknown()->GetClientRenderable()))[10];

        m_respawn_room_visualizer_draw_model_function = *draw_model_vtable_entry;
        *draw_model_vtable_entry = respawn_room_visualizer_draw_model;

        m_plugin.network_cache().find_client_class_by_name("CFuncRespawnRoomVisualizer"sv)->m_pCreateFn =
            m_func_respawn_room_visualizer_create_fn_original;

        m_func_respawn_room_visualizer_create_fn_original = nullptr;
    }
}

int HideRespawnRoomVisualizers::respawn_room_visualizer_draw_model(C_BaseEntity* self, int flags)
{
    if (Plugin::the().hide_respawn_room_visualizers().m_flask_render_hide_respawn_room_visualizers->GetBool())
        return 1;

    return Plugin::the().hide_respawn_room_visualizers().m_respawn_room_visualizer_draw_model_function(self, flags);
}
}