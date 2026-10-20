#include "SocialSubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericPlatformHttp.h"
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

FString AppendQueryString(const FString& BaseURL, const FString& QueryString)
{
	if (QueryString.IsEmpty())
	{
		return BaseURL;
	}

	return BaseURL + (BaseURL.Contains(TEXT("?")) ? TEXT("&") : TEXT("?")) + QueryString;
}

FString UrlEncode(const FString& Value)
{
	return FGenericPlatformHttp::UrlEncode(Value);
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

void TryGetOptionalBool(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, bool& OutValue)
{
	if (!JsonObject.IsValid() || !JsonObject->TryGetBoolField(FieldName, OutValue))
	{
		OutValue = false;
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

FSocialFriend MakeConfiguredFriend(int32 UserID, const TCHAR* DisplayName)
{
	FSocialFriend Friend;
	Friend.UserID = UserID;
	Friend.DisplayName = DisplayName;
	Friend.Status = EFriendStatus::Offline;
	Friend.PartyID = 0;
	return Friend;
}
}

USocialSubsystem::USocialSubsystem()
{
	MockLocalUser.UserID = 1000;
	MockLocalUser.DisplayName = TEXT("Zilla");
	MockLocalUser.PartyID = 0;

	MockFriends = {
		MakeConfiguredFriend(1001, TEXT("Alex")),
		MakeConfiguredFriend(1002, TEXT("Sam")),
		MakeConfiguredFriend(1003, TEXT("Jamie")),
		MakeConfiguredFriend(1004, TEXT("Morgan")),
		MakeConfiguredFriend(1013, TEXT("Robin"))
	};

	LocalUser = MockLocalUser;
	ResetFriendsToConfiguredList();
}

void USocialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("SocialSubsystem initialized"));

	LocalUser = MockLocalUser;
	ResetFriendsToConfiguredList();
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
	RequestAllFriendStatuses();
	RequestAllParties();
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

	WebSocket = FWebSocketsModule::Get().CreateWebSocket(AppendQueryString(WebSocketURL, BuildSessionQueryString()));
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
	SendJsonPostRequest(TEXT("RequestLocalUser"), TEXT("/api/session/connect"), BuildSessionRequestBody(), [this](const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> JsonObject;
		if (!ParseJsonObjectString(ResponseBody, JsonObject))
		{
			BroadcastRequestFailed(TEXT("RequestLocalUser"), TEXT("Could not parse the session response."));
			return;
		}

		ApplySessionSnapshot(JsonObject);
	});
}

void USocialSubsystem::RequestFriends()
{
	RequestAllFriendStatuses();
}

void USocialSubsystem::RequestParties()
{
	RequestAllParties();
}

void USocialSubsystem::RequestFriendStatus(int32 UserID)
{
	SendGetRequest(TEXT("RequestFriendStatus"), FString::Printf(TEXT("/api/users/%d/status"), UserID), [this](const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> JsonObject;
		FSocialFriend Friend;
		if (!ParseJsonObjectString(ResponseBody, JsonObject) || !ParseFriend(JsonObject, Friend))
		{
			BroadcastRequestFailed(TEXT("RequestFriendStatus"), TEXT("Could not parse the friend status response."));
			return;
		}

		if (!IsConfiguredFriendID(Friend.UserID))
		{
			UE_LOG(LogTemp, Verbose, TEXT("Ignoring status for non-friend user %d"), Friend.UserID);
			return;
		}

		ApplyFriendUpdate(Friend);
		OnFriendStatusUpdated.Broadcast(Friend);
	});
}

