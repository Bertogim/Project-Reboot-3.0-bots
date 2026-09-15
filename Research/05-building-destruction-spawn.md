# CustomBot - Research: Construcción, Destrucción y Spawn

## Piezas de construcción (Building Tools)
Seleccionar pieza = dar al jugador la pieza + equiparla mientras tiene el building tool,
lo que cambia DefaultMetadata del FortWeap_BuildingTool. Paths:
- Roof:  /Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS
         + metadata /Game/Building/EditModePatterns/Roof/EMP_Roof_RoofC.EMP_Roof_RoofC
- Floor: /Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor
         + metadata /Game/Building/EditModePatterns/Floor/EMP_Floor_Floor.EMP_Floor_Floor
- Wall:  /Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall
         + metadata /Game/Building/EditModePatterns/Wall/EMP_Wall_Solid.EMP_Wall_Solid
- Stair: /Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W
         + metadata /Game/Building/EditModePatterns/Stair/EMP_Stair_StairW.EMP_Stair_StairW
- EditTool: /Game/Items/Weapons/BuildingTools/EditTool.EditTool (clase FortWeap_EditingTool)
- Pickaxe: /Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01

Seleccion de pieza en ServerExecuteInventoryItemHook (FortPlayerController.cpp:304-420),
solo para Engine_Version < 420 (metadata swap).

## Crear building actor (construir)
ServerCreateBuildingActorHook (FortPlayerController.cpp:846-994):
1. Params en Stack->Locals (dos layouts: >=8.30 con BroadcastRemoteClientInfo, <8.30 directo).
2. Validaciones: IsWorldLocValid (StructuralSupportSystem), IsPlayerBuildableClass, CantBuild.
3. Spawn: `GetWorld()->SpawnActor<ABuildingSMActor>(BuildingClass, Transform)` con
   Transform.Translation = BuildLocation, Rotation = BuildRotator.Quaternion(), Scale3D = 1.
4. Consumo de materiales:
   - MatDefinition = K2_GetResourceItemDefinition(BuildingActor->GetResourceType())
   - if !DoesBuildFree: coste = 10 (hardcode), FindItemInstance + RemoveItem(MaterialCost),
     si no hay material -> SilentDie() + return.
5. Destruye ExistingBuildings solapados.
6. `BuildingActor->SetPlayerPlaced(true)`,
   `BuildingActor->InitializeBuildingActor(Controller, BuildingActor, true)`,
   `BuildingActor->SetTeam(PlayerStateAthena->GetTeamIndex())`.

## Grid de construccion y snap (IMPORTANTE, aplicado en CustomBotBuilding)
- El SERVIDOR no aplica snap: spawna exactamente el BuildLoc/BuildRot que recibe; quien
  ajusta al grid es el CLIENTE del jugador real antes de mandar el RPC. Por eso el bot
  debe replicar ese ajuste SI mismo (no llega un paquete de cliente).
- Funciones nativas del grid (UBuildingStructuralSupportSystem, confirmadas en
  ObjectsDump v3.5; activadas desde BuildPiece para TODAS las builds):
  - `K2_GetGridIndicesFromWorldLoc(WorldLoc)->(bool, OutGridIndices{X,Y})`
  - `K2_GetWorldLocFromGridIndices(GridIndices)->(bool, OutWorldLoc)`
  - `GetGridBox(CellIndex)->FBox` (FBox = FVector Min + FVector Max)
  - `K2_CanAdd{Wall,Floor}ActorToGrid`, `K2_CanAddCenterCellActorToGrid` etc.
- Props de tamano de grid en BuildingActor: `VertSnapGridSize`, `SnapGridSize`;
  BuildingSMActor: `PlayerGridSnapSize`.
- CustomBotBuilding.h helpers: `GridIndicesFromWorldLocation`, `CellBoxFromIndices`,
  `SnapLocationToGrid` (X/Y -> centro de celda), `SnapYawToCardinal` (90 grados),
  `CellCenterAhead(SSS, From, CardinalYaw, Steps) -> (bool, OutCenter)`.
- BuildPiece ahora: snap X/Y a centro de celda + yaw a cardinal, y valida/spawnea con el
  punt producto resultante (BuildLoc/BuildRot). Logs: `[BuildPiece] snapped loc ...`,
  `[BuildPiece] snapped yaw ...`.
