#pragma once

#include "../Signature.h"

class C_HLTVCamera;

namespace Flask::Modules
{
class Camera
{
public:
    Camera();

    void set_observe_target(int index);

private:
    static Signature s_call_to_hltv_camera_singleton_getter;
    static Signature s_hltv_camera_set_primary_target_function;

    typedef C_HLTVCamera* (*C_HLTVCameraSingletonGetterFn)();

#ifdef POSIX
    typedef __attribute__((cdecl)) void (*C_HLTVCameraSetPrimaryTargetFn)(C_HLTVCamera*, int);
#else
    typedef void(__thiscall* C_HLTVCameraSetPrimaryTargetFn)(C_HLTVCamera*, int);
#endif

    C_HLTVCameraSingletonGetterFn m_hltv_camera_singleton_getter{};
    C_HLTVCameraSetPrimaryTargetFn m_hltv_camera_set_primary_target_function{};
};
}