void USocialSubsystem::RequestAllFriendStatuses()
{
	const FString Endpoint = FString::Printf(
		TEXT("/api/friends/online?userID=%d&displayName=%s&friendIDs=%s"),
		MockLocalUser.UserID,
		*UrlEncode(MockLocalUser.DisplayName),
		*BuildFriendIDsQueryParam());

	SendGetRequest(TEXT("RequestAllFriendStatuses"), Endpoint, [this](const FString& ResponseBody)
	{
		TArray<FSocialFriend> ParsedFriends;
		if (!ParseFriendsArray(ResponseBody, ParsedFriends))
		{
			BroadcastRequestFailed(TEXT("RequestAllFriendStatuses"), TEXT("Could not parse the friends response."));
			return;
		}

		ApplyFriendSnapshot(ParsedFriends);
	});
}

void USocialSubsystem::RequestParty(int32 PartyID)
{
	SendGetRequest(TEXT("RequestParty"), FString::Printf(TEXT("/api/parties/%d"), PartyID), [this](const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> JsonObject;
		FSocialParty Party;
		if (!ParseJsonObjectString(ResponseBody, JsonObject) || !ParseParty(JsonObject, Party))
		{
			BroadcastRequestFailed(TEXT("RequestParty"), TEXT("Could not parse the party response."));
			return;
		}

		ApplyPartyUpdate(Party);
	});
}

void USocialSubsystem::RequestAllParties()
{
	SendGetRequest(TEXT("RequestAllParties"), TEXT("/api/parties"), [this](const FString& ResponseBody)
	{
		TArray<FSocialParty> ParsedParties;
		if (!ParsePartiesArray(ResponseBody, ParsedParties))
		{
			BroadcastRequestFailed(TEXT("RequestAllParties"), TEXT("Could not parse the parties response."));
			return;
		}

		for (FSocialParty& Party : ParsedParties)
		{
			DecorateParty(Party);
		}

		Parties = MoveTemp(ParsedParties);
		SortParties();
		OnPartiesUpdated.Broadcast();
	});
}

