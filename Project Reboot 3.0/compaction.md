Objective
- RESUELTO: leak explosivo de RAM con bots. Verificado: 100 bots = WS ~2.6GB PLANO (sin fuga sostenida). Antes 5 bots = +44MB/s.
Important Details
- El usuario lanza el servidor él mismo; yo vigilo reboot.log (~10s). El leak idle (0 bots) se resolvió en 658eb16 (GetName fuera de LogMemDiag → WS plano 2824MB).
- No reintentar bShouldUseReplicationGraph=false (AV por doble replicación); revertido/empujado en 5680adc.
- A/B previo: replicación manual (bManualReplication true, globals.h:44) DESCARTADA (toggle OFF → sraCalls=0 → sigue fugando). UObjects plano ⇒ leak era la arena del malloc del juego, no objetos del engine.
- CAUSA RAIZ FINAL (doble capa, ambas reales):
  1) FName::ToString() (UnrealNames.cpp:8 y 71) filtraba UN FString del engine por llamada: el params
     { FName InName; FString OutStr; } lo llenaba ProcessEvent (Conv_NameToString) con un TArray<TCHAR>
     asignado con FMemory en la arena del juego, y NADIE lo liberaba (dtor de FString solo hace
     Data.Data=nullptr; Free() = VirtualFree, incorrecto). CADA ToString() = leak en la arena.
  2) EnsureCMCActive (CustomBotMovement.h) llamaba GetOffset() 9 veces POR TICK POR BOT → GetProperty
     escaneaba la cadena de propiedades completas y por cada propiedad FName::ToString() → cientos de
     FString filtrados por tick/bot. Binned asigna en páginas de 64KB ⇒ ~1 región nueva/bot/tick
     (regions +285/s a 5 bots) + committed +45MB/s. UObjects plano, PageFaults=0.
- Bisect en vivo que condujo a ello (slider gBotTickMode, memdiag mode={}): mode 4/3 planos; mode 2/0
  ~44MB/s; mode 5 (init-only con CLAIM incluido) plano ⇒ el leak era EXACTAMENTE el bloque por-tick
  de EnsureCMCActive; el claim/estado live eran inocentes.
- RestorePawnPlayerState: se llama SOLO al spawn (EnableServerSimulation + primer tick de EnsureCMCActive).
  Off total ⇒ InitializeCharacterParts reintenta parts cada tick (UObjects 619708, ~100MB/s). Quitar el
  restore periódico de 30 ticks: re-triggeraba OnRep_PlayerState → mesh reload.
- FIX APLICADO (build 12 sep 20:09 OK):
  - UnrealNames.cpp (AMBAS ToString): Conv_NameToString_Params.OutStr.Data.FreeEngine() después de
    copiar a std::string. Cura de raíz para TODO el código que use ToString (GetProperty, Object.cpp...).
  - CustomBotMovement.h: los 9 GetOffset del bloque por-tick ahora son `static` (se cachean la primera
    vez). El bloque por tick ya no escanea NADA. Tambien Object.cpp GetPathName/GetFullName (alyre) con
    P.Data.FreeEngine() y CachedPathForDef en CustomBotPerception.h (fix previo de la capa GetPathName).
- gBotTickMode (globals.h:51) + slider GUI 0..6 (gui.h): 0=full 1=noAI 2=noAI/mov 3=noAI/mov/CMC
  4=existence 5=initNoWrites 6=initNoClaim. Se deja por si se auditan otros componentes.
- Build remoto: ./build-vm.sh --clean (VM Windows vía SSH; DLL en Build/).
Work State
Completed
- Verificación FINAL: 20:12-20:15 (DLL 20:09) mode 0: 5 bots → 2790→2792→2759MB plano; luego 90 bots →
  2818MB plano (bots=90). Pre-fix: 5 bots = +44MB/s (2792→4993 en ~40s). 100 bots ~2.6GB declarado por
  el usuario. LEAK ELIMINADO.
- Builds OK: 18:20 (fix GetPathName+cache), 19:29 (modos 5/6), 19:59 (fix raíz ToString+statics),
  20:09 (+arreglo bStartedBus: el cambio nuevo del usuario usaba Globals::bStartedBus inexistente,
  la variable real es global bStartedBus en gui.h:104).
- Biografía del fix: GetPathName era una capa real pero insuficiente (Run 18:20 siguió fugando 44MB/s);
  el bisect 5/4/3 plano vs 2/0 ~44MB/s delataron el bloque por-tick de EnsureCMCActive → ToString.
Active
- Commit + push de HEAD actual (40966f6 + fix raíz ToString/statics + restore spawn-only + modos 0-6 +
  bStartedBus). master remoto en 5680adc.
Blocked
- (none)
Next Move
1. Commit del conjunto (ver diff: 11 ficheros) con mensaje descriptivo.
2. git push (master → origin). Confirmar.
3. (frio) El slider 0..6 y memdiag quedan operativos; no necesario quitarlos.
Relevant Files
- Project Reboot 3.0/UnrealNames.cpp: FName::ToString() (const y no-const) → FreeEngine del FString de salida. CAUSA RAIZ.
- Project Reboot 3.0/CustomBot/CustomBotMovement.h: EnsureCMCActive (offsets static, restaura PS solo al spawn, modes 5/6, RestorePawnPlayerState).
- Project Reboot 3.0/CustomBot/CustomBotDebug.cpp:1712: BotTickCallbackImpl con gates de gBotTickMode 0-6.
- Project Reboot 3.0/CustomBot/CustomBotSpawner.h: memdiag diag con mode= + LogMemDiag (contador ligero, 40966f6).
- Project Reboot 3.0/Object.cpp: GetPathName/GetFullName FreeEngine (capa 1 del fix).
- Project Reboot 3.0/CustomBot/CustomBotPerception.h + CustomBotAI_Midgame.h: CachedPathForDef (capa 1 del fix).
- Project Reboot 3.0/FortGameModeAthena.cpp: StartAircraftPhase (hostState/bStartedBus, cambio matchmaking del usuario).
- Project Reboot 3.0/globals.h:51 y gui.h:389: gBotTickMode + slider.
- Build/Project Reboot 3.0.dll: build 12 sep 20:09.
- reboot.log: /home/bertogim/Escritorio/CI/Programas/Uf3/Uf3/FortniteV3.5/FortniteGame/Binaries/Win64/reboot.log.
- Git remote: https://github.com/Bertogim/Project-Reboot-3.0-bots (master hasta 5680adc; 40966f6 + fix local sin push).