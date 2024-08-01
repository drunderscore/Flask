#pragma once

#include "../Badge.h"
#include "../Forward.h"
#include "../ManagedConCommand.h"
#include "Forward.h"

namespace Flask::Modules
{
class CameraRig
{
public:
    explicit CameraRig(Plugin&);

    bool is_controlling() const { return m_flask_camera_rig_enabled->GetBool(); }

    void calc_view(Badge<Camera>, Vector& origin, QAngle&, float& fov);

private:
    Plugin& m_plugin;

    ManagedConVar m_flask_camera_rig_enabled{
        "flask_camera_rig_enabled", "0", FCVAR_NONE, nullptr, on_flask_camera_rig_enabled_change,
    };

    ManagedConVar m_flask_camera_rig_debug{
        "flask_camera_rig_debug",
        "0",
        FCVAR_NONE,
    };

    ManagedConVar m_flask_camera_rig_angles_blend_factor{
        "flask_camera_rig_angles_blend_factor",
        "4.6875",
        FCVAR_NONE,
    };

    ManagedConVar m_flask_camera_rig_fov_blend_factor{
        "flask_camera_rig_fov_blend_factor",
        "1.0",
        FCVAR_NONE,
    };

    // NOTE: This default values comes from s_default_fov
    ManagedConVar m_flask_camera_rig_fov{
        "flask_camera_rig_fov", "90.0", FCVAR_NONE, nullptr, true, 1.0f, true, 179.0f,
    };

    ManagedConVar m_flask_camera_rig_angles_roll_sensitivity{
        "flask_camera_rig_angles_roll_sensitivity",
        "150.0",
        FCVAR_NONE,
    };

    ManagedConVar m_flask_camera_rig_fov_sensitivity{
        "flask_camera_rig_fov_sensitivity",
        "50.0",
        FCVAR_NONE,
    };

    ManagedConCommand m_flask_camera_rig_angles_roll_left_start{
        "+flask_camera_rig_angles_roll_left",
        flask_camera_rig_angles_roll_left_start,
    };

    ManagedConCommand m_flask_camera_rig_angles_roll_left_end{
        "-flask_camera_rig_angles_roll_left",
        flask_camera_rig_angles_roll_left_end,
    };

    ManagedConCommand m_flask_camera_rig_angles_roll_right_start{
        "+flask_camera_rig_angles_roll_right",
        flask_camera_rig_angles_roll_right_start,
    };

    ManagedConCommand m_flask_camera_rig_angles_roll_right_end{
        "-flask_camera_rig_angles_roll_right",
        flask_camera_rig_angles_roll_right_end,
    };

    ManagedConCommand m_flask_camera_rig_fov_in_start{
        "+flask_camera_rig_fov_in",
        flask_camera_rig_fov_in_start,
    };

    ManagedConCommand m_flask_camera_rig_fov_in_end{
        "-flask_camera_rig_fov_in",
        flask_camera_rig_fov_in_end,
    };

    ManagedConCommand m_flask_camera_rig_fov_out_start{
        "+flask_camera_rig_fov_out",
        flask_camera_rig_fov_out_start,
    };

    ManagedConCommand m_flask_camera_rig_fov_out_end{
        "-flask_camera_rig_fov_out",
        flask_camera_rig_fov_out_end,
    };

    QAngle m_angles{0.0f, 0.0f, 0.0f};
    float m_fov{};

    float m_roll{};

    bool m_roll_left{};
    bool m_roll_right{};
    bool m_fov_in{};
    bool m_fov_out{};

    void draw_debug(const Vector& calculated_origin, const QAngle& calculated_angles, const float& calculated_fov,
                    const QAngle& destination_angles, const float& destination_fov) const;

    static constexpr float s_default_fov = 90.0f;

    static float lerp_theta(float a, float b, float t);

    static void flask_camera_rig_angles_roll_left_start(const CCommand&);
    static void flask_camera_rig_angles_roll_left_end(const CCommand&);

    static void flask_camera_rig_angles_roll_right_start(const CCommand&);
    static void flask_camera_rig_angles_roll_right_end(const CCommand&);

    static void flask_camera_rig_fov_in_start(const CCommand&);
    static void flask_camera_rig_fov_in_end(const CCommand&);

    static void flask_camera_rig_fov_out_start(const CCommand&);
    static void flask_camera_rig_fov_out_end(const CCommand&);

    static void on_flask_camera_rig_enabled_change(IConVar*, const char* old_value, float old_value_floating);
};
}
