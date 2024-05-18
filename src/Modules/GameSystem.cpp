#include "GameSystem.h"
#include "../Flask.h"
#include "Server.h"

using namespace std::string_view_literals;

IGameSystem::~IGameSystem() = default;
IGameSystemPerFrame::~IGameSystemPerFrame() = default;

namespace Flask::Modules
{
#ifdef POSIX
JMP::Signature GameSystem::s_game_system_add_function(
    "55 48 89 E5 41 55 41 54 49 89 FC 53 48 83 EC 08 8B ? ? ? ? ? 8B ? ? ? ? ? 44 8D 6B 01 41 39 C5 0F ? ? ? ? ? 48 ? ? ? ? ? ? 44 ? ? ? ? ? ? 48 C1 E3 03 48 ? ? ? ? ? ? 4D 85 E4 4C 89 24 18 74 ?"sv);
JMP::Signature GameSystem::s_game_system_remove_function(
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 8B ? ? ? ? ? 85 D2 7E 36 48 ? ? ? ? ? ? 31 C0 48 89 Cf EB ? ? ? ? ? 83 C0 01 48 83 C7 08 39 C2 74 ?"sv);
#else
JMP::Signature GameSystem::s_game_system_add_function(
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC ? 8B ? ? ? ? ? 48 8B F9 8B ? ? ? ? ? 8B DA BE 04 00 00 00 44 8D 42 01 44 3B C0 0F 8E ? ? ? ?"sv);
JMP::Signature GameSystem::s_game_system_remove_function(
    "48 89 5C 24 08 57 48 83 EC 30 44 8B ? ? ? ? ? 33 DB 48 8B F9 8B D3 45 85 C0 7E ? 4C 8B ? ? ? ? ? 8B C2 49 39 3C C1 49 8D 0C C1 74 ? FF C2 41 3B D0 7C ?"sv);
#endif

GameSystem::GameSystem(Plugin& plugin) : m_plugin(plugin)
{
    auto game_system_add_function =
        reinterpret_cast<IGameSystemAddFn>(s_game_system_add_function.find_in(m_plugin.client_library_bytes()));

    if (!game_system_add_function)
        throw std::runtime_error("Failed to find IGameSystem::Add");

    if (!(m_game_system_remove_function = reinterpret_cast<IGameSystemRemoveFn>(
              s_game_system_remove_function.find_in(m_plugin.client_library_bytes()))))
        throw std::runtime_error("Failed to find IGameSystem::Remove");

    game_system_add_function(this);
}

GameSystem::~GameSystem() { m_game_system_remove_function(this); }

char const* GameSystem::Name() { return m_plugin.GetPluginDescription(); }

void GameSystem::LevelInitPostEntity() { m_plugin.level_init_post_entity({}); }
void GameSystem::LevelShutdownPreEntity() { m_plugin.level_shutdown_pre_entity({}); }

void GameSystem::Update(float) { m_plugin.update({}); }
}