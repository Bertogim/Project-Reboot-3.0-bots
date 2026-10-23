#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#define CURL_STATICLIB
#include <curl/curl.h>
#include <json.hpp>

#include "hostclient.h"
#include "inc.h"
#include "log.h"
#include <cstring>

namespace
{
    // The launcher permits arbitrary host names and adds instance suffixes.
    // Read the actual login instead of guessing a "Host" name prefix.
    std::string HostLoginAccountId()
    {
        const std::wstring Command = GetCommandLineW();
        std::wstring Login;
        for (size_t i = 0; i < Command.size();)
        {
            while (i < Command.size() && (Command[i] == L' ' || Command[i] == L'\t')) ++i;
            std::wstring Token;
            bool Quoted = false;
            while (i < Command.size())
            {
                const wchar_t Ch = Command[i];
                if (Ch == L'"') { Quoted = !Quoted; ++i; continue; }
                if (!Quoted && (Ch == L' ' || Ch == L'\t')) break;
                Token.push_back(Ch);
                ++i;
            }
            const std::wstring Key = L"-AUTH_LOGIN=";
            if (Token.size() >= Key.size() &&
                _wcsnicmp(Token.c_str(), Key.c_str(), Key.size()) == 0)
                Login = Token.substr(Key.size());
        }
        const auto At = Login.find(L'@');
        if (At != std::wstring::npos) Login.resize(At);
        if (Login.empty()) return {};
        const int Length = WideCharToMultiByte(CP_UTF8, 0, Login.data(),
            (int)Login.size(), nullptr, 0, nullptr, nullptr);
        if (Length <= 0) return {};
        std::string Result(Length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, Login.data(), (int)Login.size(),
            Result.data(), Length, nullptr, nullptr);
        return Result;
    }

	struct PostResponse
	{
		std::string Body;
		long StatusCode = 0;
	};

	size_t WriteCallback(void* Contents, size_t Size, size_t Nmemb, void* UserData)
	{
		auto* Response = (PostResponse*)UserData;
		Response->Body.append((char*)Contents, Size * Nmemb);
		return Size * Nmemb;
	}

	// 127.0.0.0/8 loopback, 169.254.0.0/16 link-local, multicast (>= 224) y 0.0.0.0
	// no sirven para anunciar el host a los clientes.
	bool IsNonRoutable(const char* Ip)
	{
		in_addr Addr{};

		if (inet_pton(AF_INET, Ip, &Addr) != 1)
			return true;

		unsigned char First = Addr.S_un.S_un_b.s_b1;
		unsigned char Second = Addr.S_un.S_un_b.s_b2;

		if (First == 0 || First == 127)
			return true;

		if (First == 169 && Second == 254)
			return true;

		if (First >= 224)
			return true;

		return false;
	}

	std::string DetectLocalIpAddress()
	{
		// 1) Por ruta por defecto: connect() UDP a un destino remoto hace que el SO
		//    elija la interfaz real usada para salir (la LAN/Internet), no loopback.
		SOCKET Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

		if (Sock != INVALID_SOCKET)
		{
			sockaddr_in Dest{};
			Dest.sin_family = AF_INET;
			Dest.sin_port = htons(53);
			inet_pton(AF_INET, "8.8.8.8", &Dest.sin_addr);

			if (connect(Sock, (sockaddr*)&Dest, sizeof(Dest)) == 0)
			{
				sockaddr_in Local{};
				int LocalLen = sizeof(Local);

				if (getsockname(Sock, (sockaddr*)&Local, &LocalLen) == 0)
				{
					char IpBuf[INET_ADDRSTRLEN] = "";
					inet_ntop(AF_INET, &Local.sin_addr, IpBuf, sizeof(IpBuf));

					if (!IsNonRoutable(IpBuf))
					{
						closesocket(Sock);
						return IpBuf;
					}
				}
			}

			closesocket(Sock);
		}

		// 2) Por hostname, descartando loopback/link-local/multicast.
		char HostName[256] = "";

		if (gethostname(HostName, sizeof(HostName)) == 0)
		{
			addrinfo Hints{};
			Hints.ai_family = AF_INET;
			Hints.ai_socktype = SOCK_STREAM;

			addrinfo* Result = nullptr;

			if (getaddrinfo(HostName, nullptr, &Hints, &Result) == 0 && Result)
			{
				for (auto* Addr = Result; Addr; Addr = Addr->ai_next)
				{
					auto* SockAddr = (sockaddr_in*)Addr->ai_addr;
					char IpBuf[INET_ADDRSTRLEN] = "";
					inet_ntop(AF_INET, &SockAddr->sin_addr, IpBuf, sizeof(IpBuf));

					if (!IsNonRoutable(IpBuf))
					{
						freeaddrinfo(Result);
						return IpBuf;
					}
				}

				freeaddrinfo(Result);
			}
		}

		return {};
	}
}

