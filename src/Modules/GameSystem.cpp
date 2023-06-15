#include "GameSystem.h"
#include "../Flask.h"
#include "Server.h"

using namespace std::string_view_literals;

IGameSystem::~IGameSystem() = default;
IGameSystemPerFrame::~IGameSystemPerFrame() = default;

namespace Flask::Modules
{
#ifdef POSIX
JMP::Signature GameSystem::s_game_system_add_function("55 89 E5 56 53 83 EC 10 8B 35 ? ? ? ? A1 ? ? ? ? 8B 5D 08"sv);
JMP::Signature GameSystem::s_game_system_remove_function("55 89 E5 56 53 83 EC 10 8B 15 ? ? ? ? 8B 5D 08 85 D2"sv);
#else
JMP::Signature GameSystem::s_game_system_add_function(
    "55 8B EC 51 8B 15 ? ? ? ? 8B 0D ? ? ? ? 56 8B F2 8D 42 01 3B C1"sv);
// This is quite literally the entire function... it seems MSVC does some funny things with inheritance of virtual
// destructors, so there are one or two incredibly similar, nearly identical functions...
JMP::Signature GameSystem::s_game_system_remove_function(
    "55 8B EC 51 56 8B F1 8D 45 FC 50 B9 ? ? ? ? 89 75 FC C7 06 ? ? ? ? E8 ? ? ? ? 6A 00 68 ? ? ? ? 68 ? ? ? ? 6A 00 56 E8 ? ? ? ? 83 C4 14 85 C0 74 ? 8D 45 FC 89 75 FC 50 B9 ? ? ? ? E8 ? ? ? ? F6 45 08 01 74 ? 6A 0C 56 E8 ? ? ? ? 83 C4 08 8B C6 5E 8B E5 5D C2 04 00"sv);
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

void GameSystem::Update(float) { m_plugin.update({}); }
}