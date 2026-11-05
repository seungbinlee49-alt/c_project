#include "../src/core.h"
#include <cstring>
#include <iostream>
void require(bool yes, const char* message) {
    if (!yes)
        throw std::runtime_error(message);
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 2;
    try {
        fs::path dll = fs::absolute(argv[1]);
        HMODULE library =
            LoadLibraryExW(dll.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        require(library != nullptr, "Cannot load algorithm and dependencies");
        auto enroll = reinterpret_cast<Extract>(GetProcAddress(library, "KISA_Enroll")),
             attempt = reinterpret_cast<Extract>(GetProcAddress(library, "KISA_Attempt"));
        auto compare = reinterpret_cast<Compare>(GetProcAddress(library, "KISA_Compare"));
        require(enroll && attempt && compare, "Required C exports missing");
        GuardedBuffer output(capacity);
        int size = 123, error = 0;
        char message[256]{};
        require(enroll(123, nullptr, output.data, &size, &error, message) == 0 && size == 0 &&
                    error != 0,
                "Unknown sensor must fail with initialized outputs");
        for (int type : {1, 2, 3, 5, 7, 8, 9}) {
            UINT w = 0, h = 0;
            require(dimensions(type, w, h), "Sensor profile missing");
            GuardedBuffer image(w * h);
            std::memset(image.data, 255, w * h);
            image.readOnly();
            size = 123;
            error = 0;
            require(enroll(type, image.data, output.data, &size, &error, message) == 0 &&
                        size == 0 && error != 0,
                    "Blank enrollment must fail, not fabricate features");
            size = 123;
            error = 0;
            require(attempt(type, image.data, output.data, &size, &error, message) == 0 &&
                        size == 0 && error != 0,
                    "Blank attempt must fail");
        }
        size = 123;
        error = 0;
        require(enroll(1, nullptr, output.data, &size, &error, message) == 0 && size == 0 &&
                    error != 0,
                "Null image handling");
        double score = 999;
        error = 0;
        GuardedBuffer invalid(capacity);
        invalid.readOnly();
        require(compare(invalid.data, invalid.data, &score, &error, message) == 0 && score == 0 &&
                    error != 0,
                "Corrupt templates must fail");
        FreeLibrary(library);
        std::cout << "PASS: KISA exports, 7 sensor profiles with guarded blank inputs, null input "
                     "and corrupt templates\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
