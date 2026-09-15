# CustomBot - Research: Armas y Disparo (GAS)

## AFortWeapon (FortWeapon.h, FortWeapon.cpp)
- `GetWeaponData<T>()` -> `UFortWeaponItemDefinition*` (offset "WeaponData")
- `GetItemEntryGuid()` -> FGuid& (offset "ItemEntryGuid")
- `GetAmmoCount()` -> int& (offset "AmmoCount")
- `ServerReleaseWeaponAbilityHook` (passthrough, hook en dllmain.cpp:1397).
- `ServerReleaseWeaponAbility` UFunction: `/Script/FortniteGame.FortWeapon.ServerReleaseWeaponAbility`
- `OnPlayImpactFX` para corregir ammo tras disparar (CorrectLoadedAmmo).

## UFortWeaponItemDefinition (FortWeaponItemDefinition.h)
- `GetClipSize()`, `GetAmmoData()` -> UFortWorldItemDefinition*

## Para disparar (GAS): NO hay helper end-to-end en el repo
Existen estos building blocks:
- ASC: `PlayerStateAthena->GetAbilitySystemComponent()` (FortPlayerState.h:10)
- `MakeNewSpec(UClass* GameplayAbilityClass, UObject* SourceObject, bool bAlreadyIsDefault=false)`
  -> `FGameplayAbilitySpec*` (GameplayAbilitySpec.h:67)
- `ASC->GiveAbilityEasy(UClass*, UObject*=nullptr, bool bDoNotRegive=true)` -> handle
  (AbilitySystemComponent.h:90; impl AbilitySystemComponent_Abilities.cpp:198)
- `FGameplayAbilitySpec* MakeNewSpec(...)` + `GiveAbilityOriginal(ASC, &Handle, __int64(Spec))`
- `unsigned int* GiveAbilityAndActivateOnce(UAbilitySystemComponent*, int* outHandle, __int64 Spec, FGameplayEventData*)`
  - Signature: FortPlayerController.cpp:1205; invocacion ejemplo FortPlayerController.cpp:1209
  - Addresses::GiveAbilityAndActivateOnce (finder.h:1777, addresses.h:33)
- `InternalTryActivateAbilityOriginal(ASC, Handle, PadHex10, UObject** Out, void*, FGameplayEventData*)`
  y version ...Original2 (PadHex18) - AbilitySystemComponent.h:53-54
- `FindAbilitySpecFromHandle`, `GetActivatableAbilities`, `LoopSpecs` (para enumerar specs)
- FFortItemEntry::GetGameplayAbilitySpecHandle() (per-item cached, se pone a -1 en repo)

## Recomendación para disparo del bot
La forma mas robusta y "como un jugador" es:
1. Equipar el arma (EquipWeaponDefinition) -> dispara la logica nativa que concede la
   weapon ability y la activa. En el juego real, al equipar el arma el paquete ya crea la
   ability del arma.
2. Para disparar repetidamente (semi/full-auto), invocar el pipeline GAS: encontrar la
   spec de la weapon ability del item actual y activarla vía InternalTryActivateAbility
   o GiveAbilityAndActivateOnce.
3. Municion: WorldInventory->CorrectLoadedAmmo(ItemEntryGuid, AmmoCount) tras consumir
   (patron ya usado en FortPawn.cpp NetMulticast hook y FortWeapon.cpp OnPlayImpactFX).
- Recarga: cambiar Count del item (ammo) / no hay helper dedicado; se puede invocar
  UFunction nativa de recarga si existe o modificar ammo via FindReplicatedEntry->GetLoadedAmmo.

## NOTA IMPORTANTE
El repo NO tiene un helper de disparo. El disparo real ocurre en el motor via la weapon
ability (GAS) de la clase de arma. Para Parte 1 basta con proporcionar:
- Equipar arma (real, ya existe EquipWeaponDefinition / ServerExecuteInventoryItemHook)
- Disparar: invocar GiveAbilityAndActivateOnce o InternalTryActivateAbility sobre la
  weapon ability del arma equipada (esto consume municion y aplica daño real).
- Recargar: restablecer la municion del item (loaded ammo / count) via inventario.
- Cambiar arma: ServerExecuteInventoryItemHook con el guid del nuevo item.
