#include "image.h"
#include <cstring>
#include <iostream>
extern "C" __declspec(dllimport) int FP_ExtractImage(int, int, int, unsigned char*, unsigned char*,
                                                     int*, int*, char*);
extern "C" __declspec(dllimport) int KISA_Compare(unsigned char*, unsigned char*, double*, int*,
                                                  char*);
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 4) {
        std::cerr << "Expected 3 path arguments\n";
        return 2;
    }
    fs::path first = fs::absolute(argv[1]), second = fs::absolute(argv[2]),
             log = fs::absolute(argv[3]);
    // The external protocol explicitly requires appending one output row.
    if ((fs::exists(log) && fs::equivalent(first, log)) ||
        (fs::exists(log) && fs::exists(second) && fs::equivalent(second, log))) {
        std::cerr << "Log must not overwrite input/template\n";
        return 2;
    }
    std::ofstream out(log, std::ios::app);
    if (!out)
        return 2;
    bool ok = false;
#ifndef FVC_ENROLL
    double score = 0;
#endif
    try {
#ifdef FVC_ENROLL
        if (fs::exists(second) && fs::equivalent(first, second))
            throw std::runtime_error("Template must not overwrite source image");
        hcheck(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        ComPtr<IWICImagingFactory> factory;
        hcheck(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)));
        ComPtr<IWICBitmapDecoder> decoder;
        hcheck(factory->CreateDecoderFromFilename(first.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, &decoder));
        ComPtr<IWICBitmapFrameDecode> frame;
        hcheck(decoder->GetFrame(0, &frame));
        UINT w = 0, h = 0;
        hcheck(frame->GetSize(&w, &h));
        if (w > 2000 || h > 2000)
            throw std::runtime_error("Image too large");
        auto pixels = loadImage(first, w, h);
        GuardedBuffer input(pixels.size()), feature(capacity);
        std::memcpy(input.data, pixels.data(), pixels.size());
        input.readOnly();
        int size = 0, error = 0;
        char message[256]{};
        if (FP_ExtractImage(w, h, 500, input.data, feature.data, &size, &error, message) != 1 ||
            size <= 0 || size >= capacity)
            throw std::runtime_error("Extraction failed");
        std::ofstream file(second, std::ios::binary);
        file.write(reinterpret_cast<char*>(feature.data), size);
        if (!file)
            throw std::runtime_error("Cannot save template");
        ok = true;
#else
        auto a = readTemplate(first), b = readTemplate(second);
        GuardedBuffer aa(capacity), bb(capacity);
        std::memcpy(aa.data, a.data(), capacity);
        std::memcpy(bb.data, b.data(), capacity);
        aa.readOnly();
        bb.readOnly();
        int error = 0;
        char message[256]{};
        if (KISA_Compare(aa.data, bb.data, &score, &error, message) != 1 || !std::isfinite(score) ||
            score < 0 || score > 1)
            throw std::runtime_error("Comparison failed");
        ok = true;
#endif
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    out << utf8(first) << ' ' << utf8(second) << ' ' << (ok ? "OK" : "FAIL");
#ifndef FVC_ENROLL
    out << ' ' << std::setprecision(17) << (ok ? score : 0);
#endif
    out << '\n';
    out.flush();
    return out ? 0 : 2;
}
