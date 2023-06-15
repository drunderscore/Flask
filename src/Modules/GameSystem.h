#pragma once

// Explicitly put this at the top -- needed for igamesystem.h
#include <platform.h>

#include "../Forward.h"
#include <../game/shared/igamesystem.h>
#include <JMP/Signature.h>

class IGameSystem;

namespace Flask::Modules
{
class GameSystem : public IGameSystemPerFrame
{
public:
    explicit GameSystem(Plugin&);
    ~GameSystem() override;

protected:
    char const* Name() override;
    bool Init() override { return true; }

    void PostInit() override {}
    void Shutdown() override {}
    void LevelInitPreEntity() override {}
    void LevelInitPostEntity() override;
    void LevelShutdownPreEntity() override {}
    void LevelShutdownPostEntity() override {}
    void OnSave() override {}
    void OnRestore() override {}
    void SafeRemoveIfDesired() override {}
    bool IsPerFrame() override { return true; }
    void PreRender() override {}
    void Update(float frametime) override;
    void PostRender() override {}

private:
    static JMP::Signature s_game_system_add_function;
    static JMP::Signature s_game_system_remove_function;

    typedef void (*IGameSystemAddFn)(IGameSystem*);
    typedef void (*IGameSystemRemoveFn)(IGameSystem*);

    Plugin& m_plugin;
    IGameSystemRemoveFn m_game_system_remove_function{};
};
}