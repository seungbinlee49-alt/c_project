#include "api.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

std::vector<unsigned char> read(const std::filesystem::path& path) {
    auto count = std::filesystem::file_size(path);
    if (!count || count > capacity)
        throw std::runtime_error("Invalid input file size");
    std::vector<unsigned char> result(size_t(count), 0);
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(result.data()), count))
        throw std::runtime_error("Read failed");
    return result;
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3)
            throw std::runtime_error("probe load|enroll|attempt|compare DLL [args]");
        const std::wstring mode = argv[1];
        // Intentionally plain LoadLibraryW, with caller CWD/PATH unrelated to DLL.
        HMODULE module = LoadLibraryW(std::filesystem::absolute(argv[2]).c_str());
        if (!module) {
            std::cout << "LOAD_FAIL " << GetLastError() << '\n';
            return 2;
        }
        auto enroll = reinterpret_cast<Extract>(GetProcAddress(module, "KISA_Enroll"));
        auto attempt = reinterpret_cast<Extract>(GetProcAddress(module, "KISA_Attempt"));
        auto compare = reinterpret_cast<Compare>(GetProcAddress(module, "KISA_Compare"));
        if (!enroll || !attempt || !compare)
            throw std::runtime_error("Missing KISA API");
        if (mode == L"load") {
            std::cout << "LOAD_OK\n";
            FreeLibrary(module);
            return 0;
        }
        if (mode == L"errors") {
            std::vector<unsigned char> image(90000, 128), output(capacity);
            int size = -1, error = -1;
            char message[256]{};
            double score = -1;
            if (enroll(2, nullptr, output.data(), &size, &error, message) != 0 ||
                error != NullArgument || size != 0)
                throw std::runtime_error("Null extraction contract");
            if (attempt(99, image.data(), output.data(), &size, &error, message) != 0 ||
                error != UnsupportedType)
                throw std::runtime_error("Type error contract");
            if (compare(nullptr, nullptr, &score, &error, message) != 0 ||
                error != NullArgument || score != 0)
                throw std::runtime_error("Null comparison contract");
            if (compare(image.data(), image.data(), &score, &error, message) != 0 ||
                error != UnsupportedTemplate)
                throw std::runtime_error("Template version contract");
            std::cout << "ERROR_CONTRACT_OK\n";
            FreeLibrary(module);
            return 0;
        }
        int error = 0, bytes = 0, result = 0;
        char message[256]{};
        double score = 0;
        LARGE_INTEGER start{}, end{}, frequency{};
        QueryPerformanceFrequency(&frequency);
        if (mode == L"compare") {
            if (argc != 5)
                throw std::runtime_error("probe compare DLL enroll.bin attempt.bin");
            auto a = read(argv[3]), b = read(argv[4]);
            QueryPerformanceCounter(&start);
            result = compare(a.data(), b.data(), &score, &error, message);
            QueryPerformanceCounter(&end);
        } else {
            if (argc != 6)
                throw std::runtime_error("probe enroll|attempt DLL TYPE input.raw output.bin");
            int type = std::stoi(argv[3]);
            auto image = read(argv[4]);
            unsigned w = 0, h = 0;
            if (dimensions(type, w, h) && image.size() != size_t(w) * h)
                throw std::runtime_error("RAW size mismatch");
            std::vector<unsigned char> output(capacity + 64, 0xCD);
            QueryPerformanceCounter(&start);
            result = (mode == L"enroll" ? enroll : attempt)(type, image.data(), output.data(),
                                                            &bytes, &error, message);
            QueryPerformanceCounter(&end);
            for (size_t i = capacity; i < output.size(); ++i)
                if (output[i] != 0xCD)
                    throw std::runtime_error("Output overflow");
            if (result == 1) {
                if (bytes <= 0 || bytes >= capacity)
                    throw std::runtime_error("Invalid output size");
                std::ofstream file(argv[5], std::ios::binary);
                file.write(reinterpret_cast<char*>(output.data()), bytes);
            }
        }
        std::cout << std::setprecision(17) << result << '\t' << error << '\t' << bytes << '\t'
                  << score << '\t' << (end.QuadPart - start.QuadPart) * 1000. / frequency.QuadPart
                  << '\t' << message << '\n';
        FreeLibrary(module);
        return result == 1 && error == 0 ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
