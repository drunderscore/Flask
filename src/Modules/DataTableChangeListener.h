#pragma once

#include <dt_recv.h>
#include <functional>
#include <map>
#include <variant>

namespace Flask::Modules
{
class DataTableChangeListener
{
public:
    ~DataTableChangeListener()
    {
        for (auto& [receive_property, _] : m_listeners)
            remove_listener(*receive_property);
    }

    using ReceivePropertyChangedCallback =
        std::function<void(const CRecvProxyData*, void* output_struct, void* output_variable)>;
    using DataTableChangedCallback =
        std::function<void(const RecvProp*, void** output_variable, void* data, int object_id)>;

    enum class CallbackInvocationOrder
    {
        BeforeOriginalProxy,
        AfterOriginalProxy
    };

    void add_listener(RecvProp&, ReceivePropertyChangedCallback,
                      CallbackInvocationOrder = CallbackInvocationOrder::AfterOriginalProxy);
    void add_listener(RecvProp&, DataTableChangedCallback,
                      CallbackInvocationOrder = CallbackInvocationOrder::AfterOriginalProxy);

    void remove_listener(RecvProp& receive_property);

private:
    struct ReceivePropertyListener
    {
        RecvVarProxyFn original_proxy;
        ReceivePropertyChangedCallback callback;
        CallbackInvocationOrder callback_invocation_order;
    };

    struct DataTableListener
    {
        DataTableRecvVarProxyFn original_proxy;
        DataTableChangedCallback callback;
        CallbackInvocationOrder callback_invocation_order;
    };

    std::map<RecvProp*, std::variant<ReceivePropertyListener, DataTableListener>> m_listeners;
};
}
