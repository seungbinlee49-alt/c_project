#include "api.h"
extern "C" __declspec(dllexport) int KISA_Enroll(int, unsigned char* image, unsigned char* output,
                                                 int* size, int* error, char* message) {
#if defined(FIXTURE_overrun)
    output[capacity] = 1;
#elif defined(FIXTURE_inputwrite)
    image[0] = 1;
#endif
    (void)image;
    (void)output;
#if defined(FIXTURE_hang)
    Sleep(INFINITE);
#elif defined(FIXTURE_crash)
    TerminateProcess(GetCurrentProcess(), 77);
#endif
    *size = 0;
    *error = 42;
    strcpy_s(message, 256, "Intentional test failure");
#if defined(FIXTURE_badsize)
    *size = 1000000;
    return 1;
#elif defined(FIXTURE_badscore) || defined(FIXTURE_comparehang) || defined(FIXTURE_comparecrash)
    *size = 4;
    *error = 0;
    message[0] = 0;
    return 1;
#else
    return 0;
#endif
}
extern "C" __declspec(dllexport) int KISA_Attempt(int t, unsigned char* i, unsigned char* o, int* s,
                                                  int* e, char* m) {
    return KISA_Enroll(t, i, o, s, e, m);
}
extern "C" __declspec(dllexport) int KISA_Compare(unsigned char*, unsigned char*, double* score,
                                                  int* error, char* message) {
#if defined(FIXTURE_comparehang)
    Sleep(INFINITE);
#elif defined(FIXTURE_comparecrash)
    TerminateProcess(GetCurrentProcess(), 78);
#endif
    *score = 2.0;
    *error = 0;
    message[0] = 0;
    return 1;
}
