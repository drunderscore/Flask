#pragma once

#include "../Badge.h"
#include "../Forward.h"
#include "../ManagedConCommand.h"
#include "Forward.h"
#undef clamp
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

class IClientEntity;

namespace Flask::Modules
{
class AdditionalPointsOfInterest
{
public:
    explicit AdditionalPointsOfInterest(Plugin& plugin) : m_plugin(plugin) {}

    struct StickyTrap
    {
        std::vector<IClientEntity*> stickies;
        uint8_t team;
    };

    std::vector<StickyTrap> collect_sticky_traps() const;

private:
    static std::optional<uint8_t> parse_team(const char* value);

    static void flask_additional_poi_spectate_sentry(const CCommand&);
    static void flask_additional_poi_spectate_sticky_trap(const CCommand&);
    static void flask_additional_poi_display(const CCommand&);
    static void flask_additional_poi_spectate_passtime_ball(const CCommand&);

    Plugin& m_plugin;

    ManagedConVar m_flask_additional_poi_sticky_trap_maximum_distance{
        "flask_additional_poi_sticky_trap_maximum_distance",
        "150.0",
        FCVAR_NONE,
        "The maximum distance two stickies can be between each other to consider it a trap",
    };

    ManagedConVar m_flask_additional_poi_sticky_trap_minimum_stickies{
        "flask_additional_poi_sticky_trap_minimum_stickies",
        "3",
        FCVAR_NONE,
        "The minimum number of stickies to consider it a trap",
    };

    ManagedConCommand m_flask_additional_poi_spectate_sticky_trap{
        "flask_additional_poi_spectate_sticky_trap",
        flask_additional_poi_spectate_sticky_trap,
    };

    ManagedConCommand m_flask_additional_poi_spectate_sentry{
        "flask_additional_poi_spectate_sentry",
        flask_additional_poi_spectate_sentry,
    };

    ManagedConCommand m_flask_additional_poi_spectate_passtime_ball{
        "flask_additional_poi_spectate_passtime_ball",
        flask_additional_poi_spectate_passtime_ball,
    };

    ManagedConCommand m_flask_additional_poi_display{
        "flask_additional_poi_display",
        flask_additional_poi_display,
    };
};
}
