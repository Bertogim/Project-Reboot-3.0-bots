#pragma once

#include <atomic>
#include <string>
#include <unordered_map>

#include "inc.h"

namespace Globals
{
	extern inline bool bCreative = false;
	extern inline bool bGoingToPlayEvent = false;
	extern inline bool bEnableAGIDs = true;
	extern inline bool bNoMCP = true;
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
	extern inline bool bPrivateIPsAreOperator = true;
	extern inline int AmountOfListens = 0; // TODO: Switch to this for LastNum
	extern inline bool bDeveloperMode = false;
}

extern inline int NumToSubtractFromSquadId = 0; // I think 2?

// --- Host / Matchmaker state -------------------------------------------------
extern inline bool bPregameLocked = true;          // pregame waits for Matchmaker StartMatch (or local Force Start)
extern inline bool bStartPregame = false;          // set by StartMatch / Force Start: starts the 10s pregame countdown
extern inline int busCountdownSeconds = 300;       // bus warmup countdown; whittled to 90s once the first player joins
extern inline int lastPlayerCountForBus = 0;       // 0 = no real player joined yet; 1 = first player seen (90s armed)
extern inline bool bHostConnected = false;         // true once the backend accepted /lawin/hosts/register
extern inline std::string hostState = "idle";      // idle -> starting -> inGame
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