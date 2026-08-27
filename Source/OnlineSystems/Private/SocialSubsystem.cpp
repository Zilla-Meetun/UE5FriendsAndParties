#include "SocialSubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WebSocketsModule.h"

namespace
{
FString BuildURL(const FString& BaseURL, const FString& Endpoint)
{
	FString NormalizedBaseURL = BaseURL;
	while (NormalizedBaseURL.EndsWith(TEXT("/")))
	{
		NormalizedBaseURL.LeftChopInline(1);
	}

	if (Endpoint.StartsWith(TEXT("/")))
	{
		return NormalizedBaseURL + Endpoint;
	}

	return NormalizedBaseURL + TEXT("/") + Endpoint;
}

FString MakeHttpErrorMessage(const FHttpResponsePtr& Response, bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		return TEXT("Request failed before a response was received. Is the mock server running?");
	}

	if (!Response.IsValid())
	{
		return TEXT("The mock server did not return a valid response.");
	}

	const int32 ResponseCode = Response->GetResponseCode();
	const FString ResponseBody = Response->GetContentAsString();
	if (ResponseBody.IsEmpty())
	{
		return FString::Printf(TEXT("HTTP %d"), ResponseCode);
	}

	return FString::Printf(TEXT("HTTP %d: %s"), ResponseCode, *ResponseBody);
}

bool TryGetRequiredString(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FString& OutValue)
{
	return JsonObject.IsValid() && JsonObject->TryGetStringField(FieldName, OutValue) && !OutValue.IsEmpty();
}

bool TryGetRequiredInt(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, int32& OutValue)
{
	return JsonObject.IsValid() && JsonObject->TryGetNumberField(FieldName, OutValue);
}

void TryGetOptionalInt(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, int32& OutValue)
{
	if (!JsonObject.IsValid() || !JsonObject->TryGetNumberField(FieldName, OutValue))
	{
		OutValue = 0;
	}
}

int32 GetStatusSortRank(EFriendStatus Status)
{
	switch (Status)
	{
	case EFriendStatus::InGame:
		return 0;
	case EFriendStatus::Online:
		return 1;
	case EFriendStatus::Offline:
	default:
		return 2;
	}
}
}

void USocialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("SocialSubsystem initialized"));

	ConnectWebSocket();
}

void USocialSubsystem::Deinitialize()
{
	DisconnectWebSocket();
	Super::Deinitialize();
}

void USocialSubsystem::SetServerURLs(const FString& InBaseHttpURL, const FString& InWebSocketURL)
{
	BaseHttpURL = InBaseHttpURL;
	WebSocketURL = InWebSocketURL;

	while (BaseHttpURL.EndsWith(TEXT("/")))
	{
		BaseHttpURL.LeftChopInline(1);
	}
}

void USocialSubsystem::RefreshAll()
{
	RequestLocalUser();
	RequestFriends();
	RequestParties();
}

void USocialSubsystem::ConnectWebSocket()
{
	if (WebSocket.IsValid())
	{
		if (WebSocket->IsConnected())
		{
			return;
		}

		WebSocket->Close();
		WebSocket.Reset();
	}

	WebSocket = FWebSocketsModule::Get().CreateWebSocket(WebSocketURL);
	WebSocket->OnConnected().AddUObject(this, &USocialSubsystem::HandleWebSocketConnected);
	WebSocket->OnConnectionError().AddUObject(this, &USocialSubsystem::HandleWebSocketConnectionError);
	WebSocket->OnClosed().AddUObject(this, &USocialSubsystem::HandleWebSocketClosed);
	WebSocket->OnMessage().AddUObject(this, &USocialSubsystem::HandleWebSocketMessage);
	WebSocket->Connect();
}

void USocialSubsystem::DisconnectWebSocket()
{
	if (!WebSocket.IsValid())
	{
		return;
	}

	if (WebSocket->IsConnected())
	{
		WebSocket->Close();
	}

	WebSocket.Reset();
	OnConnectionChanged.Broadcast(false);
}

