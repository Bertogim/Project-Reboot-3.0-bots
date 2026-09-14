# Compaction — estado actual del proyecto (14 sep 2026)

## Objetivo global
DLL de bots custom para servidor LAN de Fortnite v3.5 + launcher Flutter
(Reboot-Launcher) que reinicia la partida al detectar victory royale. Todo el
dialogue con el usuario es en espanol.

Empuje final (13-14 sep 2026): resolver 5 issues reportados por el usuario y
dejar la partida 100% jugable (victory royale, teams tab, looting a cofres,
bots con armas/pico, toggle de possession).

---

## Estado actual de los 5 issues (commit `3bdda7e`, push OK)

1. **Nombres garbled en Teams tab** — RESUELTO.
   - **Causa**: `APlayerState::GetPlayerName()` (PlayerState.cpp) usaba
     `ProcessEvent(GetPlayerName)` que en v3.5 devuelve un `FString` con
     puntero colgante → nombres basura (`@orphk=oq,01`, `K(no se)^sdn`).
   - **Fix**: leer directo por offset `PlayerNamePrivate` (con fallback a
     `PlayerName`). Es exactamente lo que ya hace FortServerBotManagerAthena.cpp:56
     para escribir el nombre. ESTE es el patron correcto en v3.5, no ProcessEvent.

2. **Victory royale no se dispara al matar el ultimo bot** — RESUELTO.
   - **Causa raiz (confirmada por el usuario)**: el host NO esta en
     `GetAlivePlayers()` en v3.5 → tras morir el ultimo bot `AliveTeams.size()==0`,
     y el check pedía `== 1` (nunca se cumple).
   - **Fix**: `CheckVictoryRoyale()` (FortGameModeAthena.h:349) ahora usa
     `AliveTeams.size() <= 1` con el guard `bVictoryRoyaleHadTeams` (evita
     victoria falsa si nunca hubo 2+ equipos). Emite `[VictoryRoyale] 1`.

3. **Bots pueden ganar victory royale** — YA FUNCIONABA (implícito en el fix #2:
   se cuenta el equipo ganador sin importar si es bot o jugador).

4. **Bots en Looting pasean en radio ~5m sin hacer nada** — RESUELTO.
   - **Causa**: `DoLooting` (CustomBotAI_Midgame.h) cuando no encontraba loot a
     900-2100u hacia `PickWanderTarget` (400-1300u alrededor) → daban vueltas
     sin acercarse a nada.
   - **Fix**:
     - `HasNearbyLoot` ahora tambien detecta cofres cercanos (antes solo pickups)
       → los bots entran en estado Looting con cofres presentes.
     - `DoLooting` fallback: navega AL COFRE SIN ABRIR MAS CERCANO DEL MAPA usando
       el cache global `CustomBotPerception::CachedChests()` (barrera compartida,
       posicion + flag bSearched) en vez de pasear. Fallback final a wander solo
       si no queda ningu cofre en el mapa.

