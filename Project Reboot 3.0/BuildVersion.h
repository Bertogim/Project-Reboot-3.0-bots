#pragma once

// Version de build. La CI puede sobrescribir estos valores con /D defines
// (REBOOT_BUILD_VERSION, REBOOT_BUILD_HASH) para que el DLL loguee una
// version identificable y el artefacto incluya un archivo version.txt.
#ifndef REBOOT_BUILD_VERSION
#define REBOOT_BUILD_VERSION "dev"
#endif

#ifndef REBOOT_BUILD_HASH
#define REBOOT_BUILD_HASH "unknown"
#endif

namespace BuildVersion
{
	inline const char* GetVersion() { return REBOOT_BUILD_VERSION; }
	inline const char* GetHash()    { return REBOOT_BUILD_HASH; }
}
