#include "DataTableChangeListener.h"
#include "../Flask.h"

namespace Flask::Modules
{
void DataTableChangeListener::add_listener(RecvProp& receive_property, ReceivePropertyChangedCallback callback,
                                           CallbackInvocationOrder callback_invocation_order)
{
    m_listeners.insert(
        {&receive_property, ReceivePropertyListener{.original_proxy = receive_property.GetProxyFn(),
                                                    .callback = std::move(callback),
                                                    .callback_invocation_order = callback_invocation_order}});

    receive_property.SetProxyFn([](auto data, auto output_struct, auto output_variable) {
        auto& [original_proxy, callback, callback_invocation_order] = std::get<ReceivePropertyListener>(
            Plugin::the().data_table_change_listener().m_listeners[const_cast<RecvProp*>(data->m_pRecvProp)]);

        if (callback_invocation_order == CallbackInvocationOrder::BeforeOriginalProxy)
            callback(data, output_struct, output_variable);

        original_proxy(data, output_struct, output_variable);

        if (callback_invocation_order == CallbackInvocationOrder::AfterOriginalProxy)
            callback(data, output_struct, output_variable);
    });
}

void DataTableChangeListener::add_listener(RecvProp& receive_property, DataTableChangedCallback callback,
                                           CallbackInvocationOrder callback_invocation_order)
{
    m_listeners.insert({&receive_property, DataTableListener{.original_proxy = receive_property.GetDataTableProxyFn(),
                                                             .callback = std::move(callback),
                                                             .callback_invocation_order = callback_invocation_order}});

    receive_property.SetDataTableProxyFn([](auto receive_property, auto output_variable, auto data, auto object_id) {
        auto& [original_proxy, callback, callback_invocation_order] = std::get<DataTableListener>(
            Plugin::the().data_table_change_listener().m_listeners[const_cast<RecvProp*>(receive_property)]);

        if (callback_invocation_order == CallbackInvocationOrder::BeforeOriginalProxy)
            callback(receive_property, output_variable, data, object_id);

        original_proxy(receive_property, output_variable, data, object_id);

        if (callback_invocation_order == CallbackInvocationOrder::AfterOriginalProxy)
            callback(receive_property, output_variable, data, object_id);
    });
}

template<typename... Ts>
struct Overload : Ts...
{
    using Ts::operator()...;
};

void DataTableChangeListener::remove_listener(RecvProp& receive_property)
{
    if (auto it = m_listeners.find(&receive_property); it != m_listeners.end())
    {
        std::visit(Overload{[&receive_property](ReceivePropertyListener& receive_property_listener) {
                                receive_property.SetProxyFn(receive_property_listener.original_proxy);
                            },
                            [&receive_property](DataTableListener& data_table_listener) {
                                receive_property.SetDataTableProxyFn(data_table_listener.original_proxy);
                            }},
                   it->second);

        m_listeners.erase(it);
    }
}
}