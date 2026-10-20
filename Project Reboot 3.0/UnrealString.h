#pragma once

#include <locale>

#include "Array.h"
#include "log.h"

// #define EXPERIMENTAL_FSTRING

class FString
{
public:
	TArray<TCHAR> Data;

public:
	std::string ToString() const
    {
        // Respect the FString length; never scan beyond its declared buffer.
        if (!Data.Data || Data.ArrayNum <= 0 ||
            Data.ArrayMax < Data.ArrayNum || Data.ArrayNum > 1048576)
            return {};
        int Length = 0;
        while (Length < Data.ArrayNum && Data.Data[Length] != L'\0')
            ++Length;
        if (Length == 0) return {};
        const int Required = WideCharToMultiByte(
            CP_UTF8, 0, Data.Data, Length, nullptr, 0, nullptr, nullptr);
        if (Required <= 0) return {};
        std::string Result(Required, '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, Data.Data, Length,
            Result.data(), Required, nullptr, nullptr) != Required)
            return {};
        return Result;
    }

	void Free()
	{
		Data.Free();
	}

	bool IsValid()
	{
		return Data.Data;
	}

	void Set(const wchar_t* NewStr)
	{
		if (!NewStr/* || std::wcslen(NewStr) == 0 */) 
			return;

		constexpr size_t Inc = 1;

#ifndef EXPERIMENTAL_FSTRING
		Data.ArrayMax = Data.ArrayNum = *NewStr ? (int)std::wcslen(NewStr) + Inc : 0;

		if (Data.ArrayNum)
			Data.Data = const_cast<wchar_t*>(NewStr);
#else
		Data.ArrayNum = (int)std::wcslen(NewStr) + Inc;
		Data.ArrayMax = Data.ArrayNum;

		if (Data.ArrayNum > 0)
		{
			int amountToAlloc = (Data.ArrayNum * sizeof(TCHAR));

			if (Addresses::Free && Addresses::Realloc)
			{
				Data.Data = (TCHAR*)FMemory::Realloc(0, amountToAlloc, 0);
				memcpy_s(Data.Data, amountToAlloc, NewStr, amountToAlloc);
			}
			else
			{
				Data.Data = (TCHAR*)NewStr;
			}
		}
#endif
	}

	FString() {}

#ifdef EXPERIMENTAL_FSTRING
	FString& operator=(const wchar_t* Other)
	{
		this->Set(Other);
		return *this;
	}

	FString& operator=(const FString& Other)
	{
		this->Set(Other.Data.Data);
		return *this;
	}

	FString(const FString& Other)
	{
		this->Set(Other.Data.Data);
	}
#endif

	FString(const wchar_t* str)
	{
		Set(str);
	}

	~FString()
	{
#ifdef EXPERIMENTAL_FSTRING
		if (Data.Data)
		{
			// LOG_INFO(LogDev, "Deconstructing FString!");
			// free(Data.Data);

			if (Addresses::Realloc && Addresses::Free)
			{
				static void (*freeOriginal)(void*) = decltype(freeOriginal)(Addresses::Free);
				freeOriginal(Data.Data);
			}
			else
			{
				// VirtualFree(Data.Data, 0, MEM_RELEASE);
			}
		}
#endif

		// Free();

		Data.Data = nullptr;
		Data.ArrayNum = 0;
		Data.ArrayMax = 0;
	}
};