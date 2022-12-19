#pragma once

#include <tier1/convar.h>

namespace Flask
{
template<typename T>
class ManagedConCommandBase
{
public:
    template<typename... Args>
    explicit ManagedConCommandBase(Args&&... args) : m_value(std::forward<Args>(args)...)
    {
    }

    T* operator->() { return &m_value; }
    const T* operator->() const { return &m_value; }

    ~ManagedConCommandBase() { g_pCVar->UnregisterConCommand(&m_value); }

private:
    T m_value;
};

using ManagedConCommand = ManagedConCommandBase<ConCommand>;
using ManagedConVar = ManagedConCommandBase<ConVar>;
}