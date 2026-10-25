#include "Nexus.h"
#include <windows.h>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const HMODULE library = LoadLibraryA(argv[1]);
    if (!library) { std::cerr << "LoadLibrary failed: " << GetLastError() << '\n'; return 2; }
    auto get = reinterpret_cast<AddonDefinition_t*(*)()>(GetProcAddress(library, "GetAddonDef"));
    if (!get) { FreeLibrary(library); return 3; }
    const auto* definition = get();
    const bool valid = definition && definition->APIVersion == NEXUS_API_VERSION &&
        definition->Signature == static_cast<uint32_t>(-1847302962) &&
        std::strcmp(definition->Name, "Waypoint Probe") == 0 &&
        definition->Load && definition->Unload && definition->Flags == AF_DisableHotloading &&
        definition->Provider == UP_None && definition->Version.Major == 0 && definition->Version.Minor == 6;
    FreeLibrary(library);
    if (!valid) return 4;
    std::cout << "Nexus definition and DLL export verified\n";
}
