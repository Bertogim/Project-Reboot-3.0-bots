Objective
- Resolver el leak explosivo de RAM con bots (~230MB/s a 25 bots en el run previo; ~180MB/s a 50 bots medido hoy) y dejarlo verificado. El leak idle (0 bots) ya está resuelto.
Important Details
- El usuario lanza el servidor él mismo; yo vigilo reboot.log. Pidió vigilarlo cada 10s con sleeps cortos ("no hagas tanto sleep": usar ~8-10s).
- Leak idle resuelto: la causa era LogMemDiag con GetName() (std::string por el malloc del juego); desactivado en 658eb16 → WS plano 2824MB con 0 bots.
- No reintentar A/B forcing bShouldUseReplicationGraph=false: provoca AV (hook clásico + RepDriver del engine = doble replicación); revertido en 5680adc y pusheado.
- Dato clave del run de hoy: UObjects total plano (~614k-618k; solo +~111/bot transitorio al spawinear) mientras WS/committed/regions explotan ⇒ el leak es la arena del malloc del juego (buffers/alocs), no acumulación de objetos del engine.
- Replicación manual DESCARTADA como causa: bManualReplication=true por defecto (globals.h:44; checkbox vivo gui.h:390 "Replicacion manual por tick (A/B leak)"). Test en vivo con toggle OFF → sraCalls=0 → el leak sigue subiendo igual.
- sraCalls a 30/s constante con cualquier nº de bots (0/5/20/50) y 0 con el toggle OFF.
- Crecimiento en bins nuevos de ~64KB (regions 45012→71454→134256); top-8 regiones fijas (0x12970000=64MB, etc.); PageFaults=0 en ambos runs.
- Bot por-tick: TickAll (CustomBotSpawner.h:69) → TickCustomBotSafe (SEH, CustomBotDebug.cpp:1739) → EnsureCMCActive, Bot->Tick (no aloca), UpdateMovement, TickUnstuck, skin diferida (máx 2/tick), CustomBotAI::Tick → Midgame::Update (warmup = comportamiento de partida real).
- Revisados y limpios: CustomBotMovement (pares Alloc/VirtualFree), CustomBotCombat (no aloca buffers propios), CustomBotBuilding (BuildPiece libera ExistingBuildings y destruye actores viejos).
- Build remoto: ./build-vm.sh (compila en VM Windows vía SSH; --clean; baja el DLL a Build/).
- HEAD = 40966f6 (contador UObject ligero), build OK (DLL 12 sep 00:09). Push confirmado hasta 5680adc; 40966f6 sin confirmación de push.
Work State
Completed
- Commit 40966f6: LogMemDiag ahora loguea [memdiag] UObjects total={N} con scan O(n) sin GetName() (cero heap). Build OK vía ./build-vm.sh.
- Run 1 (replicación ON): 00:10:35 bots=5 committed=3719 regions=45012; 00:10:55 bots=20 WS=5347 committed=5358 regions=71454 UObjects=614774; 00:11:17 bots=50 WS=9229 committed=9234 regions=134256 UObjects=618012; 00:11:36 WS=12641 Private=13678. El usuario mató el proceso.
- Run 2 (replicación OFF, sraCalls=0): 00:15:11 bots=10 WS=5559 committed=5564 regions=74880 UObjects=612886; 00:15:21 bots=24 WS=6519 UObjects=614444. Sigue subiendo igual ⇒ replicación descartada.
- Conclusión: leak = arena malloc del juego, por-tick por-bot; no es UObjects ni replicación.
- Trazado completo del tick del bot (ver Important Details).
Active
- Auditoría de CustomBotAI_Midgame.h por alocaciones por-tick por-bot en el malloc del juego. Sospechoso: línea 112 std::string Path = Def->GetPathName(); + .find(CatDirs[i]) (línea 119) — falta leer el contexto (~100-140), su frecuencia de llamada (por tick vs cada Decide 0.5s) y el tipo de retorno de GetPathName().
- Última lectura del run 2: 00:15:21 WS=6519MB (bots=24) y seguía subiendo; estado final del servidor sin confirmar (probablemente matado de nuevo).
Blocked
- (none)
Next Move
1. Leer CustomBotAI_Midgame.h ~100-140: identificar la función que llama Def->GetPathName() y su frecuencia; comprobar tipo de retorno y si aloca std::string en el arena del juego por llamada.
2. Revisar los handlers de estado de Midgame por TArray/alocs sin FreeEngine o strings por iteración: DoLooting:258, DoFarming:359, DoExploring:413, DoFighting:622, DoDefending:806, BuildBarricade:758, TickTimers:35, TrySwapForBetterWeapon:229, Decide:835.
3. Implementar fix → ./build-vm.sh → pedir al usuario que lance con bots y comparar committed/regions/WS contra las curvas medidas (p.ej. 50 bots: committed 9234MB en ~40s de spawn; ~180MB/s).
Relevant Files
- Project Reboot 3.0/CustomAI/CustomBotAI_Midgame.h: investigación activa (Update:918, Decide:835, línea 112 GetPathName, handlers de estados).
- Project Reboot 3.0/CustomBot/CustomBotSpawner.h: TickAll:69, LogMemDiag, contador UObject ligero (40966f6), PendingSkinBudget=2, cola DeferredBotOps (1 bot/tick).
- Project Reboot 3.0/CustomBot/CustomBotDebug.cpp: TickCustomBotSafe:1739, BotTickCallbackImpl:1711.
- Project Reboot 3.0/CustomBot/CustomBot.h:230: Bot->Tick (no aloca).
- Project Reboot 3.0/NetDriver.cpp:81-103: gate bManualReplication + TickFlushOriginal.
- Project Reboot 3.0/globals.h:44 y Project Reboot 3.0/gui.h:390: toggle bManualReplication.
- build-vm.sh: build remoto (sin cmd.exe local).
- Build/Project Reboot 3.0.dll: build 12 sep 00:09 = 40966f6.
- reboot.log: /home/bertogim/Escritorio/CI/Programas/Uf3/Uf3/FortniteV3.5/FortniteGame/Binaries/Win64/reboot.log.
- Git remote: https://github.com/Bertogim/Project-Reboot-3.0-bots (master hasta 5680adc; 40966f6 local).