// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SocialStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/Function.h"
#include "SocialSubsystem.generated.h"

class IWebSocket;
class FJsonObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFriendsUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPartiesUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCurrentPartyChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSocialConnectionChanged, bool, bConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLocalUserUpdated, FSocialUser, LocalUser);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSocialRequestFailed, FString, Operation, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPartyActionCompleted, bool, bSuccess, int32, PartyID, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMockPresenceToggled, bool, bSuccess, int32, UserID, FString, ErrorMessage);

UCLASS(BlueprintType, Blueprintable)
class ONLINESYSTEMS_API USocialSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Social|Config")
	void SetServerURLs(const FString& InBaseHttpURL, const FString& InWebSocketURL);

	UFUNCTION(BlueprintCallable, Category = "Social")
	void RefreshAll();
	
	UFUNCTION(BlueprintCallable, Category = "Social")
	void ConnectWebSocket();
	
	UFUNCTION(BlueprintCallable, Category = "Social")
	void DisconnectWebSocket();
	
	UFUNCTION(BlueprintPure, Category = "Social")
	bool IsWebSocketConnected() const;
	
	UFUNCTION(BlueprintCallable, Category = "Social")
	void RequestLocalUser();

	UFUNCTION(BlueprintCallable, Category = "Social")
	void RequestFriends();
	
	UFUNCTION(BlueprintCallable, Category = "Social")
	void RequestParties();

	UFUNCTION(BlueprintCallable, Category = "Social|Actions")
	void JoinParty(int32 PartyID);

	UFUNCTION(BlueprintCallable, Category = "Social|Actions")
	void LeaveParty(int32 PartyID);

	UFUNCTION(BlueprintCallable, Category = "Social|Actions")
	void LeaveCurrentParty();

	UFUNCTION(BlueprintCallable, Category = "Social|Debug")
	void ToggleMockFriendPresence(int32 UserID);
	
	UFUNCTION(BlueprintPure, Category = "Social")
	const TArray<FSocialFriend>& GetFriends() const;
	
	UFUNCTION(BlueprintPure, Category = "Social")
	const TArray<FSocialParty>& GetParties() const;

	UFUNCTION(BlueprintPure, Category = "Social")
	FSocialUser GetLocalUser() const;

	UFUNCTION(BlueprintPure, Category = "Social")
	int32 GetCurrentPartyID() const;

	UFUNCTION(BlueprintPure, Category = "Social")
	bool IsInParty() const;

	UFUNCTION(BlueprintPure, Category = "Social")
	bool GetFriendByUserID(int32 UserID, FSocialFriend& OutFriend) const;

	UFUNCTION(BlueprintPure, Category = "Social")
	bool GetPartyByID(int32 PartyID, FSocialParty& OutParty) const;

	UFUNCTION(BlueprintPure, Category = "Social")
	bool GetCurrentParty(FSocialParty& OutParty) const;

	UFUNCTION(BlueprintPure, Category = "Social")
	static FString GetFriendStatusText(EFriendStatus Status);
	
	UPROPERTY(BlueprintAssignable, Category = "Social")
	FOnFriendsUpdated OnFriendsUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Social")
	FOnPartiesUpdated OnPartiesUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Social")
	FOnCurrentPartyChanged OnCurrentPartyChanged;

	UPROPERTY(BlueprintAssignable, Category = "Social")
	FOnSocialConnectionChanged OnConnectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "Social")
	FOnLocalUserUpdated OnLocalUserUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Social")
	FOnSocialRequestFailed OnRequestFailed;

	UPROPERTY(BlueprintAssignable, Category = "Social")
	FOnPartyActionCompleted OnPartyActionCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Social")
	FOnMockPresenceToggled OnMockPresenceToggled;
	
private:
	void HandleWebSocketConnected();
	void HandleWebSocketConnectionError(const FString& Error);
	void HandleWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void HandleWebSocketMessage(const FString& Message);

	void SendGetRequest(const FString& OperationName, const FString& Endpoint, TFunction<void(const FString&)> OnSuccess);
	void SendPostRequest(const FString& OperationName, const FString& Endpoint, TFunction<void(const FString&)> OnSuccess, TFunction<void(const FString&)> OnFailure);
	void BroadcastRequestFailed(const FString& OperationName, const FString& ErrorMessage);
	
	bool ParseLocalUser(const TSharedPtr<FJsonObject>& JsonObject, FSocialUser& OutUser) const;
	bool ParseFriend(const TSharedPtr<FJsonObject>& JsonObject, FSocialFriend& OutFriend) const;
	bool ParsePartyMember(const TSharedPtr<FJsonObject>& JsonObject, FPartyMember& OutMember) const;
	bool ParseParty(const TSharedPtr<FJsonObject>& JsonObject, FSocialParty& OutParty) const;
	bool ParseFriendsArray(const FString& JsonString, TArray<FSocialFriend>& OutFriends) const;
	bool ParsePartiesArray(const FString& JsonString, TArray<FSocialParty>& OutParties) const;
	bool ParseJsonObjectString(const FString& JsonString, TSharedPtr<FJsonObject>& OutObject) const;

	void ApplyLocalUserUpdate(const FSocialUser& User);
	void ApplyFriendUpdate(const FSocialFriend& Friend);
	void ApplyPartyUpdate(const FSocialParty& Party);
	void SortFriends();
	void SortParties();
	static EFriendStatus ParseFriendStatus(const FString& Status);

private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social|Config", meta = (AllowPrivateAccess = "true"))
	FString BaseHttpURL = TEXT("http://localhost:8080");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social|Config", meta = (AllowPrivateAccess = "true"))
	FString WebSocketURL = TEXT("ws://localhost:8080/ws");

	TArray<FSocialFriend> Friends;
	TArray<FSocialParty> Parties;
	TSharedPtr<IWebSocket> WebSocket;
	FSocialUser LocalUser;
};
