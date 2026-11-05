#include "api.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("Image decoding failed");
}
std::vector<unsigned char> load(const wchar_t* path, UINT w, UINT h) {
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnLoad, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame));
    UINT iw = 0, ih = 0;
    check(frame->GetSize(&iw, &ih));
    if (iw != w || ih != h)
        throw std::runtime_error("Image dimensions do not match iType");
    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(&converter));
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom));
    std::vector<unsigned char> pixels(w * h);
    check(converter->CopyPixels(nullptr, w, UINT(pixels.size()), pixels.data()));
    return pixels;
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 6)
        return 2;
    std::ofstream out(std::filesystem::path(argv[5]), std::ios::trunc);
    if (!out)
        return 3;
    std::string stage = "input";
    try {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        UINT w = 0, h = 0;
        int type = std::stoi(argv[2]);
        if (!dimensions(type, w, h))
            throw std::runtime_error("Unsupported iType");
        auto a = load(argv[3], w, h), b = load(argv[4], w, h);
        stage = "load_dll";
        HMODULE dll = LoadLibraryExW(
            argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll)
            throw std::runtime_error("Cannot load DLL");
        auto enroll = reinterpret_cast<Extract>(GetProcAddress(dll, "KISA_Enroll"));
        auto attempt = reinterpret_cast<Extract>(GetProcAddress(dll, "KISA_Attempt"));
        auto compare = reinterpret_cast<Compare>(GetProcAddress(dll, "KISA_Compare"));
        if (!enroll || !attempt || !compare)
            throw std::runtime_error("Missing KISA export");
        std::vector<unsigned char> ea(capacity), eb(capacity);
        int sa = 0, sb = 0, error = 0;
        char message[256]{};
        double score = 0;
        auto start = std::chrono::steady_clock::now();
        stage = "enroll";
        if (enroll(type, a.data(), ea.data(), &sa, &error, message) != 1)
            throw std::runtime_error("DLL returned failure");
        if (sa <= 0 || sa >= capacity)
            throw std::runtime_error("Invalid enrollment size");
        stage = "attempt";
        if (attempt(type, b.data(), eb.data(), &sb, &error, message) != 1)
            throw std::runtime_error("DLL returned failure");
        if (sb <= 0 || sb >= capacity)
            throw std::runtime_error("Invalid attempt size");
        stage = "compare";
        if (compare(ea.data(), eb.data(), &score, &error, message) != 1)
            throw std::runtime_error("DLL returned failure");
        if (!std::isfinite(score) || score < 0 || score > 1)
            throw std::runtime_error("Invalid similarity score");
        double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        out.precision(17);
        out << "{\"status\":\"OK\",\"score\":" << score << ",\"algorithm_ms\":" << ms
            << ",\"enroll_bytes\":" << sa << ",\"attempt_bytes\":" << sb << "}";
        FreeLibrary(dll);
        CoUninitialize();
        return 0;
    } catch (const std::exception& e) {
        out << "{\"status\":\"FAIL\",\"stage\":\"" << stage << "\",\"message\":\"" << e.what()
            << "\"}";
        return 0;
    }
}
