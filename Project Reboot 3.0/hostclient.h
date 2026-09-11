#pragma once

#include <string>
#include <atomic>

#include "globals.h"

class HostClient
{
public:
	static constexpr const char* BackendUrl = "http://127.0.0.1:3551";

	// Register the host with the Matchmaker backend.
	// hostId defaults to the PC name; ipOverride to the detected LAN IP; portOverride to 7777 - AmountOfListens.
	static void Register(const std::string& hostId, const std::string& ipOverride, int portOverride);
	static void StartPollThread();
	static void Stop();

	static std::string GetHostId() { return HostId; }
	static std::string GetLocalIp() { return HostIp; }
	static int GetHostPort() { return HostPort; }

private:
	static DWORD WINAPI PollThreadProc(LPVOID lpParam);
	static void PollLoop();
	static bool PostJson(const std::string& path, const std::string& body, std::string& outResponse);

	static inline std::string HostId;
	static inline std::string HostIp;
	static inline int HostPort = 0;
	static inline std::atomic<bool> bPolling{ false };
	static inline HANDLE PollThreadHandle = nullptr;
};