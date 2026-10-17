#include <windows.h>
#include <stdio.h>

int main() {
    __try {
        typedef void* (*fn_ci)(const char*, int*);
        HMODULE h = LoadLibraryA("lua51.dll");
        if (!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
        printf("Loaded lua51.dll at %p\n", h);
        fn_ci ci = (fn_ci)GetProcAddress(h, "CreateInterface");
        printf("CreateInterface at %p\n", ci);
        int rc = 0;
        void* iface = ci("LUASHARED003", &rc);
        printf("ILuaShared = %p, rc = %d\n", iface, rc);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        printf("CAUGHT EXCEPTION: 0x%08X\n", code);
    }
    return 0;
}
