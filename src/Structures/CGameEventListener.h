#pragma once

namespace Flask::Structures
{
struct CGameEventListener
{
    virtual void fire_game_event(void* event) = 0;

    bool has_registered_for_events;
};
}