#pragma once

#include "../Forward.h"
#include <JMP/Signature.h>
#include <subhook.h>

namespace Flask::Modules
{
class HUD
{
public:
    explicit HUD(Plugin&);

private:
    static JMP::Signature s_spectator_target_id_calculate_target_index;

#ifdef POSIX
    static int on_spectator_target_id_calculate_target_index(void* self, void* player);
#else
    static int __thiscall on_spectator_target_id_calculate_target_index(void* self, void* player);
#endif

    subhook::Hook m_spectator_target_id_calculate_target_index_subhook;
};
}