void USocialSubsystem::JoinParty(int32 PartyID)
{
	const FString Endpoint = FString::Printf(TEXT("/api/parties/%d/join?userID=%d"), PartyID, MockLocalUser.UserID);
	SendPostRequest(TEXT("JoinParty"), Endpoint, [this, PartyID](const FString& ResponseBody)
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
	const FString Endpoint = FString::Printf(TEXT("/api/parties/%d/leave?userID=%d"), PartyID, MockLocalUser.UserID);
	SendPostRequest(TEXT("LeaveParty"), Endpoint, [this, PartyID](const FString& ResponseBody)
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

const TArray<FSocialFriend>& USocialSubsystem::GetFriends() const
{
	return Friends;
}

const TArray<FSocialParty>& USocialSubsystem::GetParties() const
{
	return Parties;
}

const TArray<FSocialFriend>& USocialSubsystem::GetConfiguredFriends() const
{
	return MockFriends;
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

void USocialSubsystem::SetMockLocalUser(int32 UserID, const FString& DisplayName)
{
	MockLocalUser.UserID = UserID;
	MockLocalUser.DisplayName = DisplayName;
	LocalUser.UserID = UserID;
	LocalUser.DisplayName = DisplayName;
	OnLocalUserUpdated.Broadcast(LocalUser);
}

void USocialSubsystem::SetMockFriendList(const TArray<FSocialFriend>& InFriends)
{
	MockFriends = InFriends;
	ResetFriendsToConfiguredList();
	OnFriendsUpdated.Broadcast();
}

void USocialSubsystem::HandleWebSocketConnected()
{
	UE_LOG(LogTemp, Log, TEXT("WebSocket connected"));
	OnConnectionChanged.Broadcast(true);
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

	if (EventType.Equals(TEXT("session.snapshot"), ESearchCase::IgnoreCase))
	{
		ApplySessionSnapshot(DataObject);
		return;
	}

	if (EventType.Equals(TEXT("friend.presence_changed"), ESearchCase::IgnoreCase))
	{
		FSocialFriend Friend;
		if (!ParseFriend(DataObject, Friend))
		{
			BroadcastRequestFailed(TEXT("WebSocket"), TEXT("Could not parse a friend presence event."));
			return;
		}

		if (IsConfiguredFriendID(Friend.UserID))
		{
			ApplyFriendUpdate(Friend);
			OnFriendStatusUpdated.Broadcast(Friend);
		}
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

void USocialSubsystem::SendJsonPostRequest(const FString& OperationName, const FString& Endpoint, const FString& JsonBody, TFunction<void(const FString&)> OnSuccess)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(BuildURL(BaseHttpURL, Endpoint));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(JsonBody);

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
	if (!TryGetRequiredInt(JsonObject, TEXT("userID"), OutMember.UserID))
	{
		return false;
	}

	if (!TryGetRequiredString(JsonObject, TEXT("displayName"), OutMember.DisplayName))
	{
		return false;
	}

	FString Status;
	OutMember.Status = JsonObject->TryGetStringField(TEXT("status"), Status) ? ParseFriendStatus(Status) : EFriendStatus::Offline;
	TryGetOptionalInt(JsonObject, TEXT("partyID"), OutMember.PartyID);
	TryGetOptionalBool(JsonObject, TEXT("isFriend"), OutMember.bIsFriend);
	TryGetOptionalBool(JsonObject, TEXT("isLocalUser"), OutMember.bIsLocalUser);
	return true;
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

	DecorateParty(OutParty);
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

bool USocialSubsystem::ParseFriendsField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, TArray<FSocialFriend>& OutFriends) const
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!JsonObject.IsValid() || !JsonObject->TryGetArrayField(FieldName, JsonArray))
	{
		return false;
	}

	OutFriends.Reset(JsonArray->Num());
	for (const TSharedPtr<FJsonValue>& JsonValue : *JsonArray)
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

bool USocialSubsystem::ParsePartiesField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, TArray<FSocialParty>& OutParties) const
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!JsonObject.IsValid() || !JsonObject->TryGetArrayField(FieldName, JsonArray))
	{
		return false;
	}

	OutParties.Reset(JsonArray->Num());
	for (const TSharedPtr<FJsonValue>& JsonValue : *JsonArray)
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

void USocialSubsystem::ApplySessionSnapshot(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		BroadcastRequestFailed(TEXT("SessionSnapshot"), TEXT("Session snapshot was empty."));
		return;
	}

	const TSharedPtr<FJsonObject>* LocalUserObjectPtr = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("localUser"), LocalUserObjectPtr))
	{
		FSocialUser User;
		if (ParseLocalUser(*LocalUserObjectPtr, User))
		{
			ApplyLocalUserUpdate(User);
		}
	}

	TArray<FSocialFriend> SnapshotFriends;
	if (ParseFriendsField(JsonObject, TEXT("onlineFriends"), SnapshotFriends)
		|| ParseFriendsField(JsonObject, TEXT("friends"), SnapshotFriends))
	{
		ApplyFriendSnapshot(SnapshotFriends);
	}

	TArray<FSocialParty> SnapshotParties;
	if (ParsePartiesField(JsonObject, TEXT("parties"), SnapshotParties))
	{
		Parties = MoveTemp(SnapshotParties);
		SortParties();
		OnPartiesUpdated.Broadcast();
	}
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

void USocialSubsystem::ApplyFriendSnapshot(const TArray<FSocialFriend>& SnapshotFriends)
{
	ResetFriendsToConfiguredList();

	for (const FSocialFriend& Friend : SnapshotFriends)
	{
		bool bUpdatedExistingFriend = false;
		for (FSocialFriend& ExistingFriend : Friends)
		{
			if (ExistingFriend.UserID == Friend.UserID)
			{
				const FString DisplayName = ExistingFriend.DisplayName.IsEmpty() ? Friend.DisplayName : ExistingFriend.DisplayName;
				ExistingFriend = Friend;
				ExistingFriend.DisplayName = DisplayName;
				bUpdatedExistingFriend = true;
				break;
			}
		}

		if (!bUpdatedExistingFriend && IsConfiguredFriendID(Friend.UserID))
		{
			Friends.Add(Friend);
		}
	}

	SortFriends();
	OnFriendsUpdated.Broadcast();
}

