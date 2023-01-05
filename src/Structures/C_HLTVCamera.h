#pragma once

#include "CGameEventListener.h"
#include <mathlib/vector.h>

namespace Flask::Structures
{
struct C_HLTVCamera : public CGameEventListener
{
    virtual ~C_HLTVCamera() = 0;

    int camera_mode;
    int camera_man;
    Vector camera_origin;
    QAngle camera_angle;
    int target_1;
    int target_2;
    float fov;
    float offset;
    float distance;
    float last_distance;
    float theta;
    float phi;
    float inertia;
    float last_angle_update_time;
    bool entity_packet_received;
    int number_of_spectators;
    char title_text[64];
    // Missing: CUserCmd m_LastCmd;
    // Missing: Vector m_vecVelocity;
};
}