bool USocialSubsystem::IsWebSocketConnected() const
{
	return WebSocket.IsValid() && WebSocket->IsConnected();
}

void USocialSubsystem::RequestLocalUser()
{
	SendGetRequest(TEXT("RequestLocalUser"), TEXT("/api/me"), [this](const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> JsonObject;
		FSocialUser User;
		if (!ParseJsonObjectString(ResponseBody, JsonObject) || !ParseLocalUser(JsonObject, User))
		{
			BroadcastRequestFailed(TEXT("RequestLocalUser"), TEXT("Could not parse the local user response."));
			return;
		}

		ApplyLocalUserUpdate(User);
	});
}

void USocialSubsystem::RequestFriends()
{
	SendGetRequest(TEXT("RequestFriends"), TEXT("/api/friends"), [this](const FString& ResponseBody)
	{
		TArray<FSocialFriend> ParsedFriends;
		if (!ParseFriendsArray(ResponseBody, ParsedFriends))
		{
			BroadcastRequestFailed(TEXT("RequestFriends"), TEXT("Could not parse the friends response."));
			return;
		}

		Friends = MoveTemp(ParsedFriends);
		SortFriends();
		OnFriendsUpdated.Broadcast();
	});
}

void USocialSubsystem::RequestParties()
{
	SendGetRequest(TEXT("RequestParties"), TEXT("/api/parties"), [this](const FString& ResponseBody)
	{
		TArray<FSocialParty> ParsedParties;
		if (!ParsePartiesArray(ResponseBody, ParsedParties))
		{
			BroadcastRequestFailed(TEXT("RequestParties"), TEXT("Could not parse the parties response."));
			return;
		}

		Parties = MoveTemp(ParsedParties);
		SortParties();
		OnPartiesUpdated.Broadcast();
	});
}

void USocialSubsystem::JoinParty(int32 PartyID)
{
	SendPostRequest(TEXT("JoinParty"), FString::Printf(TEXT("/api/parties/%d/join"), PartyID), [this, PartyID](const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> JsonObject;
		FSocialParty Party;
		if (!ParseJsonObjectString(ResponseBody, JsonObject) || !ParseParty(JsonObject, Party))
		{
			const FString ErrorMessage = TEXT("Could not parse the join-party response.");
			BroadcastRequestFailed(TEXT("JoinParty"), ErrorMessage);
			OnPartyActionCompleted.Broadcast(false, PartyID, ErrorMessage);
			return;
		}

		ApplyPartyUpdate(Party);
		if (LocalUser.PartyID != Party.PartyID)
		{
			LocalUser.PartyID = Party.PartyID;
			OnLocalUserUpdated.Broadcast(LocalUser);
			OnCurrentPartyChanged.Broadcast();
		}
		OnPartyActionCompleted.Broadcast(true, Party.PartyID, FString());
	},
	[this, PartyID](const FString& ErrorMessage)
	{
		OnPartyActionCompleted.Broadcast(false, PartyID, ErrorMessage);
	});
}

void USocialSubsystem::LeaveParty(int32 PartyID)
{
	SendPostRequest(TEXT("LeaveParty"), FString::Printf(TEXT("/api/parties/%d/leave"), PartyID), [this, PartyID](const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> JsonObject;
		FSocialParty Party;
		if (!ParseJsonObjectString(ResponseBody, JsonObject) || !ParseParty(JsonObject, Party))
		{
			const FString ErrorMessage = TEXT("Could not parse the leave-party response.");
			BroadcastRequestFailed(TEXT("LeaveParty"), ErrorMessage);
			OnPartyActionCompleted.Broadcast(false, PartyID, ErrorMessage);
			return;
		}

		ApplyPartyUpdate(Party);
		if (LocalUser.PartyID == PartyID)
		{
			LocalUser.PartyID = 0;
			OnLocalUserUpdated.Broadcast(LocalUser);
			OnCurrentPartyChanged.Broadcast();
		}

		OnPartyActionCompleted.Broadcast(true, PartyID, FString());
	},
	[this, PartyID](const FString& ErrorMessage)
	{
		OnPartyActionCompleted.Broadcast(false, PartyID, ErrorMessage);
	});
}