void USocialSubsystem::ApplyFriendUpdate(const FSocialFriend& Friend)
{
	for (FSocialFriend& ExistingFriend : Friends)
	{
		if (ExistingFriend.UserID == Friend.UserID)
		{
			const FString DisplayName = ExistingFriend.DisplayName.IsEmpty() ? Friend.DisplayName : ExistingFriend.DisplayName;
			ExistingFriend = Friend;
			ExistingFriend.DisplayName = DisplayName;
			SortFriends();
			OnFriendsUpdated.Broadcast();
			return;
		}
	}

	if (IsConfiguredFriendID(Friend.UserID))
	{
		Friends.Add(Friend);
		SortFriends();
		OnFriendsUpdated.Broadcast();
	}
}

void USocialSubsystem::ApplyPartyUpdate(const FSocialParty& Party)
{
	FSocialParty DecoratedParty = Party;
	DecorateParty(DecoratedParty);

	for (FSocialParty& ExistingParty : Parties)
	{
		if (ExistingParty.PartyID == DecoratedParty.PartyID)
		{
			ExistingParty = DecoratedParty;
			SortParties();
			OnPartiesUpdated.Broadcast();
			return;
		}
	}

	Parties.Add(DecoratedParty);
	SortParties();
	OnPartiesUpdated.Broadcast();
}

void USocialSubsystem::ResetFriendsToConfiguredList()
{
	Friends = MockFriends;
	for (FSocialFriend& Friend : Friends)
	{
		Friend.Status = EFriendStatus::Offline;
		Friend.PartyID = 0;
	}
	SortFriends();
}

void USocialSubsystem::DecorateParty(FSocialParty& Party) const
{
	for (FPartyMember& Member : Party.Members)
	{
		Member.bIsLocalUser = Member.UserID == LocalUser.UserID || Member.UserID == MockLocalUser.UserID;
		Member.bIsFriend = IsConfiguredFriendID(Member.UserID);
	}
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

bool USocialSubsystem::IsConfiguredFriendID(int32 UserID) const
{
	for (const FSocialFriend& Friend : MockFriends)
	{
		if (Friend.UserID == UserID)
		{
			return true;
		}
	}

	return false;
}

FString USocialSubsystem::BuildSessionQueryString() const
{
	return FString::Printf(
		TEXT("userID=%d&displayName=%s&friendIDs=%s"),
		MockLocalUser.UserID,
		*UrlEncode(MockLocalUser.DisplayName),
		*BuildFriendIDsQueryParam());
}

FString USocialSubsystem::BuildSessionRequestBody() const
{
	FString FriendIDsJson;
	for (int32 Index = 0; Index < MockFriends.Num(); ++Index)
	{
		if (Index > 0)
		{
			FriendIDsJson += TEXT(",");
		}
		FriendIDsJson += FString::FromInt(MockFriends[Index].UserID);
	}

	return FString::Printf(
		TEXT("{\"localUser\":{\"userID\":%d,\"displayName\":\"%s\",\"partyID\":%d},\"friendIDs\":[%s]}"),
		MockLocalUser.UserID,
		*MockLocalUser.DisplayName.ReplaceCharWithEscapedChar(),
		MockLocalUser.PartyID,
		*FriendIDsJson);
}

FString USocialSubsystem::BuildFriendIDsQueryParam() const
{
	FString Result;
	for (int32 Index = 0; Index < MockFriends.Num(); ++Index)
	{
		if (Index > 0)
		{
			Result += TEXT(",");
		}
		Result += FString::FromInt(MockFriends[Index].UserID);
	}

	return Result;
}
