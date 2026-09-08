# Compaction de Conocimiento del Debug (estado actual)

Compactacion de todo lo aprendido trabajando en el CustomBot "Season 3". Es el punto de
entrada para retomar el trabajo sin re-descubrir nada. Los docs `01`..`06` desarrollan la
teoria/research; este archivo es el "mapa mental" actual + lecciones de la pasada de build CI.

---

## 0. Proyecto y repo
- Mod DLL C++ para servidor/private Fortnite (**Project Reboot 3.0**): headers de structs del
  engine escritos a mano (`reboot.h`, `Object.h`, `Actor.h`, `Fort*.h`), vendoring `memcury`
  (MinHook) y `spdlog` (detras de `ENABLE_SPD_LOG`; macros `LOG_INFO/LOG_ERROR/...` en `log.h`).
- Repo: `Bertogim/Project-Reboot-3.0-bots`, rama **master**.
- Build: `Project Reboot 3.0.sln` → **Release x64**, solo via GitHub Actions
  `.github/workflows/msbuild.yml` (trigger **manual** `workflow_dispatch`). Artifact `Reboot`
  = carpeta `x64/Release`. NO hay compilacion local: la CI es la unica validacion de compile.
- Comandos CI: `gh workflow run msbuild.yml --ref master` + `gh run watch <id> --exit-status`.
  Errores: `gh run view <id> --log-failed | rg "error [A-Z]"`.
- El entorno de la tool redacta identificadores en outputs `bash/rg` (ej `n`, `ln`): leer con el
  tool `read` para ver el identificador real.

## 1. Regla de oro
- NO tocar el sistema antiguo de bots: `bots.h`, `PlayerBot`, `AFortAthenaAIBotController`,
  `Bots::Tick()`, `AllPlayerBotsToTick`, `NumToSubtractFromSquadId` (globals.h).
- El `Bots::Tick()` en `NetDriver.cpp` sigue COMENTADO; el nuevo sistema corre en paralelo.
- El DebugBot usa SOLO el sistema nuevo. Teletransporte permitido unico: el spawn inicial.

## 2. Arquitectura del CustomBot
- Bot = `AFortPlayerControllerAthena` + `AFortPlayerPawnAthena` + `AFortPlayerStateAthena` +
  `AFortInventory` (WorldInventory): un JUGADOR REAL (heredan todo el gameplay).
- Todo en `Project Reboot 3.0/CustomBot/`. Modulos = headers con funciones `static` dentro de
  namespaces (cada TU compila su propia copia; es el patron del mod, OK).
- `CustomBotTypes.h` → namespace `CBT`: `FMoveRequest`{Destination,AcceptanceRadius,bStopOnArrival},
  `EMovementState`{Idle,Moving,Arrived,BlockedPath}, `ELifeState`. Incluye `reboot.h`.
- `CustomBot.h` → clase `CustomBot` (entidad): `Controller/Pawn/PlayerState/WorldInventory`,
  estado de movimiento (`MoveRequest`, `MoveState`, `bMoveRequestActive`), hook
  `void (*DebugTick)(CustomBot&)` invocado en `Tick()`, `IsReady/IsValidActor/GetLifeState/
  GetHealth/GetShield/HasArrived/IsPathBlocked`, `Destroy()`.
- `CustomBotSpawner.h` → `AllCustomBots` (std::vector<CustomBot>), `TickAll()` (loop seguro,
  borra muertos DESPUES del bucle), `InitializeClasses`, `SpawnCustomBot`.
