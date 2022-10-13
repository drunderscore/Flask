#include "../Platform.h"
#include <cassert>
#include <windows.h>

// Explicitly include windows.h BEFORE psapi.h
#include <psapi.h>

namespace Flask::Platform
{
std::span<uint8_t> get_bytes_for_library_name(const char* library_name)
{
    auto module = GetModuleHandleA(library_name);
    if (!module)
        throw PlatformException(GetLastError());

    MODULEINFO module_info{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &module_info, sizeof(module_info)))
        throw PlatformException(GetLastError());

    return {reinterpret_cast<uint8_t*>(module_info.lpBaseOfDll), static_cast<size_t>(module_info.SizeOfImage)};
}

void modify_memory_protection(std::span<uint8_t> memory_region, MemoryProtection memory_protection)
{
    DWORD platform_protection{};

    // FIXME: I don't believe that Windows virtual memory protections can be OR'd together like on Linux?

    if (memory_protection.read)
    {
        if (memory_protection.write)
        {
            if (memory_protection.execute)
                platform_protection = PAGE_EXECUTE_READWRITE;
            else
                platform_protection = PAGE_READWRITE;
        }
        else if (memory_protection.execute)
            platform_protection = PAGE_EXECUTE_READ;
        else
            platform_protection = PAGE_READONLY;
    }

    // "If this parameter is NULL or does not point to a valid variable, the function fails."
    DWORD original_platform_protection{};

    if (!VirtualProtect(memory_region.data(), memory_region.size(), platform_protection, &original_platform_protection))
        throw PlatformException(GetLastError());
}

// See: https://stackoverflow.com/questions/1387064
std::string convert_error_to_string(Error value)
{
    LPSTR message{};

    auto message_length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, value,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&message), 0, NULL);

    assert(message_length != 0);
    std::string error_message(message, message_length);
    LocalFree(message);

    return error_message;
}
}