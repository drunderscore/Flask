#pragma once

#include <map>
#include <optional>

namespace Flask
{
class GameState
{
public:
    bool update_observe_target(int index)
    {
        auto has_changed = m_observe_target != index;

        m_observe_target = index;

        return has_changed;
    }

    std::optional<int> observe_target() const { return m_observe_target; }

private:
    std::optional<int> m_observe_target{};
};
}