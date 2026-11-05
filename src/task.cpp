#include "image.h"
#include <cstring>
// Internal worker: mode DLL type inputA inputB templateOutput result noise seed
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 10)
        return 2;
    fs::path result = argv[7];
    Outcome r;
    HMODULE dll = nullptr;
    bool com = false;
    try {
        std::string mode = utf8(argv[1]);
        r.stage = "load_dll";
        r.status = "RUNNING";
        saveOutcome(result, r);
        dll = LoadLibraryExW(argv[2], nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll)
            throw std::runtime_error("Cannot load DLL");
        auto enroll = reinterpret_cast<Extract>(GetProcAddress(dll, "KISA_Enroll"));
        auto attempt = reinterpret_cast<Extract>(GetProcAddress(dll, "KISA_Attempt"));
        auto compare = reinterpret_cast<Compare>(GetProcAddress(dll, "KISA_Compare"));
        if (!enroll || !attempt || !compare)
            throw std::runtime_error("Missing KISA export");
        int returned = 0;
        char message[256]{};
        if (mode == "enroll" || mode == "attempt") {
            r.stage = "input";
            saveOutcome(result, r);
            hcheck(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            com = true;
            UINT w = 0, h = 0;
            if (!dimensions(std::stoi(argv[3]), w, h))
                throw std::runtime_error("Invalid iType");
            auto pixels = loadImage(argv[4], w, h);
            if (mode == "attempt")
                addNoise(pixels, w, h, utf8(argv[8]), static_cast<unsigned>(std::stoul(argv[9])));
            GuardedBuffer input(pixels.size()), buffer(capacity);
            std::memcpy(input.data, pixels.data(), pixels.size());
            input.readOnly();
            r.stage = mode;
            saveOutcome(result, r);
            auto start = std::chrono::steady_clock::now();
            returned = (mode == "enroll" ? enroll : attempt)(
                std::stoi(argv[3]), input.data, buffer.data, &r.bytes, &r.error, message);
            r.ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            if (returned == 1) {
                if (r.bytes <= 0 || r.bytes >= capacity)
                    throw std::runtime_error("Invalid output template size");
                std::ofstream out(fs::path(argv[6]), std::ios::binary);
                out.write(reinterpret_cast<char*>(buffer.data), r.bytes);
                if (!out)
                    throw std::runtime_error("Cannot save template");
            }
        } else if (mode == "compare") {
            r.stage = "input";
            saveOutcome(result, r);
            auto a = readTemplate(argv[4]), b = readTemplate(argv[5]);
            GuardedBuffer aa(capacity), bb(capacity);
            std::memcpy(aa.data, a.data(), capacity);
            std::memcpy(bb.data, b.data(), capacity);
            aa.readOnly();
            bb.readOnly();
            r.stage = "compare";
            saveOutcome(result, r);
            auto start = std::chrono::steady_clock::now();
            r.score = std::numeric_limits<double>::quiet_NaN();
            returned = compare(aa.data, bb.data, &r.score, &r.error, message);
            r.ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            if (returned == 1 && (!std::isfinite(r.score) || r.score < 0 || r.score > 1)) {
                r.score = 0;
                throw std::runtime_error("Invalid similarity score");
            }
            if (!std::isfinite(r.score))
                r.score = 0;
        } else
            throw std::runtime_error("Invalid worker mode");
        // Raw bytes are retained in hex because vendor messages may use a legacy encoding.
        std::ostringstream msg;
        for (size_t i = 0; i < 256 && message[i]; ++i)
            msg << std::hex << std::setw(2) << std::setfill('0')
                << int(static_cast<unsigned char>(message[i]));
        r.status = returned == 1 ? "OK" : "FAIL";
        r.message = returned == 1 ? "" : "DLL failure; message_hex=" + msg.str();
    } catch (const std::exception& e) {
        r.status = "FAIL";
        r.message = e.what();
    }
    try {
        saveOutcome(result, r);
    } catch (...) {
        return 3;
    }
    if (dll)
        FreeLibrary(dll);
    if (com)
        CoUninitialize();
    return 0;
}
