# CustomBot - Research: Architecture Overview

## Objetivo
Crear un sistema CUSTOM de bots independiente del sistema antiguo (`bots.h`, `PlayerBot`,
`AFortAthenaAIBotController`). El sistema antiguo permanece intacto. NO parchear encima.

## Build System
- MSBuild / Visual Studio 2022 (v143), NO CMake.
- Solución: `Project Reboot 3.0.sln`
- Proyecto: `Project Reboot 3.0/Project Reboot 3.0.vcxproj`
  - DynamicLibrary, stdcpplatest, x64
  - Includes adicionales: `../vendor`
  - Define opcional: `ABOVE_S20`
- Linux build: `build-linux.sh` (msbuild via msvc-wine)
- CI: `.github/workflows/msbuild.yml` (windows-latest, `msbuild "Project Reboot 3.0.sln" /p:Configuration=Release /p:Platform=x64 /m`)
- Filter map: `Project Reboot 3.0.vcxproj.filters`
- NOTA: Todo archivo .cpp/.h nuevo debe añadirse al .vcxproj (y opcionalmente al .filters).

## Ubicación del código
- Todo el código fuente: `Project Reboot 3.0/` (236 .h, 96 .cpp)
- Third-party: `vendor/`
- Claves: `FortGameModeAthena.{h,cpp}`, `FortPlayerController.{h,cpp}`,
  `FortPlayerControllerAthena.{h,cpp}`, `FortPlayerPawnAthena.{h,cpp}`,
  `FortPlayerPawn.{h,cpp}`, `FortPawn.{h,cpp}`, `FortPlayerStateAthena.h`,
  `FortInventory.{h,cpp}`, `FortWeapon.{h,cpp}`, `BancoActor.{h,cpp}` (BuildingActor),
  `BuildingSMActor.h`, `FortPickup.{h,cpp}`, `FortKismetLibrary.{h,cpp}`, `commands.cpp`.

## Sistema de bots antiguo (NO tocar)
- `bots.h` (462 líneas): clase `PlayerBot`, namespace `Bots` (SpawnBot, Tick), `Bosses`.
- `FortServerBotManagerAthena.{h,cpp}` (SpawnBotHook).
- `FortAthenaAIBotController.h`, `FortAthenaAIBotSpawnerData.h`, etc.
- Integraciones: `FortGameModeAthena.cpp` (InitializeBotClasses, SpawnBotsAtPlayerStarts,
  bot-kill en línea 1100), `commands.cpp` (spawnbot/bottest), `dllmain.cpp` (InitBotNames,
  hook SpawnBot), `NetDriver.cpp` (Bots::Tick comentado), `PlayerState.cpp` (IsBot).

## Claves del bot antiguo (referencia, no copiar)
- `PlayerBot::Initialize` spawna Controller (AFortPlayerControllerAthena) + Pawn
  (PlayerPawn_Athena_C) + PlayerState, `SetIsBot(true)`, team, inventario, loadout,
  cosméticos.
- `Bots::SpawnBot(FTransform, AActor*)` -> devuelve el Controller.
- `PlayerBot::SetupInventory` usa `AFortPlayerController::GetWorldInventory()` y
  `AddPickaxeToInventory` + `ServerExecuteInventoryItemHook`.
- Los bots antiguos saltan vía `ProcessEvent(/Script/Engine.Character.Jump)`.
