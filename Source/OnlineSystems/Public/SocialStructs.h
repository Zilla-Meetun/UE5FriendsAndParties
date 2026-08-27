#pragma once

#include "CoreMinimal.h"
#include "SocialStructs.generated.h"

UENUM(BlueprintType)
enum class EFriendStatus : uint8
{
	Offline = 0 UMETA(DisplayName = "Offline"),
	Online = 1 UMETA(DisplayName = "Online"),
	InGame = 2 UMETA(DisplayName = "In Game")
};
USTRUCT(BlueprintType)
struct FSocialFriend
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	int32 UserID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	EFriendStatus Status = EFriendStatus::Offline;
	
	UPROPERTY(BlueprintReadOnly, Category = "Social")
	int32 PartyID = 0;
	
};

USTRUCT(BlueprintType)
struct FPartyMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	int32 UserID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	FString DisplayName;
};

USTRUCT(BlueprintType)
struct FSocialParty
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	int32 PartyID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	int32 LeaderUserID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	FString LeaderDisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	TArray<FPartyMember> Members;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	int32 MaxSize = 0;
};

USTRUCT(BlueprintType)
struct FSocialUser
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	int32 UserID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Social")
	int32 PartyID = 0;
};