void USocialSubsystem::LeaveCurrentParty()
{
	if (LocalUser.PartyID == 0)
	{
		const FString ErrorMessage = TEXT("Local user is not currently in a party.");
		BroadcastRequestFailed(TEXT("LeaveCurrentParty"), ErrorMessage);
		OnPartyActionCompleted.Broadcast(false, 0, ErrorMessage);
		return;
	}

	LeaveParty(LocalUser.PartyID);
}

void USocialSubsystem::ToggleMockFriendPresence(int32 UserID)
{
	SendPostRequest(TEXT("ToggleMockFriendPresence"), FString::Printf(TEXT("/api/mock/friends/%d/toggle"), UserID), [this, UserID](const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> JsonObject;
		FSocialFriend Friend;
		if (!ParseJsonObjectString(ResponseBody, JsonObject) || !ParseFriend(JsonObject, Friend))
		{
			const FString ErrorMessage = TEXT("Could not parse the mock presence response.");
			BroadcastRequestFailed(TEXT("ToggleMockFriendPresence"), ErrorMessage);
			OnMockPresenceToggled.Broadcast(false, UserID, ErrorMessage);
			return;
		}

		ApplyFriendUpdate(Friend);
		OnMockPresenceToggled.Broadcast(true, Friend.UserID, FString());
	},
	[this, UserID](const FString& ErrorMessage)
	{
		OnMockPresenceToggled.Broadcast(false, UserID, ErrorMessage);
	});
}

const TArray<FSocialFriend>& USocialSubsystem::GetFriends() const
{
	return Friends;
}

const TArray<FSocialParty>& USocialSubsystem::GetParties() const
{
	return Parties;
}

FSocialUser USocialSubsystem::GetLocalUser() const
{
	return LocalUser;
}

int32 USocialSubsystem::GetCurrentPartyID() const
{
	return LocalUser.PartyID;
}

bool USocialSubsystem::IsInParty() const
{
	return LocalUser.PartyID != 0;
}

bool USocialSubsystem::GetFriendByUserID(int32 UserID, FSocialFriend& OutFriend) const
{
	for (const FSocialFriend& Friend : Friends)
	{
		if (Friend.UserID == UserID)
		{
			OutFriend = Friend;
			return true;
		}
	}

	return false;
}

bool USocialSubsystem::GetPartyByID(int32 PartyID, FSocialParty& OutParty) const
{
	for (const FSocialParty& Party : Parties)
	{
		if (Party.PartyID == PartyID)
		{
			OutParty = Party;
			return true;
		}
	}

	return false;
}

bool USocialSubsystem::GetCurrentParty(FSocialParty& OutParty) const
{
	if (LocalUser.PartyID == 0)
	{
		return false;
	}

	return GetPartyByID(LocalUser.PartyID, OutParty);
}

FString USocialSubsystem::GetFriendStatusText(EFriendStatus Status)
{
	switch (Status)
	{
	case EFriendStatus::Online:
		return TEXT("Online");
	case EFriendStatus::InGame:
		return TEXT("In Game");
	case EFriendStatus::Offline:
	default:
		return TEXT("Offline");
	}
}

void USocialSubsystem::HandleWebSocketConnected()
{
	UE_LOG(LogTemp, Log, TEXT("WebSocket connected"));
	OnConnectionChanged.Broadcast(true);
	RefreshAll();
}

