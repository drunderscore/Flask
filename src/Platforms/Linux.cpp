#include "../Platform.h"
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace Flask::Platform
{
std::span<uint8_t> get_bytes_for_library_name(const char* library_name)
{
    // dlopen actually returns a link_map*
    auto* dynamic_library = static_cast<link_map*>(dlopen(library_name, RTLD_NOW));
    if (!dynamic_library)
        throw PlatformException(std::string(dlerror()));

    // We have the base address of the library, but we need the size. stat-ing the l_name for the size should be fine.
    // If we pass nullptr to dlopen, it gives us ourselves. However, that doesn't give us a path that we can stat.
    // We can use "/proc/self/exe", which is just a symlink to the executable. Really nifty!

    const char* path_to_binary = library_name ? dynamic_library->l_name : "/proc/self/exe";
    struct stat dynamic_library_stat = {};
    if (stat(path_to_binary, &dynamic_library_stat) == -1)
        throw PlatformException(errno);

    dlclose(dynamic_library);

    return {reinterpret_cast<uint8_t*>(dynamic_library->l_addr), static_cast<size_t>(dynamic_library_stat.st_size)};
}

void modify_memory_protection(std::span<uint8_t> memory_region, MemoryProtection memory_protection)
{
    int platform_protection{};

    if (memory_protection.read)
        platform_protection |= PROT_READ;

    if (memory_protection.write)
        platform_protection |= PROT_WRITE;

    if (memory_protection.execute)
        platform_protection |= PROT_EXEC;

    if (mprotect(memory_region.data(), memory_region.size(), platform_protection) == -1)
        throw PlatformException(errno);
}

std::string convert_error_to_string(Error value) { return strerror(value); }
}
