#pragma once
#include <array>
#include <cstdint>
namespace identity
{
inline constexpr uint32_t magic = 0x324d4446u; // FDM2: frozen CPU INT8 release
inline constexpr uint32_t modelTag = 3670014796u;
inline constexpr std::array<unsigned char, 32> sha256 = {76,  251, 191, 218, 130, 56,  76,  115, 124, 166, 78,
                                                         235, 68,  12,  111, 26,  190, 12,  222, 255, 226, 41,
                                                         163, 225, 67,  176, 188, 253, 233, 71,  133, 251};
} // namespace identity
