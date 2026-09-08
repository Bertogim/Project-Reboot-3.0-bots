# CustomBot - Plan de Implementación (Parte 1)

## Estado: FASE 1 COMPLETADA (cuerpo/capacidades) — pendiente revisión en device

## Auditoria prompt Parte 1 (pasada de cierre)
- Comando `debugbot`: secuencia de prueba completa del Custom Bot (sin el sistema
  antiguo). Maquina de estados (DebugBotState) + timers (GetTimeSeconds) dentro de
  CustomBot::DebugTick (registrado en el comando). Un solo debugbot activo a la vez.
  Secuencia: spawn junto al primer jugador real -> MoveForward -> Jump -> BuildRamp
  -> ClimbingRamp -> TurnAround 180 -> EquipPickaxe -> destroy ramp -> equip weapon
  -> 5 disparos -> drop weapon -> wait 10s -> remove. Solo teletransporte inicial.
  Cleanup: remover de GetAlivePlayers, decrementar PlayersLeft, destruir WorldInventory/
  pawn/controller, limpiar DebugTick y estado. PlayerState queda registrado en
  GameMemberInfo (no existe API de baja; documentado como limitacion).
- FindTestWeaponDefinition: rutas conocidas + fallback escaneo de GObjects (best-effort).
- Logs spdlog con prefijo [DebugBot] en todos los pasos y errores.
- Movement: MoveTo persistente (FMoveRequest en CustomBot) + UpdateMovement por tick
  (CustomBotSpawner::TickAll llamada desde NetDriver.cpp TickFlushHook, sin tocar Bots::Tick
  ni bots.h). Added MoveForward/MoveRight/SetRotation/SetYaw + estados Idle/Moving/Arrived/BlockedPath.
- Perception: ScanNearbyObjects (FScanResult), FindNearbyLoot / FindNearestWeapon / FindNearestAmmo /
  FindNearestConsumable / FindNearestResource, FindNearestObstacle + ClassifyObstacle (EObstacleType:
  OwnStructure/EnemyStructure/WorldObject/Structure/Actor), IsPathBlocked, GetHeightDifference,
  FindReachablePoint (anillo de 8 puntos con LOS), FindNearestEnemy/FindNearestAlly + GetActorTeam/GetBuildingTeam/IsAlly/IsEnemy.
- Building: wrappers BuildWall/BuildFloor/BuildRamp/BuildRoof (construccion real con materiales).
- Inventory: DropItem / DropItemByDefinition (ServerAttemptInventoryDropHook real).
- Interaction: UseConsumable (equipa consumible + activa ability GAS via ActivatePrimaryAbility).
- Combat: refactor FireWeapon -> ActivatePrimaryAbility (compartido por arma y consumible).
- Debug nuevos: cbpath (obstaculo/altura/LOS), cbclimb [n rampas] [ClassPath] (sube por rampas),
  cbuse (consumible). Comandos previos intactos.
- Limitacion honesta: objetos del mundo (arboles/rocas) no tienen clases dedicadas en el repo;
  se detectan como ABuildingFoundation/ABuildingSMActor no player-placed (WorldObject). Destruccion
  de recursos automatica por el juego (OnDamageServer). UseConsumable "equip+activar": el consumo
  real del item depende de la ability del consumible (best-effort en esta version).

## Estrategia
Construir un sistema NUEVO e independiente en `Project Reboot 3.0/CustomBot/`.
El bot será un `AFortPlayerControllerAthena` + `AFortPlayerPawnAthena` (LOS MISMOS que usa
un jugador real), de modo que hereda TODO: movimiento, inventario, armas, municion, vida,
escudo, construccion, destruccion, interaccion, danio, eliminacion. NO inventar 'estados
falsos': usar las APIs reales.

## Estructura de archivos
- `CustomBot/CustomBotTypes.h`   - estructuras/enums/RESULT compartidos
- `CustomBot/CustomBot.h`        - entidad orquestadora (CustomerBot): posee controller/pawn/state,
                                   expone APIs de alto nivel por modulo