void USocialSubsystem::HandleWebSocketConnectionError(const FString& Error)
{
	UE_LOG(LogTemp, Warning, TEXT("WebSocket connection error: %s"), *Error);
	OnConnectionChanged.Broadcast(false);
	BroadcastRequestFailed(TEXT("ConnectWebSocket"), Error);
}

void USocialSubsystem::HandleWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Log, TEXT("WebSocket closed. Code=%d Clean=%s Reason=%s"), StatusCode, bWasClean ? TEXT("true") : TEXT("false"), *Reason);
	WebSocket.Reset();
	OnConnectionChanged.Broadcast(false);
}

void USocialSubsystem::HandleWebSocketMessage(const FString& Message)
{
	UE_LOG(LogTemp, Verbose, TEXT("WebSocket message: %s"), *Message);

	TSharedPtr<FJsonObject> RootObject;
	if (!ParseJsonObjectString(Message, RootObject))
	{
		BroadcastRequestFailed(TEXT("WebSocket"), TEXT("Could not parse a WebSocket message."));
		return;
	}

	FString EventType;
	if (!TryGetRequiredString(RootObject, TEXT("type"), EventType))
	{
		BroadcastRequestFailed(TEXT("WebSocket"), TEXT("A WebSocket message did not contain an event type."));
		return;
	}

	const TSharedPtr<FJsonObject>* DataObjectPtr = nullptr;
	const TSharedPtr<FJsonObject> DataObject = RootObject->TryGetObjectField(TEXT("data"), DataObjectPtr) ? *DataObjectPtr : nullptr;

	if (EventType.Equals(TEXT("friend.presence_changed"), ESearchCase::IgnoreCase))
	{
		FSocialFriend Friend;
		if (!ParseFriend(DataObject, Friend))
		{
			BroadcastRequestFailed(TEXT("WebSocket"), TEXT("Could not parse a friend presence event."));
			return;
		}

		ApplyFriendUpdate(Friend);
		return;
	}

	if (EventType.Equals(TEXT("party.updated"), ESearchCase::IgnoreCase))
	{
		FSocialParty Party;
		if (!ParseParty(DataObject, Party))
		{
			BroadcastRequestFailed(TEXT("WebSocket"), TEXT("Could not parse a party update event."));
			return;
		}

		ApplyPartyUpdate(Party);
		return;
	}

	if (EventType.Equals(TEXT("self.party_changed"), ESearchCase::IgnoreCase))
	{
		int32 NewPartyID = 0;
		if (!TryGetRequiredInt(DataObject, TEXT("partyID"), NewPartyID))
		{
			BroadcastRequestFailed(TEXT("WebSocket"), TEXT("Could not parse a current-party event."));
			return;
		}

		if (LocalUser.PartyID != NewPartyID)
		{
			LocalUser.PartyID = NewPartyID;
			OnLocalUserUpdated.Broadcast(LocalUser);
			OnCurrentPartyChanged.Broadcast();
		}
		return;
	}

	UE_LOG(LogTemp, Verbose, TEXT("Ignoring unknown WebSocket event: %s"), *EventType);
}

void USocialSubsystem::SendGetRequest(const FString& OperationName, const FString& Endpoint, TFunction<void(const FString&)> OnSuccess)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("GET"));
	Request->SetURL(BuildURL(BaseHttpURL, Endpoint));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

	TWeakObjectPtr<USocialSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakThis, OperationName, OnSuccess](FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		USocialSubsystem* StrongThis = WeakThis.Get();
		if (!StrongThis)
		{
			return;
		}

		if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
		{
			StrongThis->BroadcastRequestFailed(OperationName, MakeHttpErrorMessage(Response, bWasSuccessful));
			return;
		}

		OnSuccess(Response->GetContentAsString());
	});

	Request->ProcessRequest();
}

