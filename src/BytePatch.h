#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Flask
{
class BytePatch
{
public:
    // TODO: Introduce option to compare the original bytes before patching, to ensure we are patching what we intended.
    BytePatch(uint8_t* beginning_of_patch, std::vector<uint8_t> patch_bytes, bool immediately_patch = true)
        : m_patched_region({beginning_of_patch, m_patch_bytes.size()}), m_patch_bytes(std::move(patch_bytes))
    {
        m_original_bytes.assign(m_patched_region.begin(), m_patched_region.end());

        if (immediately_patch)
            patch();
    }

    ~BytePatch() { unpatch(); }

    void patch()
    {
        if (m_is_patched)
            return;

        std::copy(m_patch_bytes.begin(), m_patch_bytes.end(), m_patched_region.begin());

        m_is_patched = true;
    }

    void unpatch()
    {
        if (!m_is_patched)
            return;

        std::copy(m_original_bytes.begin(), m_original_bytes.end(), m_patched_region.begin());

        m_is_patched = false;
    }

private:
    std::vector<uint8_t> m_original_bytes;
    std::vector<uint8_t> m_patch_bytes;
    std::span<uint8_t> m_patched_region;
    bool m_is_patched{};
};
};