- `CustomBot/CustomBotMovement.h`- MoveTo, LookAt, Jump, StopMovement, deteccion obstaculos
- `CustomBot/CustomBotPerception.h` - ScanNearbyObjects, FindNearbyLoot, FindNearestWeapon,
                                   detectar jugadores/otros bots, LOS, distancia
- `CustomBot/CustomBotInventory.h` - recoger/soltar/equipar/cambiar/consultar
- `CustomBot/CustomBotCombat.h`    - equipar arma, disparar, recargar, municion, apuntar
- `CustomBot/CustomBotBuilding.h`  - BuildWall/Floor/Ramp/Roof, construir hacia arriba
- `CustomBot/CustomBotDestruction.h`- DestroyStructure, CanDestroy, GetStructureHealth,
                                   destruir objetos del mundo
- `CustomBot/CustomBotResources.h` - consultar/otorgar/gastar materiales
- `CustomBot/CustomBotInteraction.h`- CanInteract, Interact, OpenChest, PickupItem, UseConsumable
- `CustomBot/CustomBotSpawner.h`   - SpawnCustomBot, registro en GameMode/GameState
- `CustomBot/CustomBotDebug.cpp`   - comandos de consola para probar capacidades

Los modulos OPERAN sobre un CustomBot (que expone Controller/Pawn/PlayerState/WorldInventory),
reutilizando las APIs del jugador real.

## Movimiento (importante)
- No hay wrapper AddMovementInput en el repo. El metodo "como jugador" para el bot:
  invocar las UFunctions nativas del Character, o mejor: obtener el CharacterMovement
  component del pawn y manipular Velocity/impulsos. Pero lo mas parecido a input real
  es sobreescribir la entrada del controller NOILABLE.
- Decision de diseno: CustomBotMovement implementara MoveTo() que:
  * calcula direccion al objetivo, 
  * mueve el pawn hacia alli cada tick ajustando la rotacion control hacia el destino,
  * usa el sistema de movimiento nativo del Character (vía CharacterMovement,
    y/o invocando funciones de movimiento nativas si se localizan).
- Jump: ProcessEvent(`/Script/Engine.Character.Jump`).
- LookAt/SetaRotation: setear control rotation + rotar pawn.
- Deteccion de obstaculos: line trace (KismetSystemLibrary) hacia el objetivo + enorme
  diferencia de altura (pendiente) -> PATH BLOCKED; diferencia de altura media -> se podria
  subir / rodear.

## Percepcion
- ScanNearbyObjects(float Radius): TArray<AActor*> via GetAllActorsOfClass (BuildingContainer,
  AFortPickup, AFortPlayerState/Pawn, ABuildingSMActor, mundo).
- Clasificar: armas (FortWeaponItemDefinition), municion (FortAmmoItemDefinition),
  cofres (BuildingContainer), consumibles (FortConsumableItemDefinition buscar class),
  materiales (FortResourceItemDefinition), estructuras (BuildingSMActor), jugadores/bots.

## Integracion con el juego
- No tocar bots.h. Solo anadir registros en GameMode/GameState si es minimo y necesario.
- SpawnCustomBot imitara PlayerBot::Initialize pero con la nueva entidad CustomBot.

## Verificacion
- Añadir archivos al .vcxproj (+ filters) para que compile en CI.
- No compilar localmente (lento). Revisar tipos/includes/APIs manualmente.
- Comandos de debug: spawncustombot, cbmove, cbscan, cbpickup, cbequip, cbshoot,
  cbbuild <wall|floor|ramp|roof> [ClassPath], cbdestroy, cbgetmaterials, cblookat.

## Notas de cierre (Fase 1)
- Fase 2 (IA / decisiones): insertar en CustomBot::Tick() + loop (CustomBotSpawner::TickAll).
- Antes de Fase 2 resolver: SpawnCustomBot devuelve puntero al back() de std::vector<CustomBot>;
  un futuro emplace_back puede invalidar punteros existentes (usar indice o unique_ptr).
- GetPieceClass() es best-effort (rutas de clase varian por version); cbbuild acepta ClassPath.
- Fuentes no tocadas: bots.h / PlayerBot / AFortAthenaAIBotController / Bots::Tick / AllPlayerBotsToTick.