- Modulos de capacidades (operan sobre `CustomBot&`): `CustomBotMovement` (MoveTo/persistente,
  LookAt/SetRotation/SetYaw, Jump via `Character.Jump`, MoveForward/MoveRight, UpdateMovement),
  `CustomBotPerception` (scans, finders, LOS, equipos), `CustomBotInventory` (GiveItem/equip/
  drop/recoger), `CustomBotCombat` (AimAt/FireWeapon/ReduceLoadedAmmo/Reload), `CustomBotBuilding`
  (BuildWall/Floor/Ramp/Roof → BuildPiece), `CustomBotDestruction` (DestroyTarget/GetStructureHealth/
  IsStructureDestroyed), `CustomBotResources` (GiveResource/GetTotalResourceCount), `CustomBotInteraction`
  (OpenChest/PickupItem/UseConsumable).
- `CustomBotDebug.cpp` → `CustomBotDebug::HandleCommand` (comandos chat) + `StartDebugBot`
  (boton GUI) + maquina de estados `TickDebugBot`.
- Region anonima (`namespace { }`) contiene toda la maquinaria debug: `SendBotMessage`,
  `ToWide`, `GetFirstValidBot`, `DebugBotState`/`DebugBotStateName`, `DebugBotContext`+`gDebugBot`,
  `DebugBotTime`, `FindFirstValidPlayer`, `FindTestWeaponDefinition`, `DebugBotGrantLoadout`,
  `DebugBotRemove`, `DebugBotError`, `TickDebugBot`.

## 3. Wiring en runtime (quien llama a quien)
1. `NetDriver.cpp` `TickFlushHook` → `CustomBotSpawner::TickAll();` (junto al `Bots::Tick()`
   comentado). Incluye `#include "CustomBot/CustomBotSpawner.h"`.
2. `TickAll()`: por bot → `Bot.Tick()` y `CustomBotMovement::UpdateMovement(Bot)`.
3. `Tick()` → si `DebugTick` registrado → `DebugTick(*this)`.
4. GUI: pestana **Fun** → boton **"Spawn Debug Bot (test sequence)"** (gui.h)
   → `CustomBotDebug::StartDebugBot(Cast<AFortPlayerControllerAthena>(GetLocalPlayerController()))`.
5. `StartDebugBot` (CustomBotDebug.cpp): guard anti-duplicado → `FindFirstValidPlayer()` →
   si ninguno, avisa y no crea nada → `SpawnCustomBot` en `player + Fwd*250 + Z50` → `TeleportTo`
   (UNICO teleport) → `DebugBotGrantLoadout` (fallo → `DebugBotRemove`) → registra
   `DebugBot->DebugTick = &TickDebugBot` y `gDebugBot.Step = Spawned`.

## 4. Secuencia DebugBot (maquina de estados)
`Spawned → MovingForward → Jumping → BuildingRamp → ClimbingRamp → Turning → EquippingPickaxe
→ DestroyingRamp → EquippingWeapon → Shooting(×5) → DroppingWeapon → WaitingToDisappear(10s) → remove`
- Timers: `UGameplayStatics::GetTimeSeconds(GetWorld())` (detecta `T - NextActionTime >= 0`).
- Movimientos reales: `MoveTo` (persistente, `UpdateMovement` re-aplica velocidad cada tick),
  `Jump` (`Character.Jump`), construccion real de rampa (`SelectPiece(BuildRamp)+BuildRamp`,
  consume material), pico (`EquipPickaxe`), destruccion de la rampa (pipeline real de estructura;
  el melee de pico y el dato de `GetPieceClass()` dependen de la version), disparos GAS,
  drop real (`ServerAttemptInventoryDropHook`).
- Cleanup `DebugBotRemove`: quita de `GetAlivePlayers()` (`RemoveAt(i,1)`), decrementa
  `GetPlayersLeft()` + `OnRep_PlayersLeft()`, `K2_DestroyActor` del WorldInventory, `Bot.Destroy()`
  (pawn+controller), limpia `DebugTick`/`MoveRequest`/`bMoveRequestActive`/`MoveState`/`gDebugBot`.
  `TickAll` borra el bot muerto del vector al final del loop.
- Logs `spdlog` con prefijo `[DebugBot]` en todos los pasos y errores.

