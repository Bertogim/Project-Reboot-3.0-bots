# CustomBot - Research: Utilidades de Código (reboot.h, World, log, math)

## reboot.h (funciones/macros libres)
- `template<typename T=UObject> T* FindObject(const TCHAR* Name, UClass* Class=nullptr, UObject* Outer=nullptr)` -> StaticFindObject.
- `template<typename T=UObject> T* FindObject(const std::string& NameStr, ...)`
- `template<typename T=UObject> T* LoadObject(const TCHAR* Name, UClass* Class=T::StaticClass(), ...)`
- `GetEngine()`, `static UWorld* GetWorld()` (via GameViewport->World), `GetLocalPlayers()`, `GetLocalPlayer()`, `GetLocalPlayerController()`.
- `template<typename T, bool bCheckType=true> T* Cast(UObject* Object)` -> si IsA(StaticClass()).
- `GetFieldMask(void* Property, int additional=0)`, `ReadBitfield(Addr, FieldMask)`, `SetBitfield(Addr, FieldMask, NewVal)`.
- `template<typename T=UObject> std::vector<T*> GetAllObjectsOfClass(UClass* Class)`, `GetRandomObjectOfClass`.
- `FindOffsetStruct(StructName, MemberName, bWarnIfNotFound=true)`, `FindPropertyStruct(...)`.
- `CopyStruct(void* Dest, void* Src, size_t Size, UStruct* Struct=nullptr)`.
- `template<typename T=__int64> T* Alloc(size_t Size=..., bool bUseFMemoryRealloc=false)` (VirtualAlloc por defecto).
- Macros: `GET_PLAYLIST(GameState)`, `VALIDATEOFFSET(offset)`.
- Variables globales: `Fortnite_Version`, `Engine_Version` (globals/globals.h), `Globals::bInfiniteMaterials`, `Globals::bNoMCP`, `Globals::bCreative`, `Globals::bLateGame`, `Globals::bPrivateIPsAreOperator`, `StartingShield`, `NumToSubtractFromSquadId`, `AmountToSubtractIndex`.
- `extern inline int AmountOfRestarts`, `FRandomStream ReplicationRandStream`, etc.

## World.h (UWorld)
- `template<typename T=AActor> T*& GetGameMode()` (offset "AuthorityGameMode")
- `AGameState*& GetGameState()` (offset "GameState")
- `UGameInstance* GetOwningGameInstance()`, `FTimerManager& GetTimerManager()`
- `template<ActorType> ActorType* SpawnActor(UClass* Class, FTransform Transform=FTransform(), void* SpawnParameters=nullptr)`
- `template<ActorType> ActorType* SpawnActor(UClass* Class, FVector Location, FQuat Rotation=FQuat(), FVector Scale3D={1,1,1}, void* SpawnParameters=nullptr)`
- `void* CreateSpawnParameters(ESpawnActorCollisionHandlingMethod=Undefined, bool bDeferConstruction=false, UObject* Owner=nullptr)` (retorna puntero a struct stack-alloc; cuidado con lifetime - los SpawnActor de World lo usan inline).

## log.h
- `LOG_INFO(loggerName, ...)`, `LOG_WARN(...)`, `LOG_ERROR(...)`, `LOG_DEBUG(...)`, `LOG_FATAL(...)`
- Usan `std::format(__VA_ARGS__)` -> placeholders `{}`.
- Loggers registrados: LogTeams, LogMemory, LogFinder, LogInit, LogNet, LogDev, LogPlayer,
  LogLoot, LogMinigame, LogLoading, LogHook, LogAbilities, LogEvent, LogPlaylist, LogGame,
  LogAI, LogInteraction, LogCreative, LogZone, LogReplication, LogMutator, LogVehicles,
  LogUI, LogBots, LogCosmetics, LogMatchmaker, LogRebooting, LogObjectViewer, LogLateGame.
- Incluir: `#include "log.h"` (y reboot.h usualmente).

## Vectores/Matemática (descubierto por agente)
- FVector (Vector.h): X,Y,Z; operadores +,-,*(float),+=,-=, SizeSquared(); | = dot; CompareVectors.
  - SIN Size(), Normalize(), /, ==, cross. SIMPLIFICADO: usar FMath::Sqrt(v|v) e InvSqrt.
- FMath (UnrealMathUtility.h): Clamp(X,Min,Max) template, Square(A).
  - GenericPlatformMath.h: Min, Max, Abs, InvSqrt (1/sqrtf), Fmod, FloorToInt, RoundToInt,
    Fractional, Sin, Cos, Tan, Asin, Acos, Atan, Atan2 (RADIANES, minimax), Sqrt, Pow,
    Loge, Sinh, Lerp<T>(A,B,Alpha) (funciona en FVector y escalares, NO FRotator).
- FRotator (Rotator.h): Pitch,Yaw,Roll (float, o double si ABOVE_S20). Sin constructores explícitos
  (agregate init). Metodos: Quaternion() -> FQuat, Vector() -> FVector (forward/dir desde rot,
  en UnrealMath.cpp:107). Static: NormalizeAxis(float), ClampAxis(float).
