#pragma once
#include "core.h"
#include <bcrypt.h>
inline std::string sha256Bytes(const std::vector<unsigned char>& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32];
    DWORD objectSize = 0, written = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot open SHA256 provider");
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                      sizeof(objectSize), &written, 0);
    std::vector<unsigned char> object(objectSize);
    bool ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
    if (ok)
        ok = BCryptHashData(hash, const_cast<PUCHAR>(data.data()), ULONG(data.size()), 0) >= 0;
    if (ok)
        ok = BCryptFinishHash(hash, digest, 32, 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok)
        throw std::runtime_error("Cannot hash bytes");
    std::ostringstream out;
    for (auto x : digest)
        out << std::hex << std::setw(2) << std::setfill('0') << int(x);
    return out.str();
}
inline std::string sha256(const fs::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 provider unavailable");
    DWORD bytes = 0, length = 0;
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length),
                      sizeof(length), &bytes, 0);
    std::vector<unsigned char> object(length);
    unsigned char digest[32];
    if (BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("SHA256 initialization failed");
    }
    std::ifstream input(path, std::ios::binary);
    std::vector<char> buffer(65536);
    bool ok = bool(input);
    while (input) {
        input.read(buffer.data(), buffer.size());
        auto n = input.gcount();
        if (n && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(n), 0) < 0)
            ok = false;
    }
    if (input.bad())
        ok = false;
    if (BCryptFinishHash(hash, digest, 32, 0) < 0)
        ok = false;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok)
        throw std::runtime_error("Cannot hash input file");
    std::ostringstream out;
    for (auto x : digest)
        out << std::hex << std::setw(2) << std::setfill('0') << int(x);
    return out.str();
}
