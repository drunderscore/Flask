#pragma once

#include "../Forward.h"
#include "../ManagedConCommand.h"
#include <basehandle.h>
#include <client_class.h>

class C_BaseEntity;
class IClientNetworkable;

namespace Flask::Modules
{
class HideRespawnRoomVisualizers
{
public:
    explicit HideRespawnRoomVisualizers(Plugin&);
    ~HideRespawnRoomVisualizers();

private:
#ifdef POSIX
    static int respawn_room_visualizer_draw_model(C_BaseEntity*, int);
#else
    static int __thiscall respawn_room_visualizer_draw_model(C_BaseEntity*, int);
#endif

    void on_create_entity(IClientNetworkable*);

    Plugin& m_plugin;

    CreateClientClassFn m_func_respawn_room_visualizer_create_fn_original{};

    using C_FuncRespawnRoomVisualizerDrawModelFn = decltype(respawn_room_visualizer_draw_model)*;
    C_FuncRespawnRoomVisualizerDrawModelFn* m_respawn_room_visualizer_draw_model_function_vtable_entry{};
    C_FuncRespawnRoomVisualizerDrawModelFn m_respawn_room_visualizer_draw_model_function{};
    ManagedConVar m_flask_render_hide_respawn_room_visualizers{
        "flask_render_hide_respawn_room_visualizers", "1", FCVAR_NONE, "Should the respawn room visualizers be hidden"};
};
}