## 5. Firmas verificadas (memoria clave para seguir codeando)
- `reboot.h`: `FindObject<T>(std::string)` :65; `LoadObject(const TCHAR*, T::StaticClass)` :38;
  overload wide de `FindObject<T>(const wchar_t*)` existe (tras :67); `GetWorld()` :92;
  `GetEngine()` :74; `GetLocalPlayerController()` → `UObject*` :124; `Cast<T,bCheckType=true>` :136-152
  (usa `T::StaticClass()` solo si `bCheckType`); `GetAllObjectsOfClass<T=UObject>(UClass*)` :230-252
  → `std::vector<T*>` (ya con `Cast<T,false>`).
- `Object.h:39` `ClassPrivate` PUBLICO; `GetName()/GetPathName()/GetFullName()` → `std::string` (73-75).
- `Cast<AFortPlayerControllerAthena>` valido (tiene `StaticClass`); `Cast<UObject>` NO (usar `Cast<UObject,false>`).
- `FortGameModeAthena.h:271` `GetAlivePlayers()` → `TArray<AFortPlayerControllerAthena*>&`;
  `FortGameStateAthena.h:96` `int& GetPlayersLeft()`; `:161` `AddPlayerStateToGameMemberInfo`;
  `:169` `OnRep_PlayersLeft()`.
- `FortPawn.h:49-52` `SetHealth/SetMaxHealth/SetShield/SetMaxShield`; `:47` `GetShield`.
- `FortPlayerPawn.h:52` `static ServerHandlePickupHook(...)` → llamar SIEMPRE qualificado
  `AFortPlayerPawn::ServerHandlePickupHook(...)`.
- `GameplayStatics.h:13` `UGameplayStatics::GetTimeSeconds(UObject*)`.
- `FMath`: `Sin/Cos` (GenericPlatformMath.h:50/53), `Atan2` (:57), `Sqrt` (:58); `FMath::Clamp` (UnrealMathUtility.h). `FVector` (Vector.h): `operator+/operator-/operator*` son CONST (arreglado),
  `operator|` = dot (const), `SizeSquared()`.
- `TArray`: `Num()`, `at(i[,size])`, `Free()`, `Add`, `RemoveAt`. std::vector: `.erase` con indices
  descendentes para borrado seguro.
- `UGameplayStatics::GetAllActorsOfClass(GetWorld(), UClass*)` → TArray<AActor*> (Perception usa esto).
- UObject `IsA(UClass* base)`; `ProcessEvent(UFunction*, void* Params)`; `FindFunction(name)`.
- `Bot.PlayerState->SetIsBot(true)`; `Controller->Possess(Pawn)`; `ServerChangeName/GameMode->ChangeName`.

## 6. Lecciones de la pasada de compilacion CI (importante, costo ~6min por run)
1. **Convencion de includes**: los cpp de la raiz usan `"CustomBot/X.h"`; los archivos DENTRO de
   `CustomBot/` usan nombres sin prefijo tanto para hermanos (`CustomBot.h`) como para headers de raiz
   (`reboot.h`, `FortItem.h`...). Por eso vcxproj debe incluir `$(ProjectDir)` en
   `AdditionalIncludeDirectories` (4 configs: `../vendor;$(ProjectDir)`). Si se hereda, respetarlo.
2. **Macros LOG_* se rompen en if/else** (log.h:103-111): expenden `if (spdlog::get(...)) info(...);`
   con `;` DENTRO. `if (cond) LOG_INFO(...); else ...` → C2181 (else sin if). SIEMPRE llaves:
   `if (cond) { LOG_INFO(...); } else { ... }`.
3. **FVector const**: los operadores eran no-const → fallan con lvalues `const FVector`. Ya const.
4. **Orden de definicion en namespaces de headers** → C3861 (funcion usada antes de definirse).
   Forward-declarar: `static bool IsEnemy(CustomBot&, AFortPlayerStateAthena*);` al top del namespace.
   Afecta: `SetRotation`, `BuildPiece`, `ActivatePrimaryAbility`, `IsAlly/IsEnemy/GetPlayerStateOf`,
   `SetCustomBotName/GrantAbilities/SetupInventory`.
