#include "Flask.h"
#undef clamp
#include "DataTableHelper.h"
#include "Platform.h"
#include "Tier0Logger.h"
#include <boost/lexical_cast.hpp>
#include <client_class.h>
#include <convar.h>
#include <icliententity.h>
#include <spdlog/spdlog.h>
#include <tier1.h>

using namespace std::string_view_literals;

// These are implemented in game code, only used to call IGameSystem::Remove(this), which we do in Unload instead.
IGameSystem::~IGameSystem() = default;
IGameSystemPerFrame::~IGameSystemPerFrame() = default;

namespace Flask
{
Plugin Plugin::s_the;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(Plugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS,
                                  Plugin::s_the)

// FIXME: Signatures should be fetched from file, dependent on platform (Linux vs. Windows)
#ifdef POSIX
Signature Plugin::s_game_system_add_function("55 89 E5 56 53 83 EC 10 8B 35 ? ? ? ? A1 ? ? ? ? 8B 5D 08"sv);
Signature Plugin::s_game_system_remove_function("55 89 E5 56 53 83 EC 10 8B 15 ? ? ? ? 8B 5D 08 85 D2"sv);
// Call is from CViewRender::SetUpViews
Signature Plugin::s_call_to_hltv_camera_singleton_getter(
    "E8 ? ? ? ? 8B 8D 54 FF FF FF 89 74 24 0C 8B 95 50 FF FF FF 89 04 24 89 4C 24 08 89 54 24 04 E8 ? ? ? ? C6 85 43 FF FF FF 00"sv);
Signature Plugin::s_hltv_camera_set_primary_target_function(
    "55 89 E5 57 56 53 83 EC 3C 8B 5D 08 8B 45 0C 8B 7B 28 39 C7 0F 84 ? ? ? ? 89 43 28"sv);

std::string_view Plugin::s_client_library_name = "tf/bin/client.so";
#elif _WIN32
Signature Plugin::s_game_system_add_function("55 8B EC 51 8B 15 ? ? ? ? 8B 0D ? ? ? ? 56 8B F2 8D 42 01 3B C1"sv);
// This is quite literally the entire function... it seems MSVC does some funny things with inheritance of virtual
// destructors, so there are one or two incredibly similar, nearly identical functions...
Signature Plugin::s_game_system_remove_function(
    "55 8B EC 51 56 8B F1 8D 45 FC 50 B9 ? ? ? ? 89 75 FC C7 06 ? ? ? ? E8 ? ? ? ? 6A 00 68 ? ? ? ? 68 ? ? ? ? 6A 00 56 E8 ? ? ? ? 83 C4 14 85 C0 74 ? 8D 45 FC 89 75 FC 50 B9 ? ? ? ? E8 ? ? ? ? F6 45 08 01 74 ? 6A 0C 56 E8 ? ? ? ? 83 C4 08 8B C6 5E 8B E5 5D C2 04 00"sv);
// Call is from CViewRender::SetUpViews
Signature Plugin::s_call_to_hltv_camera_singleton_getter(
    "E8 ? ? ? ? 8B C8 E8 ? ? ? ? E9 ? ? ? ? 8B 0D ? ? ? ? 8B 01 8B 40 20"sv);
Signature Plugin::s_hltv_camera_set_primary_target_function(
    "55 8B EC 8B 45 08 83 EC 18 53 56 8B F1 8B 5E 28 3B D8 0F 84 ? ? ? ? 89 46 28"sv);

std::string_view Plugin::s_client_library_name = "tf/bin/client.dll";
#endif

ConCommand Plugin::s_flask_network_client_list("flask_network_client_list", flask_network_client_list);
ConCommand Plugin::s_flask_send_user_interaction("flask_send_user_interaction", flask_send_user_interaction);

void Plugin::insert_client_class_and_receive_table_into_cache(ClientClass& client_class)
{
    m_cached_client_classes_by_name.insert({client_class.m_pNetworkName, &client_class});

    insert_receive_table_and_base_into_cache(*client_class.m_pRecvTable);
}

void Plugin::insert_receive_table_and_base_into_cache(RecvTable& receive_table)
{
    // If we already have this table in the cache, then we must also have it's base class hierarchy, so ignore it
    // entirely.
    if (m_cached_receive_tables_by_name.contains(receive_table.m_pNetTableName))
        return;

    m_cached_receive_tables_by_name.insert({receive_table.m_pNetTableName, &receive_table});

    if (auto base_table = DataTableHelper::get_property_from_table_by_name(
            receive_table, DataTableHelper::s_base_class_table_property_name))
        insert_receive_table_and_base_into_cache(*base_table->GetDataTable());
}

template<typename T>
bool try_load_interface(T*& destination, const char* interface_version, CreateInterfaceFn create_interface_function)
{
    if (!(destination = reinterpret_cast<T*>(create_interface_function(interface_version, nullptr))))
    {
        spdlog::critical("Failed to find interface {}", interface_version);
        return false;
    }

    return true;
}

bool Plugin::Load(CreateInterfaceFn interface_factory, CreateInterfaceFn game_server_factory)
{
    // We should only ever be coming back from one thread
    spdlog::default_logger()->sinks().push_back(std::make_shared<Tier0LoggerSingleThreaded>());

    ConnectTier1Libraries(&interface_factory, 1);

    // If we are loaded with debug, then we'll allow debug messages to go through.
    // Though if this changes later on though, they'll get stopped by tier0... oh well.
    ConVarRef developer_convar("developer");
    if (developer_convar.GetBool())
        spdlog::set_level(spdlog::level::debug);

    if (!try_load_interface(m_engine_client, VENGINE_CLIENT_INTERFACE_VERSION, interface_factory))
        return false;

    if (!try_load_interface(m_debug_overlay, VDEBUG_OVERLAY_INTERFACE_VERSION, interface_factory))
        return false;

    if (!try_load_interface(m_engine_tool, VENGINETOOL_INTERFACE_VERSION, interface_factory))
        return false;

    m_engine_tool->GetClientFactory(m_client_interface_factory_function);

    if (!m_client_interface_factory_function)
    {
        spdlog::critical("Failed to get client interface factory function");
        return false;
    }

    if (!try_load_interface(m_game_event_manager, INTERFACEVERSION_GAMEEVENTSMANAGER2, interface_factory))
        return false;

    // Source SDK is outdated -- TF2 has VClient017
    if (!try_load_interface(m_base_client_dll, "VClient017", m_client_interface_factory_function))
        return false;

    if (!try_load_interface(m_client_entity_list, VCLIENTENTITYLIST_INTERFACE_VERSION,
                            m_client_interface_factory_function))
        return false;

    auto game_system_add_function =
        reinterpret_cast<IGameSystemAddFn>(s_game_system_add_function.find_in_library(s_client_library_name.data()));

    if (!game_system_add_function)
    {
        spdlog::critical("Failed to find IGameSystem::Add");
        return false;
    }

    if (!(m_game_system_remove_function = reinterpret_cast<IGameSystemRemoveFn>(
              s_game_system_remove_function.find_in_library(s_client_library_name.data()))))
    {
        spdlog::critical("Failed to find IGameSystem::Remove");
        return false;
    }

    // Because this singleton getter only returns the address of some static, it's impossible to write a signature for
    // the function. Instead, we've written a signature for a CALL to it. Once we have that address, we take the operand
    // and add the base address + 5 (because calls are relative)

    auto address_of_call_to_hltv_camera_singleton_getter =
        s_call_to_hltv_camera_singleton_getter.find_in_library(s_client_library_name.data());

    if (!address_of_call_to_hltv_camera_singleton_getter)
    {
        spdlog::critical("Failed to find call to C_HLTVCamera singleton getter");
        return false;
    }

    auto address_of_call_to_hltv_camera_singleton_getter_integer =
        reinterpret_cast<uintptr_t>(address_of_call_to_hltv_camera_singleton_getter);

    m_hltv_camera_singleton_getter = reinterpret_cast<C_HLTVCameraSingletonGetter>(
        *reinterpret_cast<uintptr_t*>(address_of_call_to_hltv_camera_singleton_getter_integer + 1) +
        address_of_call_to_hltv_camera_singleton_getter_integer + 5);

    if (!(m_hltv_camera_set_primary_target_function = reinterpret_cast<C_HLTVCameraSetPrimaryTargetFn>(
              s_hltv_camera_set_primary_target_function.find_in_library(s_client_library_name.data()))))
    {
        spdlog::critical("Failed to find C_HLTVCamera::SetPrimaryTarget");
        return false;
    }

    auto next_client_class = get_head_of_client_class_list();
    do
    {
        insert_client_class_and_receive_table_into_cache(*next_client_class);
    } while ((next_client_class = next_client_class->m_pNext));

    spdlog::debug("Cached {} client classes and {} receive tables", m_cached_client_classes_by_name.size(),
                  m_cached_receive_tables_by_name.size());

    game_event_manager().AddListener(this, "hltv_changed_target", false);

    game_system_add_function(this);

    ConVar_Register();

    accept();

    spdlog::info("Flask loaded");

    return true;
}

// FIXME: Leaving this function on Windows crashes because of ESP shenanigans
void Plugin::Unload()
{
    if (m_game_event_manager)
        m_game_event_manager->RemoveListener(this);

    if (m_game_system_remove_function)
        m_game_system_remove_function(this);

    ConVar_Unregister();

    DisconnectTier1Libraries();
}

void Plugin::Update(float)
{
    if (auto maybe_poll_error = poll(); maybe_poll_error)
        spdlog::error("Got error whilst polling Boost::Asio: {}", maybe_poll_error.to_string());
}

void Plugin::flask_network_client_list(const CCommand& args)
{
    for (auto& client : Plugin::the().clients())
        spdlog::info("{}", boost::lexical_cast<std::string>(client->remote_endpoint()));
}

void Plugin::flask_send_user_interaction(const CCommand& args)
{
    if (args.ArgC() >= 2)
        Plugin::the().did_user_interact(args.Arg(1));
}

ClientClass* Plugin::get_head_of_client_class_list()
{
    // It is not uncommon for Valve to modify an existing interface, often then increasing the interface version.
    // Unfortunately, when changing this interface, they added some virtuals in-between existing ones...
    return (*reinterpret_cast<IBaseClientDLL017GetClientClasses**>(&base_client_dll()))[8](&base_client_dll());
}

void Plugin::set_observe_target(int index)
{
    m_hltv_camera_set_primary_target_function(m_hltv_camera_singleton_getter(), index);
}

void Plugin::on_client_connected(Badge<Network::Client>, Network::Client& client)
{
    spdlog::info("Client {} connected", boost::lexical_cast<std::string>(client.remote_endpoint()));

    client.sync_game_state(m_current_game_state);
}

void Plugin::FireGameEvent(IGameEvent* event)
{
    if (event->GetName() == "hltv_changed_target"sv)
    {
        auto index = event->GetInt("obs_target");
        if (m_current_game_state.update_observe_target(index))
            did_observe_target_change(event->GetInt("obs_target"));
    }
}
}