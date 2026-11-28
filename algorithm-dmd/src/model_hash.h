#pragma once
#include "errors.h"
#include <array>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
namespace modelhash
{
inline std::array<unsigned char, 32> sha256(const std::filesystem::path &path)
{
    struct Handles
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<unsigned char> object;
        ~Handles()
        {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } handles;
    auto check = [](NTSTATUS code) {
        if (code < 0)
            throw AlgorithmError(112, "Cannot verify DMD model SHA256");
    };
    check(BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD bytes = 0, received = 0;
    check(BCryptGetProperty(handles.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&bytes), sizeof(bytes),
                            &received, 0));
    handles.object.resize(bytes);
    std::vector<unsigned char> buffer(1024 * 1024);
    check(BCryptCreateHash(handles.algorithm, &handles.hash, handles.object.data(), bytes, nullptr, 0, 0));
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw AlgorithmError(110, "Cannot open DMD model");
    while (input)
    {
        input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        auto count = input.gcount();
        if (count > 0)
            check(BCryptHashData(handles.hash, buffer.data(), ULONG(count), 0));
    }
    if (!input.eof())
        throw AlgorithmError(110, "Cannot read DMD model");
    std::array<unsigned char, 32> digest{};
    check(BCryptFinishHash(handles.hash, digest.data(), ULONG(digest.size()), 0));
    return digest;
}
} // namespace modelhash
