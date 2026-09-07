#pragma once

#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>

#include "log.h"

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "dbghelp.lib")

// Lightweight anti-crash utilities.
//
// 1. A vectored exception handler that logs any unhandled access violation with
//    useful detail (exception code, faulting address, registers, the bytes of the
//    faulting instruction and a native backtrace with module+offset per frame)
//    straight into the spdlog reboot.log, so a crash gives you a real trace
//    instead of a silent process death.

namespace AntiCrash
{
	inline long CurrentExceptionCount = 0;

	inline const char* GetModuleName(uintptr_t Address)
	{
		static thread_local char TLName[MAX_PATH] = "";
		HMODULE hMods[512];
		DWORD cbNeeded = 0;
		TLName[0] = 0;
		if (!EnumProcessModulesEx(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL))
			return TLName;
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
					snprintf(TLName, MAX_PATH, "%s+0x%llx", Name[0] ? Name : "?", (unsigned long long)(Address - (uintptr_t)Info.lpBaseOfDll));
				}
				break;
			}
		}
		return TLName;
	}

	inline bool IsRangeReadable(uintptr_t Addr, size_t Len)
	{
		MEMORY_BASIC_INFORMATION Mbi{};
		if (!VirtualQuery((LPCVOID)Addr, &Mbi, sizeof(Mbi)))
			return false;
		if (Mbi.State != MEM_COMMIT)
			return false;
		if (Mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
			return false;
		return true;
	}

	inline void PrintRegisters(CONTEXT* C)
	{
#if defined(_M_X64)
		LOG_ERROR(LogBots, "AntiCrash: RIP=0x{:x} RSP=0x{:x} RBP=0x{:x}", (uintptr_t)C->Rip, (uintptr_t)C->Rsp, (uintptr_t)C->Rbp);
		LOG_ERROR(LogBots, "AntiCrash: RAX=0x{:x} RBX=0x{:x} RCX=0x{:x} RDX=0x{:x}",
			(uintptr_t)C->Rax, (uintptr_t)C->Rbx, (uintptr_t)C->Rcx, (uintptr_t)C->Rdx);
		LOG_ERROR(LogBots, "AntiCrash: RSI=0x{:x} RDI=0x{:x} R8=0x{:x} R9=0x{:x}",
			(uintptr_t)C->Rsi, (uintptr_t)C->Rdi, (uintptr_t)C->R8, (uintptr_t)C->R9);
		LOG_ERROR(LogBots, "AntiCrash: R10=0x{:x} R11=0x{:x} R12=0x{:x} R13=0x{:x} R14=0x{:x} R15=0x{:x}",
			(uintptr_t)C->R10, (uintptr_t)C->R11, (uintptr_t)C->R12, (uintptr_t)C->R13, (uintptr_t)C->R14, (uintptr_t)C->R15);
#else
		LOG_ERROR(LogBots, "AntiCrash: EIP=0x{:x} ESP=0x{:x} EBP=0x{:x}", (uintptr_t)C->Eip, (uintptr_t)C->Esp, (uintptr_t)C->Ebp);
		LOG_ERROR(LogBots, "AntiCrash: EAX=0x{:x} EBX=0x{:x} ECX=0x{:x} EDX=0x{:x}", (uintptr_t)C->Eax, (uintptr_t)C->Ebx, (uintptr_t)C->Ecx, (uintptr_t)C->Edx);
		LOG_ERROR(LogBots, "AntiCrash: ESI=0x{:x} EDI=0x{:x}", (uintptr_t)C->Esi, (uintptr_t)C->Edi);
#endif
	}

	inline void PrintInstruction(uintptr_t Rip)
	{
		// Dump the raw bytes around the faulting instruction so a disassembler can
		// decode it even without symbols for the engine.
		uintptr_t Start = Rip >= 16 ? Rip - 16 : 0;
		unsigned char Bytes[64];
		int Printed = 0;
		for (int i = 0; i < 64; ++i)
		{
			uintptr_t A = Start + i;
			if (IsRangeReadable(A, 1))
				Bytes[i] = *(volatile unsigned char*)A;
			else
				Bytes[i] = 0xCC;
		}
		std::string Hex = "AntiCrash: inst at RIP-16 [-16..+47]  ";
		char Buf[8];
		for (int i = 0; i < 64; ++i)
		{
			snprintf(Buf, sizeof(Buf), "%02x ", Bytes[i]);
			Hex += Buf;
			if (i == 31)
				Hex += "\n                                      ";
			Printed++;
		}
		LOG_ERROR(LogBots, "{}", Hex);
	}

	inline void PrintStackBacktrace()
	{
		constexpr int kMaxFrames = 64;
		void* Frames[kMaxFrames] = {};
		USHORT Count = CaptureStackBackTrace(0, kMaxFrames, Frames, nullptr);
		for (int i = 0; i < Count; ++i)
		{
			const char* Mod = GetModuleName((uintptr_t)Frames[i]);
			LOG_ERROR(LogBots, "AntiCrash: #{:02d} 0x{:x} [{}]", i, (uintptr_t)Frames[i], Mod[0] ? Mod : "?");
		}
	}

	inline LONG WINAPI VectoredHandler(PEXCEPTION_POINTERS Info)
	{
		if (!Info || !Info->ExceptionRecord)
			return EXCEPTION_CONTINUE_SEARCH;

		DWORD Code = Info->ExceptionRecord->ExceptionCode;

		// Debugger notifications (OutputDebugString etc.) are raised constantly
		// (e.g. DBG_PRINTEXCEPTION_C 0x40010006, DBG_PRINTEXCEPTION_WIDE_C
		// 0x4001000A). They are informational, not fatal: the game itself raises
		// them on every damage hit. Logging them from inside the handler (spdlog
		// locks a mutex) jams the game thread until the server dies. Fatal codes
		// always set the severity bit (0x80000000); debug ones do not.
		if (!(Code & 0x80000000))
			return EXCEPTION_CONTINUE_SEARCH;

		LOG_ERROR(LogBots, "========== AntiCrash: exception 0x{:08x} at RIP 0x{:x} ==========",
			Code, (uintptr_t)Info->ContextRecord->Rip);

		if (Code == EXCEPTION_ACCESS_VIOLATION)
		{
			ULONG_PTR Info0 = Info->ExceptionRecord->ExceptionInformation[0];
			uintptr_t FaultAddress = (uintptr_t)Info->ExceptionRecord->ExceptionInformation[1];
			LOG_ERROR(LogBots, "AntiCrash: ACCESS_VIOLATION access={} address=0x{:x}",
				(Info0 == 0) ? "read" : (Info0 == 1) ? "write" : (Info0 == 8) ? "execute" : "?",
				FaultAddress);
		}

		LOG_ERROR(LogBots, "AntiCrash: faulting instruction module: {}",
			[](uintptr_t A) { const char* M = GetModuleName(A); return M[0] ? M : "?"; }((uintptr_t)Info->ContextRecord->Rip));

		PrintRegisters(Info->ContextRecord);
		PrintInstruction((uintptr_t)Info->ContextRecord->Rip);

		LOG_ERROR(LogBots, "AntiCrash: native backtrace:");
		PrintStackBacktrace();

		LOG_ERROR(LogBots, "AntiCrash: if this repeats check Bots::Tick / bot spawn / damage hooks.");

		return EXCEPTION_CONTINUE_SEARCH;
	}

	inline bool Install()
	{
		AddVectoredExceptionHandler(1, VectoredHandler);
		LOG_INFO(LogBots, "AntiCrash: vectored exception handler installed.");
		return true;
	}
}

// Wrappers reserved for possible future SEH guarding. MSVC refuses __try in
// functions that require C++ object unwinding (C2712, even under /EHa), and these
// hooks declare objects with destructors (Cast<>, TArray, smart refs...). The real
// safety net is the vectored exception handler above, which turns a silent crash
// into a detailed error in the log.
#define CRASHGUARD_BEGIN

#define CRASHGUARD_END