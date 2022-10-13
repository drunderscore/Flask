#pragma once

#include <cinttypes>
#include <fmt/format.h>
#include <span>
#include <stdexcept>

namespace Flask::Platform
{
// Both Windows and Linux use a 32-bit signed value for errors
using Error = int32_t;

struct MemoryProtection
{
    bool read{};
    bool write{};
    bool execute{};
};

std::span<uint8_t> get_bytes_for_library_name(const char* library_name);
void modify_memory_protection(std::span<uint8_t> memory_region, MemoryProtection);
std::string convert_error_to_string(Error);

class PlatformException : public std::runtime_error
{
public:
    explicit PlatformException(Error error) : std::runtime_error(convert_error_to_string(error)), m_error(error) {}

    explicit PlatformException(const std::string& message) : std::runtime_error(message) {}

    Error error() const { return m_error; }

private:
    Error m_error{};
};
}