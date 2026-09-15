# CustomBot - Research: Inventario, Materiales, Loot y Pickup

## AFortInventory (FortInventory.h, FortInventory.cpp)
- `GetItemList()` -> `FFortItemList&` (offset "Inventory")
- `GetInventoryType()` -> `EFortInventoryType&` (offset "InventoryType", World/Account/Outpost)
- `HandleInventoryLocalUpdate()` (via `/Script/FortniteGame.FortInventory.HandleInventoryLocalUpdate`)
- `Update(bool bMarkArrayDirty = true)` -> HandleInventoryLocalUpdate + MarkArrayDirty
- `AddItem(FFortItemEntry*, bool* bShouldUpdate, ...)`
- `AddItem(UFortItemDefinition*, bool* bShouldUpdate, int Count=1, int LoadedAmmo=-1, bool bShowItemToast=false)`
- `RemoveItem(const FGuid&, bool* bShouldUpdate, int Count, bool bForceRemoval=false, bool bIgnoreVariables=false)`
- `SwapItem`, `ModifyCount`
- `GetPickaxeInstance()`, `FindItemInstance(UFortItemDefinition*)`, `FindItemInstance(FGuid)`,
  `FindReplicatedEntry(FGuid)`, `CorrectLoadedAmmo(FGuid, int)`

## FFortItemList
- `GetItemInstances()` -> `TArray<UFortItem*>` (offset "ItemInstances")
- `GetReplicatedEntries()` -> `TArray<FFortItemEntry>` (offset "ReplicatedEntries")

## FFortItemEntry (FortItem.h)
- `GetItemDefinition()`, `GetItemGuid()`, `GetCount()`, `GetLoadedAmmo()`
- `GetGameplayAbilitySpecHandle()` (offset "GameplayAbilitySpecHandle"; repo lo pone a -1)

## Recursos / Materiales
- `EFortResourceType` (BuildingSMActor.h): Wood=0, Stone=1, Metal=2, Permanite=3, None=4.
  NOTA: El usuario habla de "Wood/Brick/Metal" pero el repo usa Wood/Stone/Metal (Stone=ladrillo).
- `UFortKismetLibrary::K2_GetResourceItemDefinition(EFortResourceType)` -> `UFortResourceItemDefinition`
  - Wood  -> /Game/Items/ResourcePickups/WoodItemData.WoodItemData
  - Stone -> /Game/Items/ResourcePickups/StoneItemData.StoneItemData  (BRICK/ladrillo)
  - Metal -> /Game/Items/ResourcePickups/MetalItemData.MetalItemData
- `K2_GiveBuildingResourceHook` en FortKismetLibrary.cpp:418 (otorga recurso por tipo).

## Loot
- `AFortPickup` (FortPickup.h): representa item en el suelo.
  - `GetPrimaryPickupItemEntry()` -> `FFortItemEntry*` (offset "PrimaryPickupItemEntry")
  - `GetPickupLocationData()` -> `FFortPickupLocationData*`
  - `SpawnPickup(PickupCreateData&)`, `SpawnPickup(FFortItemEntry*, ...)`
  - `OnRep_PrimaryPickupItemEntry`, `OnRep_PickupLocationData`, `TossPickup`, `SpawnMovementComponent`
  - `bPickedUp` bitfield (offset "bPickedUp") -> se usa para comprobar/evitar doble recogida
- `AFortPickup::GetPrimaryPickupItemEntry()->GetItemDefinition()` -> definir tipo de loot.
- `FFortPickupLocationData`: GetPickupTarget (AFortPawn*), GetFlyTime, GetPickupGuid, etc.

## Recogida de items (como un jugador)
- `AFortPlayerPawn::ServerHandlePickupHook(AFortPlayerPawn* Pawn, AFortPickup* Pickup, float InFlyTime, FVector InStartDirection, bool bPlayPickupSound)`
  - Comprueba `Pickup->Get<bool>(bPickedUpOffset)`; si ya recogido, return.
  - Añade a `Pawn->Get<TArray<AFortPickup*>>(IncomingPickupsOffset).Add(Pickup)`.
  - Configura PickupLocationData: PickupTarget=Pawn, FlyTime=0.40, ItemOwner=Pawn, PickupGuid=weapon guid.
  - Procesa OnRep_PickupLocationData y OnRep_bPickedUp.
- Alternativa: `ServerHandlePickupWithRequestedSwapHook` (pickup con swap implicito).
- comandos/jugador real recogen via overlap (OnCapsuleBeginOverlap).

## Interacción (ServerAttemptInteractHook - FortPlayerController.cpp:422)
- Struct determinada por si usa componente (FortControllerComponent_Interaction).
- `ServerAttemptInteractOriginal(Context, Stack)` -> fallback/interaccion vanilla.
- BuildingContainer (arma): SpawnLoot, marca bAlreadySearched, BounceContainer.
- Para el bot: `ServerAttemptInteract()` de AController no existe en repo; la interaccion
  real se hace via `ProcessEvent` de la UFunction del PlayerController:
  `/Script/FortniteGame.FortPlayerController.ServerAttemptInteract` (o la variante componente).
  Alternativa: invocar directamente las funciones de container (SpawnLoot).

## Clasificación de items (FortInventory.h IsPrimaryQuickbar)
- Clases utiles para clasificar loot:
  - `/Script/FortniteGame.FortWeaponMeleeItemDefinition`
  - `/Script/FortniteGame.FortEditToolItemDefinition`
  - `/Script/FortniteGame.FortBuildingItemDefinition`
  - `/Script/FortniteGame.FortAmmoItemDefinition`
  - `/Script/FortniteGame.FortResourceItemDefinition`
  - `/Script/FortniteGame.FortTrapItemDefinition`
  - `/Script/FortniteGame.FortGadgetItemDefinition`
  - `/Script/FortniteGame.FortDecoItemDefinition`
