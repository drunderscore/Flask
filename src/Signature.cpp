#include "Signature.h"
#include "Platform.h"
#include <cctype>
#include <charconv>

namespace Flask
{
Signature::Signature(std::string_view signature)
{
    for (auto i = 0; i < signature.length(); i++)
    {
        auto c = signature[i];

        // Spaces are insignificant
        if (isspace(c))
            continue;

        if (c == '?')
        {
            m_values.push_back({});
            continue;
        }

        uint8_t expected_byte_value;
        std::from_chars(signature.data() + i, signature.data() + i + 2, expected_byte_value, 16);
        m_values.push_back(expected_byte_value);
        i++;
    }
}

void* Signature::find_in_library(const char* library_name)
{
    auto library_bytes = Platform::get_bytes_for_library_name(library_name);

    for (auto i = 0; i < library_bytes.size() - m_values.size(); i++)
    {
        bool failed = false;

        for (auto j = 0; j < m_values.size(); j++)
        {
            auto& value = m_values[j];
            if (!value.has_value())
                continue;

            if (*(library_bytes.data() + i + j) != *value)
            {
                failed = true;
                break;
            }
        }

        if (!failed)
            return static_cast<void*>(library_bytes.data() + i);
    }

    return nullptr;
}
}