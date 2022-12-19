#pragma once

#include "../Badge.h"
#include "../Forward.h"
#include "../ManagedConCommand.h"
#include "Forward.h"
#include <basehandle.h>

class C_BaseEntity;
class IHandleEntity;

namespace Flask::Modules
{
class HideRespawnRoomVisualizers
{
public:
    explicit HideRespawnRoomVisualizers(Plugin&);
    ~HideRespawnRoomVisualizers();

    void on_add_entity(Badge<EntityListener>, IHandleEntity&, CBaseHandle);

private:
#ifdef POSIX
    static __attribute__((cdecl)) int respawn_room_visualizer_draw_model(C_BaseEntity*, int);
#else
    static int __thiscall respawn_room_visualizer_draw_model(C_BaseEntity*, int);
#endif

    Plugin& m_plugin;

    using C_FuncRespawnRoomVisualizerDrawModelFn = decltype(respawn_room_visualizer_draw_model)*;
    C_FuncRespawnRoomVisualizerDrawModelFn* m_respawn_room_visualizer_draw_model_function_vtable_entry{};
    C_FuncRespawnRoomVisualizerDrawModelFn m_respawn_room_visualizer_draw_model_function{};
    ManagedConVar m_flask_render_hide_respawn_room_visualizers{
        "flask_render_hide_respawn_room_visualizers", "1", FCVAR_NONE, "Should the respawn room visualizers be hidden"};
};
}