5. **Bots no usan/cogen pico y armas** — RESUELTO (con toggle possession).
   - El codigo de combate YA equipaba pico/armas (DoFighting/DoFarming/EquipBestWeapon),
     pero el problema era que nunca llegaban al loot (issue #4).
   - Ademas, `CustomBotInventory::EquipItem`: con `gBotPossessBots` ON usa PRIMERO
     `ServerExecuteInventoryItemHook` (path completo del jugador; requiere
     `Controller->GetPawn()` — con UnPossess ese GetPawn() es nullptr y el path
     falla silenciosamente). Con possession OFF usa `EquipWeaponDefinition`
     directo sobre el pawn (funciona sin controller).

---

## Toggle de possession (`gBotPossessBots`)

- **Definido** en globals.h (default false). Checkbox "Poseer bots" en la pestaña
  Bots de gui.h (~linea 1507).
- **Comportamiento correcto (IMPORTANTE, decidido con el usuario)**:
  - `EnableServerSimulation` (CustomBotMovement.h) SIEMPRE se ejecuta en el spawn
    (RUNPHYS: `SetIsBot(false)` + `bRunPhysicsWithNoController=true`). NO se
    skipea nunca.
  - Solo el `UnPossess()` interno es condicional a `!gBotPossessBots`: con el
    toggle ON el bot queda POSEIDO pero la serversimulation CMC sigue intacta.
  - NO marcar `bKeepPossessed=true` en spawn (con numeración anterior se hacia;
    ahora no hace falta, y ReleaseForSimulation haria UnPossess al saltar del
    avion). Con toggle ON y bKeepPossessed=false, ReleaseForSimulation hace early
    return → el bot nunca se desposeye.
- **Objetivo real del toggle** (lo que pidio el usuario): poder usar el path de
  equipar armas y disparar que necesita possession. No es un "modo diagnostic",
  es funcional.
- **Pendiente de validar**: nunca se probo RUNPHYS con possession on/off; verificar
  en partida real que con toggle ON los bots disparan y con OFF se mueven OK.

---

## Arquitectura clave (para no perderse mañana)

### Percepcion (CustomBotPerception.h)
- **SharedBucket por clase de actor**: una sola `GetAllActorsOfClass` de mundo por
  clase y por `SharedCooldown=0.5s`; todos los bots filtran por distancia cuadrada
  sobre `Bucket.Locations` cacheado (cero llamadas nativas frias).
- **CachedChests()**: vector global de `{FVector Location; bool bSearched}` de todos
  los BuildingContainer del mundo. Lo usan PickLandingPoint (aterrizar cerca de
  cofre) y ahora DoLooting (navegar a cofre lejano).
- **RefreshLootCache** (ScanCooldown=0.35s): un barrido de FortPickup clasifica
  armas/consumibles/otro + otro de BuildingContainer → CachedNearest*.

### IA (CustomAI/)
- `CustomBotAI.h`: master tick (TickMidgame/TickBus), `PickLandingPoint(Aggression,
  RiskTolerance)` — aterriza cerca de un cofre sin abrir del mapa.
- `CustomBotAI_Midgame.h`: `Decide()` (maquina de estados), `DoLooting` (~275),
  `DoFarming` (~414), `DoExploring` (~468), `DoFighting` (~725), `EquipBestWeapon`
  (~210), `TrySwapForBetterWeapon` (~246), `HasNearbyLoot` (~951).
- `CustomBotAI_Bus.h`: `ReleaseForSimulation` (~104): sibKeepPossessed=false early
  return; si no UnPossess al saltar del avion.

### Inventario/combate (CustomBot/)
- `CustomBotInventory.h`: `EquipItem` (~122, path possession/logic segun toggle),
  `EquipPickaxe` (~161, usa EquipItem), `EquipItemByGuid` (~108, hook controller),
  `DropItem`/`GiveItem`/`GetItemCount`/`SetLoadedAmmo`.
- `CustomBotCombat.h`: `FireWeapon` (~105) → `ActivatePrimaryAbility` (~115, activa
  la 1ª spec activable del ASC del PlayerState via `InternalTryActivateAbilityOriginal2`),
  `IsPickaxeEquipped`, `EnemyHasRealWeapon`, `StopFiring`.
- `CustomBot.h`: struct del bot (BotAIContext, FMoveRequest, flags CMC: bCMCInitialized,
  bInAirPhase, bFiringWeapon + timeout 0.5s, bKeepPossessed, GroundGravityZ/Scale).

### Boss/Fisica
- `CustomBotSpawner.h`: `SpawnCustomBot` (~600-710), `ProcessBotDeathCounters`
  (quita de AlivePlayers y llama CheckVictoryRoyale), `TickAll` (NetDriver.cpp:79).
- `CustomBotMovement.h`: `EnableServerSimulation` (~115), `EnsureCMCActive` (clave
  para CMC; los GetOffset estaticos para no fugar), `MoveTo`/`LookAt`/`Jump`.

### Victory royale
- `FortGameModeAthena.h` (~294-354): flags `bVictoryRoyaleLogged`/`bVictoryRoyaleHadTeams`
  (estaticos inline) + `CheckVictoryRoyale()`; resetea en warmup; emite
  `[VictoryRoyale] 1` cuando AliveTeams<=1 y alguna vez hubo 2+.
- Se llama desde: `FortPlayerController.cpp` ClientOnPawnDiedHook (muerte real) y
  `CustomBotSpawner.h` ProcessBotDeathCounters (muerte bot).

### Launcher (Reboot-Launcher)
- `kGameFinishedLine = "[VictoryRoyale]"` en game_constants.dart:26.
- `handleGameOutput` (game_metadata.dart:259-284) parsea fin de partida; reboot 25s.
- Build via GitHub Actions (workflow build.yml, tag v* o dispatch). Launcher content
  ya commiteado (b202634). Run anterior OK (34874547533).

---

## Flujo de victory royale (para debug rapido)
1. Host mata ultimo bot → `ProcessBotDeathCounters` → quita bot de GetAlivePlayers
   → `CheckVictoryRoyale()`.
2. En v3.5 el host NO esta en GetAlivePlayers → AliveTeams.size()==0.
3. Con `bVictoryRoyaleHadTeams=true` (hubo 2+ equipos antes) y size<=1 → emite
   `[VictoryRoyale] 1`.
4. Launcher lee esa linea → reinicia partida.

## Flujo de muertede bots (importante)
El engine NO ejecuta el flujo nativo de muerte para bots simulados (SetIsBot=false
+ UnPossess). `TickAll` detecta muerte (pawn destruido o Health<=0, DBNO=vivo) y
`HandleBotDeath` completa: killer via Instigator, ClientReportKill + KillScore,
--PlayersLeft + OnRep, quitar de GetAlivePlayers, Destroy. Flag bDeathHandled
anti-doble proceso (commit c8ad58e).

---

## Comandos y build
- **DLL**: `./build-vm.sh --clean` (VM Windows por SSH, artefacto `Build/Project Reboot 3.0.dll`).
- **Launcher**: push a tag `v*` en Reboot-Launcher o `gh workflow run build.yml` → GitHub Actions.
- **Leak RAM**: YA RESUELTO (ver commit 658eb16 + fix UnrealNames.cpp FreeEngine +
  GetOffset estaticos en EnsureCMCActive). WS plano ~2.6GB con 100 bots. No reabrir.

## Pendiente / continuar mañana
- [ ] Verificar build DLL del commit 3bdda7e (corriendo en segundo plano al escribir esto).
- [ ] Verificar build launcher (workflow disparado).
- [ ] Probar en partida los 5 fixes + toggle possession (movimiento / disparo).
- [ ] Confirmar que el nombre del host real sale bien en Teams tab (era el caso garbled #1).
- [ ] Si el toggle possession va bien, considerarnolo en la tabla de estado / documentar comportamiento.