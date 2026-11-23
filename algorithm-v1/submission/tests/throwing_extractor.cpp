#include <windows.h>
extern "C" int __cdecl ThrowUnknown(void*) noexcept(false) {
    throw 17;
}
