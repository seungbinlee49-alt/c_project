#include "api.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point t)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
std::vector<unsigned char> read(const wchar_t *name)
{
    std::ifstream f(name, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Missing input");
    size_t n = size_t(f.tellg());
    f.seekg(0);
    std::vector<unsigned char> b(n);
    f.read(reinterpret_cast<char *>(b.data()), n);
    return b;
}
int wmain(int argc, wchar_t **argv)
{
    try
    {
        if (argc != 7)
            throw std::runtime_error("dmd_probe DLL type enroll.raw attempt.raw repeats output-prefix");
        int type = std::stoi(argv[2]), repeat = std::stoi(argv[5]);
        unsigned w = 0, h = 0;
        if (!dimensions(type, w, h))
            throw std::runtime_error("Invalid type");
        auto first = read(argv[3]), second = read(argv[4]);
        if (first.size() != size_t(w) * h || second.size() != first.size())
            throw std::runtime_error("Raw image size mismatch");
        auto start = Clock::now();
        HMODULE dll = LoadLibraryW(std::filesystem::absolute(argv[1]).c_str());
        if (!dll)
            throw std::runtime_error("LoadLibraryW failed: " + std::to_string(GetLastError()));
        double load = ms(start);
        auto enroll = reinterpret_cast<Extract>(GetProcAddress(dll, "KISA_Enroll"));
        auto attempt = reinterpret_cast<Extract>(GetProcAddress(dll, "KISA_Attempt"));
        auto compare = reinterpret_cast<Compare>(GetProcAddress(dll, "KISA_Compare"));
        if (!enroll || !attempt || !compare)
            throw std::runtime_error("Missing exports");
        std::vector<unsigned char> a(capacity), b(capacity);
        std::cout << std::setprecision(12);
        for (int i = 0; i < repeat; ++i)
        {
            int na = 0, nb = 0, error = 0;
            char message[256]{};
            double score = 0;
            auto begin = Clock::now();
            auto t = Clock::now();
            int re = enroll(type, first.data(), a.data(), &na, &error, message);
            double et = ms(t);
            if (!re)
                throw std::runtime_error(std::string("Enroll: ") + std::to_string(error) + " " + message);
            t = Clock::now();
            int ra = attempt(type, second.data(), b.data(), &nb, &error, message);
            double at = ms(t);
            if (!ra)
                throw std::runtime_error(std::string("Attempt: ") + std::to_string(error) + " " + message);
            t = Clock::now();
            int rc = compare(a.data(), b.data(), &score, &error, message);
            double ct = ms(t), total = ms(begin);
            if (!rc)
                throw std::runtime_error(std::string("Compare: ") + std::to_string(error) + " " + message);
            std::cout << "{\"iteration\":" << i << ",\"dll_load_ms\":" << load << ",\"enroll_ms\":" << et
                      << ",\"attempt_ms\":" << at << ",\"compare_ms\":" << ct << ",\"total_ms\":" << total
                      << ",\"under_5s\":" << (total < 5000 ? "true" : "false") << ",\"score\":" << score
                      << ",\"enroll_bytes\":" << na << ",\"attempt_bytes\":" << nb << ",\"cuda_provider_loaded\":"
                      << (GetModuleHandleW(L"onnxruntime_providers_cuda.dll") ? "true" : "false")
                      << ",\"cuda_driver_loaded\":" << (GetModuleHandleW(L"nvcuda.dll") ? "true" : "false") << "}\n"
                      << std::flush;
            if (i == 0)
            {
                std::filesystem::path prefix(argv[6]);
                std::ofstream fa(prefix.wstring() + L".enroll.bin", std::ios::binary),
                    fb(prefix.wstring() + L".attempt.bin", std::ios::binary);
                fa.write(reinterpret_cast<char *>(a.data()), na);
                fb.write(reinterpret_cast<char *>(b.data()), nb);
            }
        }
        FreeLibrary(dll);
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
