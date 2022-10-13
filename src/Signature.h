#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Flask
{
class Signature
{
public:
    explicit Signature(std::string_view signature);

    void* find_in_library(const char* library_name);

    const std::vector<std::optional<uint8_t>>& values() const { return m_values; }

private:
    std::vector<std::optional<uint8_t>> m_values;
};
}