void HostClient::Register(const std::string& hostId, const std::string& ipOverride, int portOverride)
{
	WSADATA WsaData{};

	if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
		LOG_WARN(LogMatchmaker, "[HostClient] WSAStartup failed!");

	char ComputerName[256] = "";
	gethostname(ComputerName, sizeof(ComputerName));

	HostId = hostId.empty() ? ComputerName : hostId;
	HostIp = ipOverride.empty() ? DetectLocalIpAddress() : ipOverride;
	HostPort = portOverride > 0 ? portOverride : (7777 - Globals::AmountOfListens);

	LOG_INFO(LogMatchmaker, "[HostClient] Registering host {} at {}:{} ...", HostId, HostIp, HostPort);

	nlohmann::json Body;
	Body["hostId"] = HostId;
	Body["ip"] = HostIp;
	Body["port"] = HostPort;
	Body["modes"] = { "solo", "duo", "squad" };

	std::string Response;

	long Status = PostJson("/lawin/hosts/register", Body.dump(), Response);

	if (Status >= 200 && Status < 300)
	{
		bHostConnected = true;
		LOG_INFO(LogMatchmaker, "[HostClient] Registered successfully with the backend.");
	}
	else
	{
		bHostConnected = false;
		LOG_WARN(LogMatchmaker, "[HostClient] Backend rejected the registration (status {}, raw: {}).", Status, Response.substr(0, 200));
	}
}

void HostClient::StartPollThread()
{
	if (bPolling.load())
		return;

	if (!bHostConnected)
		LOG_INFO(LogMatchmaker, "[HostClient] Starting poll thread without a confirmed registration.");

	bPolling.store(true);
	PollThreadHandle = CreateThread(nullptr, 0, PollThreadProc, nullptr, 0, nullptr);

	if (!PollThreadHandle)
	{
		bPolling.store(false);
		LOG_ERROR(LogMatchmaker, "[HostClient] Failed to create poll thread!");
	}
}

void HostClient::Stop()
{
	bPolling.store(false);
}

DWORD WINAPI HostClient::PollThreadProc(LPVOID)
{
	PollLoop();
	return 0;
}