- FQuat (Quat.h): X,Y,Z,W; Rotator() -> FRotator.
- FTransform (Transform.h): Rotation(FQuat), Translation(FVector), Scale3D(FVector) + padding.
- CONVERSIONES:
  - dist(a,b): `FVector Delta = B - A; float Dist = FMath::Sqrt(Delta | Delta);` o a->GetDistanceTo(b).
  - normalize(v): `float Len = FMath::Sqrt(Dir|Dir); FVector N = Dir * (1.0f/Len);` o `* FMath::InvSqrt(Dir|Dir)`.
  - look-at(direccion dir) (NO existe MakeRotFromX/Conv_VectorToRotator):
    ```
    constexpr float RAD_TO_DEG = 180.0f / 3.1415926535897932f;
    FRotator LookAt;
    LookAt.Yaw   = FMath::Atan2(Dir.Y, Dir.X) * RAD_TO_DEG;
    LookAt.Pitch = FMath::Atan2(Dir.Z, FMath::Sqrt(Dir.X*Dir.X+Dir.Y*Dir.Y)) * RAD_TO_DEG;
    LookAt.Roll  = 0.f;
    ```
- Set rotation de un pawn: NO hay SetActorRotation. Usar `Pawn->TeleportTo(Loc, Rot)` (K2_TeleportTo).
  - Rotation para spawn: `SpawnActor(Class, Loc, rot.Quaternion(), Scale)`.
- FRotator aritmetica: NO hay operadores; hacer por componente (rot.Pitch += x; rot.Yaw += y;).
- Forward/Right/Up del actor: GetActorForwardVector()/GetActorRightVector()/GetActorUpVector()
  (ProcessEvent en Actor.cpp:119-144).

## Actor.h / Actor.cpp
- GetActorLocation() (K2_GetActorLocation), GetActorRotation() (K2_GetActorRotation), GetActorScale3D(),
  GetActorForwardVector/RightVector/UpVector, GetTransform(), GetOwner(), K2_DestroyActor(),
  GetDistanceTo(AActor*) -> float, TeleportTo(FVector, FRotator) -> bool, SetOwner(AActor*),
  ForceNetUpdate(), SetCanBeDamaged(bool), CanBeDamaged(), IsActorBeingDestroyed(), HasAuthority()
  (Role==3), GetClosestActor(UClass*, DistMax, pred), GetActorEyesViewPoint(FVector*,FRotator*) const,
  GetComponentByClass(UClass*), AddComponentByClass(UClass*), GetNetDormancy/SetNetDormancy,
  FlushNetDormancy(), GetNetUpdateFrequency(), GetMinNetUpdateFrequency().
- NO: SetActorLocation, SetActorRotation (no existen; usar TeleportTo).

## Controller.h / PlayerController.h
- AController: GetViewTarget(), Possess(APawn*), UnPossess(), GetStateName(), GetPawn() (offset "Pawn"),
  GetPlayerState() (offset "PlayerState").
- APlayerController: GetCheatManager(), GetNetConnection(), SetPlayerIsWaiting(bool), IsPlayerWaiting(),
  ServerChangeName(FString), SpawnCheatManager(UClass*), GetControlRotation() -> FRotator
  (/Script/Engine.Controller.GetControlRotation), ServerRestartPlayer().
- NOTA: No hay wrappers AddMovementInput/AddPitchInput/AddYawInput/SetControlRotation en el repo.

## Pawn.h / FortPawn.h
- APawn: GetPlayerState(), GetController() (offset "Controller"), GetAIControllerClass().
- AFortPawn: EquipWeaponDefinition(UFortWeaponItemDefinition*, FGuid) -> AFortWeapon*
  (/Script/FortniteGame.FortPawn.EquipWeaponDefinition; structs de params segun version),
  PickUpActor(AActor*, UFortDecoItemDefinition*) -> bool, GetCurrentWeapon() (offset "CurrentWeapon"),
  IsDBNO(), SetDBNO(bool), SetHasPlayedDying(bool), OnRep_IsDBNO(), GetShield(), GetHealth(),
  SetHealth(float), SetMaxHealth(float), SetShield(float), SetMaxShield(float).

## FortPlayerPawn / FortPlayerPawnAthena
- AFortPlayerPawn: GetCosmeticLoadout(), ServerChoosePart(EFortCustomPartType, UObject*),
  ForceLaunchPlayerZipline() (CharacterMovement->Velocity + LaunchCharacter), ServerOnExitVehicle,
  GetVehicle(), GetVehicleWeaponDefinition(Vehicle).
- Recogida real de pickup: ServerHandlePickupHook(Pawn, AFortPickup*, InFlyTime, InStartDirection, bPlayPickupSound):
  + comprueba Pickup->Get<bool>(bPickedUpOffset); add a Pawn->Get<TArray<AFortPickup*>>(IncomingPickupsOffset);
  + PickupLocationData: PickupTarget=Pawn, FlyTime=0.40, ItemOwner=Pawn, PickupGuid=weapon guid;
  + OnRep_PickupLocationData + OnRep_bPickedUp.
  + ServerHandlePickupWithRequestedSwapHook (con swap). ServerHandlePickupInfoHook.
- AFortPlayerPawnAthena: GetDBNORevivalStacking().
- Jump: ProcessEvent(/Script/Engine.Character.Jump) (ej. bots.h:440).
- Launch: ProcessEvent(/Script/Engine.Character.LaunchCharacter) params {FVector LaunchVelocity, bool bXYOverride, bool bZOverride}.

## FGuid
- Destructura: constructor FGuid(), FGuid(-1,-1,-1,-1) se usa, FGuid(...). Default FGuid{}. Ver FortItem.h.
