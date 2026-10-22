# Console support sources

These Source SDK files were copied from the `opglua` revision recorded in
`IMPORT.json`. Copyright notices remain in each file. Only the console support
sources and their header dependencies are included.

The local console interfaces follow the host ABI. The game engine checkout is
not read by the build. Utility sources are compiled locally, and the Windows
build generates import libraries from the configured host's tier0 and vstdlib
DLL exports. Set `OPGLUA_HOST_BIN` when those DLLs are in another location.

The GarrysMod Lua headers are in the adjacent `gmodcommon/include` directory.