- Orientacion de la rampa: las StairW suben hacia +X local; par colocarlas con la
  entrada frente al bot hay que darles yaw = cardinal(hacia la que apunta el bot) + 180.

## Destruir estructuras
- Daño real via arma (pickaxe melee) -> OnDamageServer -> BuildingActor.cpp hook.
- `ABuildingActor::SilentDie()` / `K2_DestroyActor()`.
- `ABuildingActor::GetHealth()`, `GetMaxHealth()`, `GetHealthPercent()`, `IsDestroyed()`,
  `IsPlayerBuildable()`, `SetTeam(uint8)`.
- `ABuildingSMActor::GetResourceType()`, `IsPlayerPlaced()`, `SetPlayerPlaced(bool)`,
  `GetCurrentBuildingLevel()`, `GetEditingPlayer()`.
- Destruccion para atravesar: usar arma (daño melee) contra la estructura, o
  SilentDie/K2_DestroyActor si se permite (para bot, mejor usar el arma para que sea
  "como un jugador", pero exponer ambas).
- Objetos del mundo (arboles/rocas) que dan materiales: son BuildingActor/ABuildingSMActor
  en muchos casos; su destruccion por arma melee da recursos (OnDamageServerHook).

## Spawn / PlayerStarts / GameMode / GameState
- PlayerStarts: FortPlayerStartCreative (`/Script/FortniteGame.FortPlayerStartCreative`) y
  FortPlayerStartWarmup (`/Script/FortniteGame.FortPlayerStartWarmup`), via
  `UGameplayStatics::GetAllActorsOfClass`. Usar GetTransform() de cada PlayerStart.
- Spawn actor: `GetWorld()->SpawnActor<AController>(Class)` y
  `GetWorld()->SpawnActor<AFortPlayerPawnAthena>(PawnClass, SpawnTransform, CreateSpawnParameters(...))`.
- CreateSpawnParameters(ESpawnActorCollisionHandlingMethod, bool, controller).
- GameMode: `Cast<AFortGameModeAthena>(GetWorld()->GetGameMode())`.
  - `Athena_PickTeamHook(GameMode, 0, Controller)` -> team index.
  - `GetAlivePlayers()`, `GetStartingItems()`, `ChangeName(Controller, NewName, true)`.
- GameState: `Cast<AFortGameStateAthena>(GetWorld()->GetGameState())`.
  - `AddPlayerStateToGameMemberInfo(PlayerState)`, `GetPlayersLeft()`, `OnRep_PlayersLeft()`.
- `GetWorld()->GetGameMode()` / `GetWorld()->GetGameState()` son funciones libres (reboot.h).
- `AFortPlayerStateAthena`: GetTeamIndex(), GetSquadId(), IsBot(), SetIsBot(bool),
  HeroType offset.
- `UGameplayStatics::GetTimeSeconds(GetWorld())`.

## Equipar (seleccionar) un item del inventario
- `AFortPlayerController::ServerExecuteInventoryItemHook(Controller, FGuid ItemGuid)`.
- Esto gestiona armas, decos/trampas, building pieces y gadgets.

## Interactuar con containers (cofres)
- ABuildingContainer (BuildingContainer.h): `SpawnLoot(Pawn)`, marca bAlreadySearched,
  `BounceContainer()`.
- Interaccion generica: ServerAttemptInteract (UFunction del PlayerController).

## PATRON EXACTO DE SPAWN (old system, bots.h ~252-335) - REFERENCIA KEY
Esto es lo que hace PlayerBot::Initialize y DEBE replicarse en CustomBot/Spawner
(aunque usando solo Controller+Pawn reales, sin AI controller):
1. Classes (PlayerBot::InitializeBotClasses, ShouldUseAIBotController() = false):
   - PawnClass  = `/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C`
   - ControllerClass = AFortPlayerControllerAthena::StaticClass()
2. Spawn en el orden correcto (IMPORTANTE):
   - `Controller = GetWorld()->SpawnActor<AController>(ControllerClass);`
   - `Pawn = GetWorld()->SpawnActor<AFortPlayerPawnAthena>(PawnClass, SpawnTransform, CreateSpawnParameters(AdjustIfPossibleButAlwaysSpawn));`
   - `PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());`
