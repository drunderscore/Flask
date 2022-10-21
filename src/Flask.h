#pragma once

// Explicitly put this at the top -- needed for igamesystem.h
#include <platform.h>

#include "GameState.h"
#include "Network/WebsocketServer.h"
#include "Signature.h"
#include <../game/shared/igamesystem.h>
#include <cdll_int.h>
#include <client_class.h>
#include <icliententitylist.h>
#include <igameevents.h>
#include <iserverplugin.h>
#include <ivdebugoverlay.h>
#include <tier1/convar.h>
#include <toolframework/ienginetool.h>

class C_HLTVCamera;

namespace Flask
{
class Plugin : public IServerPluginCallbacks,
               public IGameSystemPerFrame,
               public Network::WebsocketServer,
               public IGameEventListener2
{
public:
    ~Plugin() override = default;

    bool Load(CreateInterfaceFn, CreateInterfaceFn) override;
    void Unload() override;
    void Pause() override {}
    void UnPause() override {}
    const char* GetPluginDescription() override { return "Flask"; }
    void LevelInit(char const*) override {}
    void ServerActivate(edict_t*, int, int) override {}
    void GameFrame(bool) override {}
    void LevelShutdown() override {}
    void ClientActive(edict_t*) override {}
    void ClientDisconnect(edict_t*) override {}
    void ClientPutInServer(edict_t*, char const*) override {}
    void SetCommandClient(int) override {}
    void ClientSettingsChanged(edict_t*) override {}
    PLUGIN_RESULT ClientConnect(bool*, edict_t*, const char*, const char*, char*, int) override
    {
        return PLUGIN_CONTINUE;
    }
    PLUGIN_RESULT ClientCommand(edict_t*, const CCommand&) override { return PLUGIN_CONTINUE; }
    PLUGIN_RESULT NetworkIDValidated(const char*, const char*) override { return PLUGIN_CONTINUE; }
    void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t*, EQueryCvarValueStatus, const char*, const char*) override
    {
    }

    char const* Name() override { return GetPluginDescription(); }
    bool Init() override { return true; }

    void PostInit() override {}
    void Shutdown() override {}
    void LevelInitPreEntity() override {}
    void LevelInitPostEntity() override {}
    void LevelShutdownPreEntity() override {}
    void LevelShutdownPostEntity() override {}
    void OnSave() override {}
    void OnRestore() override {}
    void SafeRemoveIfDesired() override {}
    bool IsPerFrame() override { return true; }
    void PreRender() override {}
    void Update(float frametime) override;
    void PostRender() override {}

    IVEngineClient& engine_client() { return *m_engine_client; }
    IVDebugOverlay& debug_overlay() { return *m_debug_overlay; }
    IEngineTool& engine_tool() { return *m_engine_tool; }
    IBaseClientDLL& base_client_dll() { return *m_base_client_dll; }
    IGameEventManager2& game_event_manager() { return *m_game_event_manager; }
    IClientEntityList& client_entity_list() { return *m_client_entity_list; }

    ClientClass* get_head_of_client_class_list();

    ClientClass* find_client_class_by_name(std::string_view name)
    {
        if (auto it = m_cached_client_classes_by_name.find(name); it != m_cached_client_classes_by_name.end())
            return it->second;

        return nullptr;
    }

    RecvTable* find_receive_table_by_name(std::string_view name)
    {
        if (auto it = m_cached_receive_tables_by_name.find(name); it != m_cached_receive_tables_by_name.end())
            return it->second;

        return nullptr;
    }

    static Plugin s_the;
    static Plugin& the() { return s_the; }

    void set_observe_target(int index) override;

    void on_client_connected(Badge<Network::Client>, Network::Client&) override;

    void FireGameEvent(IGameEvent* event) override;

private:
    static Signature s_game_system_add_function;
    static Signature s_game_system_remove_function;
    static Signature s_call_to_hltv_camera_singleton_getter;
    static Signature s_hltv_camera_set_primary_target_function;
    static std::string_view s_client_library_name;

    static ConCommand s_flask_network_client_list;

    static void flask_network_client_list(const CCommand&);

    typedef void (*IGameSystemAddFn)(IGameSystem*);
    typedef void (*IGameSystemRemoveFn)(IGameSystem*);
    typedef C_HLTVCamera* (*C_HLTVCameraSingletonGetter)();

    // NOTE: Not only does MSVC not support the attribute calling-convention notation, but MSVC also does not implement
    // calls to member functions to be similar to cdecl -- MSVC puts the this pointer into ECX.
#ifdef POSIX
    typedef __attribute__((cdecl)) void (*C_HLTVCameraSetPrimaryTargetFn)(C_HLTVCamera*, int);
    typedef __attribute__((cdecl)) ClientClass* (*IBaseClientDLL017GetClientClasses)(IBaseClientDLL*);
#elif _WIN32
    // FIXME: clang-format formats this weirdly, but I'm not sure if I'm even putting it in a favorable order... but I
    //        also don't think I should expect clang-format to be able to format MSVC-specific declarations... perhaps
    //        we should clang-format off this entire part.
    typedef void(__thiscall* C_HLTVCameraSetPrimaryTargetFn)(C_HLTVCamera*, int);
    typedef ClientClass*(__thiscall* IBaseClientDLL017GetClientClasses)(IBaseClientDLL*);
#endif

    IVEngineClient* m_engine_client{};
    IVDebugOverlay* m_debug_overlay{};
    IEngineTool* m_engine_tool{};
    CreateInterfaceFn m_client_interface_factory_function{};
    IBaseClientDLL* m_base_client_dll{};
    IGameEventManager2* m_game_event_manager{};
    IClientEntityList* m_client_entity_list{};
    std::map<std::string, ClientClass*, std::less<>> m_cached_client_classes_by_name;
    std::map<std::string, RecvTable*, std::less<>> m_cached_receive_tables_by_name;
    IGameSystemRemoveFn m_game_system_remove_function{};
    C_HLTVCameraSingletonGetter m_hltv_camera_singleton_getter{};
    C_HLTVCameraSetPrimaryTargetFn m_hltv_camera_set_primary_target_function{};
    GameState m_current_game_state;

    void insert_client_class_and_receive_table_into_cache(ClientClass&);
    void insert_receive_table_and_base_into_cache(RecvTable&);
};
}