5. **Default args**: no repetir `= ...` en forward + definicion → C2572 (dejar solo en la forward).
6. **`static inline` en scope de bloque** → C7524 (dentro de funcion usar `static`).
7. **`Cast<T,false>`**: originalmente seguia instanciando `T::StaticClass()` porque el branch era un
   `if` runtime → FIX: `if constexpr (bCheckType)` en la plantilla de `Cast`.
8. **`GetAllObjectsOfClass<T>`**: hacia `push_back(Object)` (UObject*) → error C2665 con `vector<T*>`.
   FIX: `Cast<T,false>(Object)` (ya con IsA arriba).
9. **Hooks estaticos de clases**: `ServerHandlePickupHook` es miembro estatico de `AFortPlayerPawn` →
   llamarlo `AFortPlayerPawn::ServerHandlePickupHook(...)`.
10. **Sintomas CI**: error en `reboot.h(n,m)` casi siempre es una instanciacion de plantilla de OTRO
    archivo; mirar "the template instantiation context is" en el log.

## 7. Comandos de chat que siguen existiendo (`\cmd`)
`spawncustombot`, `cbmove <x> <y> <z>`, `cbscan`, `cbpickup`, `cbequip`, `cbshoot`, `cbbuild
<wall|floor|ramp|roof> [ClassPath]`, `cbdestroy`, `cbgetmaterials`, `cblookat`, `cbpath <x>
<y> <z>`, `cbclimb [n rampas] [ClassPath]`, `cbuse`. Requieren operador (IsOperator) y una
`\` en el chat. `debugbot` YA NO es comando (solo boton GUI).

## 8. Estado del repo (master)
- `69326e9` Add Season 3 custom bot system (Parte 1) with debug commands
- `33c01f3` Add debugbot command and sequence for Custom Bot test (Parte 1)
- `44320a9` → `46a7446` 7 fixes de compilacion CI (includes, operadores, forward decls, macros LOG,
  Cast/if constexpr, GetAllObjectsOfClass)
- `05c6deb` Move debugbot from chat command to a Fun tab button
- Build CI: SUCCESS (Release x64). Artifact `Reboot`.

## 9. Limitaciones conocidas / honestidad
- NUNCA ejecutado en match: compila en CI pero el comportamiento runtime (fisica de la rampa,
  disparos GAS, municion, clean removal) NO esta verificado.
- `GetPieceClass()` y `FindTestWeaponDefinition()` son best-effort (clases/rutas varian por version).
- Melee de pico real pendiente: la destruccion de la rampa usa el pipeline real de estructura
  (`DestroyTarget`/`SilentDie`); el dano por golpe de pico es de Parte 2.
- `UseConsumable` = equipar + activar ability GAS (no hay hook nativo de consumicion; depende de la ability).
- PlayerState del bot NO se destruye en cleanup (GameMemberInfo no tiene API de baja; destruirlo
  dejaria puntero colgante).
- Municion inyectada como loadedAmmo=999 (no stacks de ammo separados).
- `SpawnCustomBot` devuelve puntero a `AllCustomBots.back()`: un futuro `emplace_back` puede
  invalidar punteros existentes → TODO Parte 2: usar indices o `std::unique_ptr`.
- Llamadas a engine desde el hilo de la GUI (patron existente del mod): riesgo de thread-safety.

## 10. Siguiente paso (Parte 2)
- IA/decisiones dentro de `CustomBot::Tick()` apoyada en Perception + modulos (bucle de decision).
- Resolver la invalidez del puntero del vector (indices/unique_ptr).
- Pipeline melee real (pico → dano por golpe) y municion como item real.
- Verificacion en device (partida real) y ajustar best-effort de rampa/arma segun la version.