void USocialSubsystem::SendPostRequest(const FString& OperationName, const FString& Endpoint, TFunction<void(const FString&)> OnSuccess, TFunction<void(const FString&)> OnFailure)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(BuildURL(BaseHttpURL, Endpoint));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

	TWeakObjectPtr<USocialSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakThis, OperationName, OnSuccess, OnFailure](FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		USocialSubsystem* StrongThis = WeakThis.Get();
		if (!StrongThis)
		{
			return;
		}

		if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
		{
			const FString ErrorMessage = MakeHttpErrorMessage(Response, bWasSuccessful);
			StrongThis->BroadcastRequestFailed(OperationName, ErrorMessage);
			OnFailure(ErrorMessage);
			return;
		}

		OnSuccess(Response->GetContentAsString());
	});

	Request->ProcessRequest();
}

void USocialSubsystem::BroadcastRequestFailed(const FString& OperationName, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT("%s failed: %s"), *OperationName, *ErrorMessage);
	OnRequestFailed.Broadcast(OperationName, ErrorMessage);
}

bool USocialSubsystem::ParseLocalUser(const TSharedPtr<FJsonObject>& JsonObject, FSocialUser& OutUser) const
{
	if (!TryGetRequiredInt(JsonObject, TEXT("userID"), OutUser.UserID))
	{
		return false;
	}

	if (!TryGetRequiredString(JsonObject, TEXT("displayName"), OutUser.DisplayName))
	{
		return false;
	}

	TryGetOptionalInt(JsonObject, TEXT("partyID"), OutUser.PartyID);
	return true;
}

bool USocialSubsystem::ParseFriend(const TSharedPtr<FJsonObject>& JsonObject, FSocialFriend& OutFriend) const
{
	if (!TryGetRequiredInt(JsonObject, TEXT("userID"), OutFriend.UserID))
	{
		return false;
	}

	if (!TryGetRequiredString(JsonObject, TEXT("displayName"), OutFriend.DisplayName))
	{
		return false;
	}

	FString Status;
	OutFriend.Status = JsonObject->TryGetStringField(TEXT("status"), Status) ? ParseFriendStatus(Status) : EFriendStatus::Offline;
	TryGetOptionalInt(JsonObject, TEXT("partyID"), OutFriend.PartyID);
	return true;
}

bool USocialSubsystem::ParsePartyMember(const TSharedPtr<FJsonObject>& JsonObject, FPartyMember& OutMember) const
{
	return TryGetRequiredInt(JsonObject, TEXT("userID"), OutMember.UserID)
		&& TryGetRequiredString(JsonObject, TEXT("displayName"), OutMember.DisplayName);
}

bool USocialSubsystem::ParseParty(const TSharedPtr<FJsonObject>& JsonObject, FSocialParty& OutParty) const
{
	if (!TryGetRequiredInt(JsonObject, TEXT("partyID"), OutParty.PartyID))
	{
		return false;
	}

	if (!TryGetRequiredString(JsonObject, TEXT("name"), OutParty.Name))
	{
		return false;
	}

	if (!TryGetRequiredInt(JsonObject, TEXT("leaderUserID"), OutParty.LeaderUserID))
	{
		return false;
	}

	if (!TryGetRequiredString(JsonObject, TEXT("leaderDisplayName"), OutParty.LeaderDisplayName))
	{
		return false;
	}

	if (!TryGetRequiredInt(JsonObject, TEXT("maxSize"), OutParty.MaxSize))
	{
		return false;
	}

	OutParty.Members.Reset();
	const TArray<TSharedPtr<FJsonValue>>* MemberValues = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("members"), MemberValues))
	{
		for (const TSharedPtr<FJsonValue>& MemberValue : *MemberValues)
		{
			FPartyMember Member;
			if (!ParsePartyMember(MemberValue.IsValid() ? MemberValue->AsObject() : nullptr, Member))
			{
				return false;
			}

			OutParty.Members.Add(Member);
		}
	}

	return true;
}