void HostClient::PollLoop()
{
	while (bPolling.load())
	{
		bool bCanJoin = hostState == "starting" && Globals::bInitializedPlaylist;

		nlohmann::json Body;
		Body["hostId"] = HostId;
		Body["hostState"] = hostState;
        const bool ReportingMatchEnd = HostMatchEnding.load();
        Body["matchEnding"] = ReportingMatchEnd;
        Body["hostAccountId"] = HostLoginAccountId();
        unsigned long long PopulationGeneration;
        {
            std::lock_guard<std::mutex> Lock(HostPopulationMutex);
            PopulationGeneration = HostPopulationGeneration;
            Body["presentAccountIds"] = HostPresentAccountIds;
        }
		Body["canJoin"] = bCanJoin;
		Body["playlist"] = PlaylistName;
		Body["countdownRemaining"] = HostCountdownRemainingSeconds.load();
        Body["fillAndBalanceTeams"] = HostFillAndBalanceTeams.load();
		const int ListeningPort = HostListeningPort.load();
		if (ListeningPort > 0) Body["port"] = ListeningPort;

		std::string Response;

		long Status = PostJson("/lawin/hosts/poll", Body.dump(), Response);

		if (Status >= 200 && Status < 300)
		{
			try
			{
				auto Json = nlohmann::json::parse(Response);
                if (ReportingMatchEnd && Json.value("matchEndAcknowledged", false))
                {
                    std::lock_guard<std::mutex> Lock(HostPopulationMutex);
                    if (PopulationGeneration == HostPopulationGeneration &&
                        HostMatchEnding.load())
                        HostMatchEndAcknowledged.store(true);
                }
                if (Json.contains("population") && Json["population"].is_object())
                {
                    const auto& Population = Json["population"];
                    const int Outside = Population.value("outside", -1);
                    const bool AllHere = Population.value("allHere", false);
                    std::lock_guard<std::mutex> Lock(HostPopulationMutex);
                    if (PopulationGeneration == HostPopulationGeneration)
                    {
                        HostPopulationOutside.store(Outside);
                        HostPopulationAllHere.store(AllHere);
                        HostPopulationReceivedAt = GetTickCount64();
                    }
                }

				if (Json.contains("commands") && Json["commands"].is_array())
				{
					for (auto& Cmd : Json["commands"])
					{
						std::string Type = Cmd.value("type", "");

						if (Type == "StartMatch")
						{
							LOG_INFO(LogMatchmaker, "[HostClient] Received StartMatch command!");

							if (Cmd.contains("mode") && Cmd["mode"].is_string())
								LOG_INFO(LogMatchmaker, "[HostClient] Mode: {}", Cmd["mode"].get<std::string>());

							if (Cmd.contains("playlist") && Cmd["playlist"].is_string())
							{
								PlaylistName = Cmd["playlist"].get<std::string>();
								LOG_INFO(LogMatchmaker, "[HostClient] Playlist: {}", PlaylistName);
							}

							{
                                std::lock_guard<std::mutex> Lock(HostTeamAssignmentsMutex);
                                HostTeamAssignments.clear();
                                if (Cmd.contains("groupTeams") && Cmd["groupTeams"].is_object())
                                    for (auto& [AccountId, TeamJson] : Cmd["groupTeams"].items())
                                        if (TeamJson.is_number_integer() && TeamJson.get<int>() >= 3 && TeamJson.get<int>() <= 102)
                                            HostTeamAssignments[AccountId] = TeamJson.get<int>();
                            }
                            HostTeamsDirty.store(true);
                            {
                                std::lock_guard<std::mutex> Lock(HostTeamAssignmentsMutex);
                                LOG_INFO(LogMatchmaker, "[team-debug] command={} assignments={}",
                                    Type, HostTeamAssignments.size());
                                for (const auto& Pair : HostTeamAssignments)
                                    LOG_INFO(LogMatchmaker, "[team-debug] account={} team={}",
                                        Pair.first, Pair.second);
                            }

							bPregameLocked = false;
							bStartPregame = true;
							hostState = "starting";

							LOG_INFO(LogMatchmaker, "[HostClient] Match armed, pregame countdown started.");
						}
                        else if (Type == "UpdateTeams")
                        {
                            {
                                std::lock_guard<std::mutex> Lock(HostTeamAssignmentsMutex);
                                if (Cmd.contains("groupTeams") && Cmd["groupTeams"].is_object())
                                    for (auto& [AccountId, TeamJson] : Cmd["groupTeams"].items())
                                        if (TeamJson.is_number_integer() && TeamJson.get<int>() >= 3 && TeamJson.get<int>() <= 102)
                                            HostTeamAssignments[AccountId] = TeamJson.get<int>();
                            }
                            // UObject access is deferred to the game thread.
                            HostTeamsDirty.store(true);
                            {
                                std::lock_guard<std::mutex> Lock(HostTeamAssignmentsMutex);
                                LOG_INFO(LogMatchmaker, "[team-debug] command={} assignments={}",
                                    Type, HostTeamAssignments.size());
                                for (const auto& Pair : HostTeamAssignments)
                                    LOG_INFO(LogMatchmaker, "[team-debug] account={} team={}",
                                        Pair.first, Pair.second);
                            }
                            LOG_INFO(LogMatchmaker, "[HostClient] Team update received (final={}).", Cmd.value("finalized", false));
                        }
						else
						{
							LOG_WARN(LogMatchmaker, "[HostClient] Unknown command type: {}", Type);
						}
					}
				}
			}
			catch (const std::exception& e)
			{
				LOG_WARN(LogMatchmaker, "[HostClient] Failed to parse poll response: {}", e.what());
			}
		}
		else if (Status == 404)
		{
			// The backend was restarted and lost the in-memory host registry.
			// Re-register so the matchmaker accepts our polls again.
			LOG_WARN(LogMatchmaker, "[HostClient] Backend no me conoce (404), re-registrando el host...");
			Register(HostId, HostIp, HostPort);
		}

		Sleep(2000);
	}
}

long HostClient::PostJson(const std::string& path, const std::string& body, std::string& outResponse)
{
	static CURL* Curl = curl_easy_init();

	if (!Curl)
		return 0;

	PostResponse Response;

	curl_slist* Headers = curl_slist_append(nullptr, "Content-Type: application/json");

	std::string Url = std::string(BackendUrl) + path;

	curl_easy_setopt(Curl, CURLOPT_URL, Url.c_str());
	curl_easy_setopt(Curl, CURLOPT_POST, 1L);
	curl_easy_setopt(Curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(Curl, CURLOPT_HTTPHEADER, Headers);
	curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &Response);
	curl_easy_setopt(Curl, CURLOPT_CONNECTTIMEOUT, 3L);
	curl_easy_setopt(Curl, CURLOPT_TIMEOUT, 5L);
	curl_easy_setopt(Curl, CURLOPT_NOSIGNAL, 1L);

	CURLcode PerformResult = curl_easy_perform(Curl);

	curl_slist_free_all(Headers);

	if (PerformResult != CURLE_OK)
	{
		LOG_WARN(LogMatchmaker, "[HostClient] POST {} failed: {}", path, curl_easy_strerror(PerformResult));
		return 0;
	}

	curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &Response.StatusCode);
	outResponse = Response.Body;

	if (Response.StatusCode < 200 || Response.StatusCode >= 300)
		LOG_WARN(LogMatchmaker, "[HostClient] POST {} returned status {}", path, Response.StatusCode);

	return Response.StatusCode;
}