3. `PlayerState->SetIsBot(true);`
4. Si `Controller->GetPawn() != Pawn` -> `Controller->Possess(Pawn);`
5. Nombre: `SetName(NewName)` -> si Fortnite_Version<9 `PlayerController->ServerChangeName(NewName)`,
   si no `GameMode->ChangeName(Controller, NewName, true)`. Luego `PlayerState->OnRep_PlayerName();`
   (APlayerController::ServerChangeName envoltorio en PlayerController.cpp; AGameModeBase::ChangeName
   envoltorio en GameModeBase.cpp).
6. Team: `PlayerState->GetTeamIndex() = GameMode->Athena_PickTeamHook(GameMode, 0, Controller);`
   y luego `PlayerState->GetSquadId() = PlayerState->GetTeamIndex() - NumToSubtractFromSquadId;`
   (NumToSubtractFromSquadId es global en reboot.h / globals).
7. `GameState->AddPlayerStateToGameMemberInfo(PlayerState);`
8. `Pawn->SetHealth(100); Pawn->SetMaxHealth(100);`
9. Abilities: `GetPlayerAbilitySet()` (FortGameModeAthena.h) ->
   `PlayerState->GetAbilitySystemComponent()`; si ambos no nulos:
   `PlayerAbilitySet->GiveToAbilitySystem(AbilitySystemComponent);`
10. Inventory (PlayerBot::SetupInventory, solo parte old):
    - get `AFortInventory**` via Controller->GetWorldInventory() (FortPlayerController) o offset "Inventory".
    - `*Inventory = GetWorld()->SpawnActor<AFortInventory>(FortInventoryClass /* /Script/FortniteGame.FortInventory */, FTransform{}, CreateSpawnParameters(AlwaysSpawn, false, Controller));`
    - `(*Inventory)->GetInventoryType() = EFortInventoryType::World;`
    - si FortPlayerController: `Get<bool>("bHasInitializedWorldInventory") = true;`
    - StartingItems del GameMode (GetStartingItems()) -> AddItem(cada item, nullptr, count).
    - `FortPlayerController->AddPickaxeToInventory()` y luego
      `ServerExecuteInventoryItemHook(Controller, PickaxeInstance->GetItemEntry()->GetItemGuid());`
    - `(*Inventory)->Update();`
11. Si controlador NO es AI: `++GameState->GetPlayersLeft(); GameState->OnRep_PlayersLeft();`
12. Si es AFortPlayerControllerAthena: `GameMode->GetAlivePlayers().Add(FortPlayerControllerAthena);`
13. PlayerState->OnRep_PlayerName / OnRep_DeathInfo ...

Athena_PickTeamHook(GameMode, preferredTeam, Controller) (FortGameModeAthena.cpp:1082-1243):
- Lee PlayerState, bIsBot = PlayerState->IsBot(). Decide MaxSquadSize y TeamsNum desde playlist.
- Devuelve NextTeamIndex (int). Ademas rellena TeamsArrayContainer->TeamsArray[team] con weakplayerstate.
- static Current/NextTeamIndex/CurrentTeamMembers dan rotacion de teams; se resetean cuando cambia
  Globals::AmountOfListens.

Tareas de respawn/limpieza de plants del old system NO son necesarias para CustomBot Part 1.

## Bots::Tick (loop de actualizacion old system, bots.h:389-457) - REFERENCIA
- Iterar AllPlayerBotsToTick; skip si Controller o Pawn IsActorBeingDestroyed(); requiere
  PlayerState no nulo. En Warmup salto (JumpFn); en aircraft dar gracias (ServerThankBusDriver).
- Para CustomBot: implementaremos nuestro PROPIO loop de tick en el CustomBot entity/module
  (NO tocar Bots::Tick ni AllPlayerBotsToTick).

## Bots::SpawnBotsAtPlayerStarts (bots.h:350-387)
- Get PlayerStarts via UGameplayStatics::GetAllActorsOfClass(World, FortPlayerStartCreative/Warmup).
- SpawnBot(PlayerStart->GetTransform(), PlayerStart). (No lo usaremos tal cual; referencia.)
