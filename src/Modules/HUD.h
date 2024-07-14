#pragma once

#include "../Forward.h"
#include "../ManagedConCommand.h"
#include <JMP/Signature.h>
#include <set>
#include <subhook.h>

namespace Flask::Modules
{
class HUD
{
public:
    explicit HUD(Plugin&);

private:
    static JMP::Signature s_spectator_target_id_calculate_target_index;
    static JMP::Signature s_tf_player_panel_update;

#ifdef POSIX
    static int on_spectator_target_id_calculate_target_index(void* self, void* player);
    static bool on_tf_player_panel_update(void* self);
#else
    static int __thiscall on_spectator_target_id_calculate_target_index(void* self, void* player);
    static bool __thiscall on_tf_player_panel_update(void* self);
#endif

    static void on_flask_hud_spectator_gui_player_whitelist(IConVar*, const char* old_value, float old_value_float);

    // FIXME: We should prefer to include the new and updated Source headers/libraries that I use for building
    //        CastingEssentials Next.
    //        That would mean all these offsets are automatically correct just be creating a structure of the same
    //        layout and inheriting vgui::EditablePanel.
    static constexpr uint32_t s_size_of_vgui_editable_panel = 368;
    static constexpr uint32_t s_offset_of_tf_player_panel_player_index = 192;
    static constexpr uint32_t s_offset_of_tf_player_panel_steam_id = 208;

    subhook::Hook m_spectator_target_id_calculate_target_index_subhook;
    subhook::Hook m_tf_player_panel_subhook;
    // This is kept up-to-date with m_flask_hud_spectator_gui_player_whitelist. Used for comparisons for much faster
    // access.
    std::set<uint64_t> m_spectator_gui_player_steam_id_whitelist;

    ManagedConVar m_flask_hud_spectator_gui_player_whitelist{
        "flask_hud_spectator_gui_player_whitelist",
        "",
        FCVAR_NONE,
        "A comma-separated list of Steam 64 IDs that will appear on the spectator GUI, or empty to ignore.",
        on_flask_hud_spectator_gui_player_whitelist,
    };

    ManagedConVar m_flask_hud_spectator_gui_player_max_distance{
        "flask_hud_spectator_gui_player_max_distance",
        "0.0",
        FCVAR_NONE,
        "The maximum distance the camera can be from a player to appear on the spectator GUI, or 0 to ignore.",
    };
};
}
