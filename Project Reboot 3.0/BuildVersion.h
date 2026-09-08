#pragma once

// Version de build.
//
// La CI genera BuildVersion.generated.h (con REBOOT_BUILD_VERSION/REBOOT_BUILD_HASH)
// antes de compilar y lo coloca junto a este fichero. Si no existe (build local),
// se usan valores por defecto.
#ifdef __has_include
  #if __has_include("BuildVersion.generated.h")
    #include "BuildVersion.generated.h"
  #endif
#endif

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
