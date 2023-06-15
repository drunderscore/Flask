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

    void add_listener(RecvProp&, ReceivePropertyChangedCallback);
    void add_listener(RecvProp&, DataTableChangedCallback);

    void remove_listener(RecvProp& receive_property);

private:
    struct ReceivePropertyListener
    {
        RecvVarProxyFn original_proxy;
        ReceivePropertyChangedCallback callback;
    };

    struct DataTableListener
    {
        DataTableRecvVarProxyFn original_proxy;
        DataTableChangedCallback callback;
    };

    std::map<RecvProp*, std::variant<ReceivePropertyListener, DataTableListener>> m_listeners;
};
}
