#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

#include "inc.h"

namespace Globals
{
	// Interruptor maestro: bots custom habilitados.
	inline constexpr bool bBotsEnabled = false;
	extern inline bool bCreative = false;
	extern inline bool bGoingToPlayEvent = false;
	extern inline bool bEnableAGIDs = true;
	extern inline bool bNoMCP = false;
	extern inline bool bLogProcessEvent = false;
	// extern inline bool bLateGame = false;
	extern inline std::atomic<bool> bLateGame(false);

	extern inline bool bInfiniteMaterials = false;
	extern inline bool bInfiniteAmmo = false;
	extern inline bool bShouldUseReplicationGraph = false;

	extern inline bool bHitReadyToStartMatch = false;
	extern inline bool bInitializedPlaylist = false;
	extern inline bool bStartedListening = false;
	extern inline bool bAutoRestart = false; // doesnt work fyi
	extern inline bool bFillVendingMachines = true;
	extern inline bool bPrivateIPsAreOperator = false;
	extern inline int AmountOfListens = 0; // TODO: Switch to this for LastNum
	extern inline bool bDeveloperMode = false;
}

extern inline int NumToSubtractFromSquadId = 0; // I think 2?

// --- Host / Matchmaker state -------------------------------------------------
extern inline bool bPregameLocked = true;          // pregame waits for Matchmaker StartMatch (or local Force Start)
extern inline bool bStartPregame = false;          // set by StartMatch / Force Start: starts the 10s pregame countdown
extern inline int busCountdownSeconds = 0;         // 0 = waiting without an automatic countdown
extern inline int lastPlayerCountForBus = 0;       // 1 = population-based countdown armed
// El tick del servidor consume las solicitudes de la interfaz.
extern inline std::atomic<int> HostCountdownDurationSeconds{300};
extern inline std::atomic<int> HostCountdownRemainingSeconds{300};
extern inline std::atomic<bool> HostCountdownPaused{false};
extern inline std::atomic<int> HostCountdownResetRequest{-1};
extern inline std::atomic<int> HostCountdownPauseRequest{0}; // 1=pausar, 2=reanudar
// Snapshots cross threads; UObject access stays on the game thread.
inline std::mutex HostPopulationMutex{};
inline std::vector<std::string> HostPresentAccountIds{};
inline std::atomic<int> HostPopulationOutside{-1};
inline std::atomic<bool> HostPopulationAllHere{false};
inline unsigned long long HostPopulationReceivedAt = 0;
inline unsigned long long HostPopulationGeneration = 0;
inline std::atomic<bool> HostMatchEnding{false};
inline std::atomic<bool> HostMatchEndAcknowledged{false};

extern inline int InitialPopulationSafeZonePhase = 0;
extern inline bool bPopulationSafeZonePending = false;

// -1 = automatic population; other values select an existing population band.
inline std::atomic<int> HostZoneModeParticipants{-1};
inline std::atomic<int> HostZoneModeRequest{-2}; // -2 = no request
inline std::atomic<bool> HostCleanRestartRequest{false};
static inline int GetZoneModeParticipants(int ActualPlayers)
{
    const int Override = HostZoneModeParticipants.load();
    return Override >= 0 ? Override : ActualPlayers;
}

static inline int GetPopulationSafeZonePhase(int Players)
{
	// Humanos + bots al arrancar el bus.
	if (Players <= 0) return 0;
	// 50+ conserva el inicio y los tiempos nativos de la tormenta.
	if (Players >= 50) return 0;
	// 1-5 empieza en fase 2; 6-49 empieza en fase 1.
	return Players <= 5 ? 2 : 1;
}
extern inline bool bHostConnected = false;         // true once the backend accepted /lawin/hosts/register
extern inline std::string hostState = "idle";      // idle -> starting -> inGame
inline std::mutex HostTeamAssignmentsMutex{};
extern inline std::atomic<bool> HostTeamsDirty{false};
extern inline std::atomic<int> HostListeningPort{0};
extern inline std::unordered_map<std::string, int> HostTeamAssignments = {}; // accountId -> teamIndex (from StartMatch)
extern inline unsigned long long gManualSraCalls = 0;   // diagnostico: veces que el hook replica manualmente
extern inline bool bManualReplication = true;            // ON: el hook replica por tick + original; OFF: solo el original (A/B del leak)

// Aislamiento del leak de RAM de los bots custom (A/B por fases en vivo).
// 0=tick completo | 1=sin IA (Midgame/RefillLobbyHP) | 2=sin IA/movimiento |
// 3=sin IA/mov/CMC | 4=existencia pura (solo se loguea). Cambiar en caliente
// con el slider de la GUI; la pendiente de committed/WS entre fases aisla el
// componente del tick que fuga.
extern inline int gBotTickMode = 0;

extern inline std::string PlaylistName =
"/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo";
// "/Game/Athena/Playlists/gg/Playlist_Gg_Reverse.Playlist_Gg_Reverse";
// "/Game/Athena/Playlists/Playlist_DefaultDuo.Playlist_DefaultDuo";
// "/Game/Athena/Playlists/Playground/Playlist_Playground.Playlist_Playground";
// "/Game/Athena/Playlists/Carmine/Playlist_Carmine.Playlist_Carmine";
// "/Game/Athena/Playlists/Fill/Playlist_Fill_Solo.Playlist_Fill_Solo";
// "/Game/Athena/Playlists/Low/Playlist_Low_Solo.Playlist_Low_Solo";
// "/Game/Athena/Playlists/Bling/Playlist_Bling_Solo.Playlist_Bling_Solo";
// "/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2";
// "/Game/Athena/Playlists/Ashton/Playlist_Ashton_Sm.Playlist_Ashton_Sm";
// "/Game/Athena/Playlists/BattleLab/Playlist_BattleLab.Playlist_BattleLab";
// "/MoleGame/Playlists/Playlist_MoleGame.Playlist_MoleGame"; // very experimental dont use

// Server-side Team Rumble goal; zero preserves the mode's native behavior.
inline std::atomic<int> HostRumbleKillGoal{0};
inline std::atomic<int> HostRumbleLeadingKills{0};
inline std::atomic<bool> HostLategameBusRequest{false};
static inline bool IsHostTeamRumblePlaylist(const std::string& Name)
{
    return Name.find("Playlist_Respawn_24") != std::string::npos;
}

inline bool HostLiveLategameBoarding = false; // Game-thread only.

// Only controllers successfully recovered by the current live bus request.
// Accessed exclusively on the game thread.
inline std::vector<void*> HostLiveLategameRecovered;

inline std::atomic<bool> HostFillAndBalanceTeams{true};
