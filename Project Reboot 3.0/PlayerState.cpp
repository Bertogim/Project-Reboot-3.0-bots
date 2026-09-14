#include "PlayerState.h"

#include "reboot.h"

FString& APlayerState::GetSavedNetworkAddress()
{
	static auto SavedNetworkAddressOffset = GetOffset("SavedNetworkAddress");
	return Get<FString>(SavedNetworkAddressOffset);
}

FString APlayerState::GetPlayerName()
{
	// En v3.5 el nombre vive en PlayerNamePrivate (no en PlayerName), y leerlo
	// via ProcessEvent(GetPlayerName) devuelve un FString con puntero colgante
	// (nombres garbled). Leer el offset directo es lo que ya usa
	// FortServerBotManagerAthena para escribir el nombre de los bots.
	static auto PlayerNamePrivateOffset = GetOffset("PlayerNamePrivate", false);

	if (PlayerNamePrivateOffset != -1)
		return Get<FString>(PlayerNamePrivateOffset);

	static auto PlayerNameOffset = GetOffset("PlayerName");
	return Get<FString>(PlayerNameOffset);
}

int& APlayerState::GetPlayerID()
{
	static auto PlayerIDOffset = FindOffsetStruct("/Script/Engine.PlayerState", "PlayerID", false);

	if (PlayerIDOffset == -1)
	{
		static auto PlayerIdOffset = FindOffsetStruct("/Script/Engine.PlayerState", "PlayerId", false);
		return Get<int>(PlayerIdOffset);
	}

	return Get<int>(PlayerIDOffset);
}

bool APlayerState::IsBot()
{
	static auto bIsABotOffset = GetOffset("bIsABot");
	static auto bIsABotFieldMask = GetFieldMask(GetProperty("bIsABot"));
	return ReadBitfieldValue(bIsABotOffset, bIsABotFieldMask);
}

void APlayerState::SetIsBot(bool NewValue)
{
	static auto bIsABotOffset = GetOffset("bIsABot");
	static auto bIsABotFieldMask = GetFieldMask(GetProperty("bIsABot"));
	return SetBitfieldValue(bIsABotOffset, bIsABotFieldMask, NewValue);
}

void APlayerState::OnRep_PlayerName()
{
	static auto OnRep_PlayerNameFn = FindObject<UFunction>("/Script/Engine.PlayerState.OnRep_PlayerName");
	this->ProcessEvent(OnRep_PlayerNameFn);
}