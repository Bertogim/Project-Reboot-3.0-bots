# CustomBot - Research: Sistema del Jugador Real (Player)

## Clases clave
- `APlayerController` (PlayerController.h): GetCheatManager, GetNetConnection,
  SetPlayerIsWaiting, IsPlayerWaiting, ServerChangeName, SpawnCheatManager,
  GetControlRotation() (via `/Script/Engine.Controller.GetControlRotation`),
  ServerRestartPlayer.
- `AController` (Controller.h): GetViewTarget, Possess, UnPossess, GetStateName,
  GetPawn() (offset "Pawn"), GetPlayerState() (offset "PlayerState").
- `APawn` (Pawn.h): GetPlayerState, GetController (offset "Controller"), GetAIControllerClass.
- `AActor` (Actor.h): GetActorLocation, GetActorRotation (`/Script/Engine.Actor.K2_GetActorRotation`),
  TeleportTo, K2_DestroyActor, SetOwner, ForceNetUpdate, GetWorld, etc.

## AFortPlayerController (FortPlayerController.h)
- `GetWorldInventory()` -> `AFortInventory*&` (offset "WorldInventory")
- `GetMyFortPawn()` -> `AFortPawn*&` (offset "MyFortPawn")
- `GetCosmeticLoadout()` -> `FFortAthenaLoadout*` (offsets CosmeticLoadoutPC/CustomizationLoadout)
- `AddPickaxeToInventory()` -> `UFortItem*` (añade pickaxe WID_Harvest_Pickaxe_Athena_C_T01)
- `ShouldTryPickupSwap()`, `HasTryPickupSwap()`
- `ClientEquipItem(FGuid, bool)` (via `/Script/...FortPlayerControllerAthena.ClientEquipItem`)
- `ClientForceCancelBuildingTool()`
- `DoesBuildFree()` (Globals::bInfiniteMaterials || bitfield bBuildFree)
- `DropAllItems`, `ApplyCosmeticLoadout()`
- Enums: EFortResourceType en BuildingSMActor.h (Wood=0, Stone=1, Metal=2), EInteractionBeingAttempted.
- Hooks: ServerExecuteInventoryItemHook, ServerAttemptInteractHook, ServerCreateBuildingActorHook,
  ServerAttemptInventoryDropHook, ClientOnPawnDiedHook, etc.

## AFortPlayerControllerAthena (FortPlayerControllerAthena.h)
- `GetPlayerStateAthena()`, `GetMarkerComponent()`, `GetResurrectionComponent()`,
  `IsInGhostMode()`, `IsMarkedAlive()`, `SpectateOnDeath()`, `RespawnPlayerAfterDeath(bool)`.
- Funciones libres: `ApplyHID(Pawn, HeroDefinition, bUseServerChoosePart)`,
  `ApplyCID(Pawn, CID, ...)` -> cosmética.
- `StaticClass()` = `/Script/FortniteGame.FortPlayerControllerAthena`

## AFortPawn (FortPawn.h, FortPawn.cpp)
- `EquipWeaponDefinition(UFortWeaponItemDefinition*, FGuid)` -> `AFortWeapon*` (via
  `/Script/FortniteGame.FortPawn.EquipWeaponDefinition`; versiones de params 17/16/15).
- `PickUpActor(AActor*, UFortDecoItemDefinition*)` -> bool (para decos/trampas).
- `GetCurrentWeapon()` -> `AFortWeapon*&` (offset "CurrentWeapon")
- `IsDBNO()`, `SetDBNO(bool)`, `SetHasPlayedDying(bool)`, `OnRep_IsDBNO()`
- `GetShield()`, `GetHealth()` (via UFunction), `SetHealth(float)`, `SetMaxHealth(float)`,
  `SetShield(float)`, `SetMaxShield(float)`.
- `OnRep_IsDBNO`, `NetMulticast_Athena_BatchedDamageCuesHook`, `MovingEmoteStoppedHook`.

## AFortPlayerPawn (FortPlayerPawn.h, .cpp)
- `GetCosmeticLoadout()`, `ServerChoosePart(EFortCustomPartType, UObject*)`,
  `ForceLaunchPlayerZipline()` (usa CharacterMovement -> LaunchCharacter),
  `ServerOnExitVehicle`, `GetVehicle`, `GetVehicleWeaponDefinition`.
- `ServerHandlePickupHook(AFortPlayerPawn*, AFortPickup*, float, FVector, bool)`:
  Lógica real de recogida de pickup (marca bPickedUp, PickupLocationData, OnRep).
- `ServerHandlePickupWithRequestedSwapHook`, `ServerHandlePickupInfoHook`.
- `ServerReviveFromDBNOHook`.

## AFortPlayerPawnAthena (FortPlayerPawnAthena.h)
- `GetDBNORevivalStacking()`, `Athena.OnCapsuleBeginOverlapHook`.

## Cómo se obtiene el ASC (AbilitySystemComponent)
- `AFortPlayerState::GetAbilitySystemComponent()` -> `UAbilitySystemComponent*&`
  (offset "AbilitySystemComponent") en FortPlayerState.h:10-14.
- `ASCs` para jugador: `PlayerStateAthena->GetAbilitySystemComponent()`.

## Movimiento del jugador real
- No hay clase Character.wrapper en el repo (no existe Character.h).
- Movimiento vía CharacterMovement component en el Pawn (offset "CharacterMovement"),
  Velocity en ese component.
- Salto: bots antiguos usan `ProcessEvent(/Script/Engine.Character.Jump)`.
- Launch: `/Script/Engine.Character.LaunchCharacter` params {FVector LaunchVelocity, bool bXYOverride, bool bZOverride}.
- Rotación control: `GetControlRotation()`. ViewPoint: `GetPlayerViewPointHook`.
- NOTA: No hay AddMovementInput/AddPitchInput wrappers ni ACharacter wrapper en el repo.
  Para movimiento orientado a input habrá que invocar UFunctions nativas del Character
  (AddMovementInput) o sobreescribir /cambiar directamente el CharacterMovement->Velocity.

## Danio a estructuras / recursos
- `ABuildingActor::OnDamageServerHook` (BuildingActor.cpp:14) intercepta el daño a
  ABuildingSMActor; requiere weapon melee (FortWeaponMeleeItemDefinition) para otorgar
  materiales via `WorldInventory->AddItem(K2_GetResourceItemDefinition(ResourceType), ...)`.
  El daño/destruccion real lo hace el codigo vanilla (OnDamageServerOriginal).