bool USocialSubsystem::ParseFriendsArray(const FString& JsonString, TArray<FSocialFriend>& OutFriends) const
{
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		return false;
	}

	OutFriends.Reset(JsonArray.Num());
	for (const TSharedPtr<FJsonValue>& JsonValue : JsonArray)
	{
		FSocialFriend Friend;
		if (!ParseFriend(JsonValue.IsValid() ? JsonValue->AsObject() : nullptr, Friend))
		{
			return false;
		}

		OutFriends.Add(Friend);
	}

	return true;
}

bool USocialSubsystem::ParsePartiesArray(const FString& JsonString, TArray<FSocialParty>& OutParties) const
{
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		return false;
	}

	OutParties.Reset(JsonArray.Num());
	for (const TSharedPtr<FJsonValue>& JsonValue : JsonArray)
	{
		FSocialParty Party;
		if (!ParseParty(JsonValue.IsValid() ? JsonValue->AsObject() : nullptr, Party))
		{
			return false;
		}

		OutParties.Add(Party);
	}

	return true;
}

bool USocialSubsystem::ParseJsonObjectString(const FString& JsonString, TSharedPtr<FJsonObject>& OutObject) const
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

void USocialSubsystem::ApplyLocalUserUpdate(const FSocialUser& User)
{
	const int32 PreviousPartyID = LocalUser.PartyID;
	LocalUser = User;

	OnLocalUserUpdated.Broadcast(LocalUser);
	if (PreviousPartyID != LocalUser.PartyID)
	{
		OnCurrentPartyChanged.Broadcast();
	}
}

void USocialSubsystem::ApplyFriendUpdate(const FSocialFriend& Friend)
{
	for (FSocialFriend& ExistingFriend : Friends)
	{
		if (ExistingFriend.UserID == Friend.UserID)
		{
			ExistingFriend = Friend;
			SortFriends();
			OnFriendsUpdated.Broadcast();
			return;
		}
	}

	Friends.Add(Friend);
	SortFriends();
	OnFriendsUpdated.Broadcast();
}

void USocialSubsystem::ApplyPartyUpdate(const FSocialParty& Party)
{
	for (FSocialParty& ExistingParty : Parties)
	{
		if (ExistingParty.PartyID == Party.PartyID)
		{
			ExistingParty = Party;
			SortParties();
			OnPartiesUpdated.Broadcast();
			return;
		}
	}

	Parties.Add(Party);
	SortParties();
	OnPartiesUpdated.Broadcast();
}

void USocialSubsystem::SortFriends()
{
	Friends.Sort([](const FSocialFriend& Left, const FSocialFriend& Right)
	{
		const int32 LeftRank = GetStatusSortRank(Left.Status);
		const int32 RightRank = GetStatusSortRank(Right.Status);
		if (LeftRank != RightRank)
		{
			return LeftRank < RightRank;
		}

		const int32 NameComparison = Left.DisplayName.Compare(Right.DisplayName, ESearchCase::IgnoreCase);
		if (NameComparison != 0)
		{
			return NameComparison < 0;
		}

		return Left.UserID < Right.UserID;
	});
}

void USocialSubsystem::SortParties()
{
	Parties.Sort([](const FSocialParty& Left, const FSocialParty& Right)
	{
		const int32 NameComparison = Left.Name.Compare(Right.Name, ESearchCase::IgnoreCase);
		if (NameComparison != 0)
		{
			return NameComparison < 0;
		}

		return Left.PartyID < Right.PartyID;
	});
}

EFriendStatus USocialSubsystem::ParseFriendStatus(const FString& Status)
{
	if (Status.Equals(TEXT("online"), ESearchCase::IgnoreCase))
	{
		return EFriendStatus::Online;
	}

	if (Status.Equals(TEXT("in_game"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("ingame"), ESearchCase::IgnoreCase))
	{
		return EFriendStatus::InGame;
	}

	return EFriendStatus::Offline;
}
