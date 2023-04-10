#pragma once

#include "../Forward.h"
#include "../Structures/Forward.h"
#include <JMP/Signature.h>

namespace Flask::Modules
{
class Camera
{
public:
    explicit Camera(const Plugin&);

    void set_observe_target(int index);

    inline Structures::C_HLTVCamera& camera() { return *m_hltv_camera_singleton_getter(); }

private:
    static JMP::Signature s_call_to_hltv_camera_singleton_getter;
    static JMP::Signature s_hltv_camera_set_primary_target_function;

    typedef Structures::C_HLTVCamera* (*C_HLTVCameraSingletonGetterFn)();

#ifdef POSIX
    typedef __attribute__((cdecl)) void (*C_HLTVCameraSetPrimaryTargetFn)(Structures::C_HLTVCamera*, int);
#else
    typedef void(__thiscall* C_HLTVCameraSetPrimaryTargetFn)(Structures::C_HLTVCamera*, int);
#endif

    C_HLTVCameraSingletonGetterFn m_hltv_camera_singleton_getter{};
    C_HLTVCameraSetPrimaryTargetFn m_hltv_camera_set_primary_target_function{};
};
}