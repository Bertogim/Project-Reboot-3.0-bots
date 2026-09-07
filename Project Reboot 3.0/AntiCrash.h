#pragma once

#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <cstdint>

#include "log.h"

#pragma comment(lib, "Psapi.lib")

// Lightweight anti-crash utilities.
//
// 1. A vectored exception handler that logs any unhandled access violation with
//    useful detail (exception code, faulting address, module + offset) straight
//    into the spdlog reboot.log, so a crash gives you a real trace instead of a
//    silent process death.
//
// 2. A CRASHGUARD macro to wrap the body of our risky native hooks in __try/__except
//    so an access violation is captured, logged and swallowed instead of killing the
//    whole dedicated server.

namespace AntiCrash
{
	inline long CurrentExceptionCount = 0;

	inline void PrintModule(uintptr_t Address)
	{
		HMODULE hMods[256];
		DWORD cbNeeded = 0;
		if (!EnumProcessModulesEx(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL))
			return;

		int Count = (int)(cbNeeded / sizeof(HMODULE));
		for (int i = 0; i < Count; ++i)
		{
			MODULEINFO Info;
			CHAR Name[MAX_PATH];
			if (GetModuleInformation(GetCurrentProcess(), hMods[i], &Info, sizeof(Info)) &&
				Address >= (uintptr_t)Info.lpBaseOfDll &&
				Address < (uintptr_t)Info.lpBaseOfDll + Info.SizeOfImage)
			{
				if (GetModuleBaseNameA(GetCurrentProcess(), hMods[i], Name, sizeof(Name)))
				{
					LOG_ERROR(LogBots, "AntiCrash: fault in module '{}' at 0x{:x} (offset from base 0x{:x})",
						(Name[0] ? Name : "?"), Address, Address - (uintptr_t)Info.lpBaseOfDll);
				}
				return;
			}
		}
		LOG_ERROR(LogBots, "AntiCrash: fault at unknown module, addr 0x{:x}", Address);
	}

	inline LONG WINAPI VectoredHandler(PEXCEPTION_POINTERS Info)
	{
		if (!Info || !Info->ExceptionRecord)
			return EXCEPTION_CONTINUE_SEARCH;

		DWORD Code = Info->ExceptionRecord->ExceptionCode;

		// Only log the memory-access failures that commonly kill the server from our hooks.
		if (Code == EXCEPTION_ACCESS_VIOLATION)
		{
			uintptr_t FaultAddress = (uintptr_t)Info->ExceptionRecord->ExceptionInformation[1];
			LOG_ERROR(LogBots, "========== AntiCrash: ACCESS_VIOLATION at 0x{:x} (read), RIP 0x{:x} ==========",
				FaultAddress, (uintptr_t)Info->ContextRecord->Rip);
			PrintModule((uintptr_t)Info->ContextRecord->Rip);
			LOG_ERROR(LogBots, "AntiCrash: if this repeats check Bots::Tick / bot spawn / damage hooks.");
		}
		else
		{
			LOG_ERROR(LogBots, "AntiCrash: unhandled exception code 0x{:x} at RIP 0x{:x}", Code, (uintptr_t)Info->ContextRecord->Rip);
			PrintModule((uintptr_t)Info->ContextRecord->Rip);
		}

		return EXCEPTION_CONTINUE_SEARCH;
	}

	inline bool Install()
	{
		AddVectoredExceptionHandler(1, VectoredHandler);
		LOG_INFO(LogBots, "AntiCrash: vectored exception handler installed.");
		return true;
	}
}

// Wrap a native hook body. On AV it logs and continues instead of crashing.
// NOTE: do NOT declare C++ objects with non-trivial destructors inside this scope
// (MSVC forbids __try with unwind). Keep the guarded block C-style.
#define CRASHGUARD_BEGIN \
	__try {

#define CRASHGUARD_END \
	} __except (AntiCrash::CurrentExceptionCount++, EXCEPTION_EXECUTE_HANDLER) { \
		LOG_ERROR(LogBots, "AntiCrash: caught exception in hook (code 0x{:x})", GetExceptionCode()); \
	}
