# UE5 Friends And Parties

An Unreal Engine 5.8 portfolio project that showcases a simple friends, presence, and party UI integration.

The project uses a small local Go mock service instead of a real platform backend. Unreal talks to that service through HTTP for initial state and actions, then listens to WebSocket events for live friend and party updates. The goal is to demonstrate the client-side integration pattern clearly without shipping proprietary code, credentials, or third-party platform dependencies.

## What This Demonstrates

- A Blueprint-friendly `USocialSubsystem` for social data.
- HTTP requests for local session state, friend status checks, active parties, join, and leave.
- WebSocket updates for friend presence, party updates, and current-party changes.
- UMG widgets that can be populated from cached subsystem data.
- A showcase `GameMode` and `PlayerController` that can boot directly into the social UI.
- A local mock backend that is easy to run and inspect.

## Project Layout

```text
FriendsAndParties.uproject       Unreal project file
Source/FriendsAndParties/        Game module, showcase GameMode and PlayerController
Source/OnlineSystems/            Social subsystem, structs, HTTP/WebSocket integration
Content/UI/                      UMG widgets for the social showcase
Content/Level/                   Example/showcase levels
MockServer/                      Local Go backend for demo data and live updates
```

## Requirements

- Unreal Engine 5.8
- Visual Studio 2022 with C++ game development tools
- Go 1.22 or newer for the mock service

## Run The Mock Service

From a terminal:

```powershell
cd "MockServer"
go mod download
go run .
```

The service runs at:

- HTTP: `http://localhost:8080`
- WebSocket: `ws://localhost:8080/ws`

Useful test calls from PowerShell:

```powershell
$body = '{"localUser":{"userID":1000,"displayName":"Zilla"},"friendIDs":[1001,1002,1003,1004]}'
Invoke-RestMethod -Method Post "http://localhost:8080/api/session/connect" -ContentType "application/json" -Body $body
Invoke-RestMethod "http://localhost:8080/api/friends/online?userID=1000&displayName=Zilla&friendIDs=1001,1002,1003,1004"
Invoke-RestMethod "http://localhost:8080/api/users/1001/status"
Invoke-RestMethod "http://localhost:8080/api/parties"
Invoke-RestMethod "http://localhost:8080/api/parties/2002"
Invoke-RestMethod -Method Post "http://localhost:8080/api/mock/users/1001/logout"
Invoke-RestMethod -Method Post "http://localhost:8080/api/mock/users/1001/login?status=online"
Invoke-RestMethod -Method Post "http://localhost:8080/api/parties/2001/join?userID=1000"
Invoke-RestMethod -Method Post "http://localhost:8080/api/parties/2001/leave?userID=1000"
```

## Run The Unreal Project

1. Start the mock service first.
2. Open `FriendsAndParties.uproject` in Unreal Engine 5.8.
3. Compile the C++ modules if Unreal prompts you.
4. Open an existing showcase level, or create a new test level.
5. Press Play.

The project default GameMode is set to:

```text
/Script/FriendsAndParties.FriendsAndPartiesGameMode
```

That GameMode uses `ASocialShowcasePlayerController`, which:

- shows the mouse cursor,
- creates `/Game/UI/WBP_SocialMenu` on BeginPlay when available,
- connects the `USocialSubsystem`,
- calls `RefreshAll` so the UI has data to display.

## Creating A Test Level

For the fastest demo:

1. Create or open a level.
2. In World Settings, use `FriendsAndPartiesGameMode` as the GameMode override, or rely on the project default.
3. Press Play while the mock service is running.

For a more editable Blueprint setup:

1. Create `BP_SocialShowcasePlayerController` from `SocialShowcasePlayerController`.
2. Set `Social Menu Widget Class` to your social menu widget, for example `WBP_SocialMenu`.
3. Create `BP_FriendsAndPartiesGameMode` from `FriendsAndPartiesGameMode`.
4. Set its Player Controller Class to `BP_SocialShowcasePlayerController`.
5. Set that GameMode in your test level's World Settings.

## Blueprint Integration

In a widget such as `WBP_SocialMenu`, get the subsystem:

```text
Get Game Instance
-> Get Game Instance Subsystem
-> Class: SocialSubsystem
```

Store it as a variable, for example `SocialSubsystemRef`.

Bind these events:

- `OnFriendsUpdated`
- `OnPartiesUpdated`
- `OnLocalUserUpdated`
- `OnCurrentPartyChanged`
- `OnConnectionChanged`
- `OnPartyActionCompleted`
- `OnFriendStatusUpdated`
- `OnRequestFailed`

If your widget is spawned by `SocialShowcasePlayerController`, the controller already calls `ConnectWebSocket` and `RefreshAll` after creating the widget. If you use the widget somewhere else, call those nodes yourself after binding events.

### Friend UI

When `OnFriendsUpdated` fires:

1. Call `GetFriends`.
2. Clear your friends list container.
3. For each `SocialFriend`, create or update a friend row widget.

Useful fields:

- `UserID`
- `DisplayName`
- `Status`
- `PartyID`

Useful friend nodes:

- `GetConfiguredFriends`: the static Unreal-side friend list used when connecting to the service.
- `RequestFriendStatus(UserID)`: request one user's current status.
- `RequestAllFriendStatuses`: ask the mock service which configured friends are online, then mark everyone else offline locally.
- `GetFriendByUserID`: read one cached friend row after a request or WebSocket update.
- `GetFriendStatusText`: display the enum as text.

Do not drive presence from Unreal for the showcase UI. Use Postman, curl, or a small external script against `POST /api/mock/users/{userID}/login` and `POST /api/mock/users/{userID}/logout`; Unreal should only receive and display the updates.

### Party UI

When `OnPartiesUpdated` fires:

1. Call `GetParties`.
2. Clear your party list container.
3. For each `SocialParty`, create or update a party row widget.

Useful fields:

- `PartyID`
- `Name`
- `LeaderDisplayName`
- `Members`
- `MaxSize`

A join button should call `JoinParty(PartyID)`. The mock service starts with Alex's party at party ID `2001`.

Useful party nodes:

- `RequestParty(PartyID)`: request one active party.
- `RequestAllParties`: request the active party list.
- `GetPartyByID`: read one cached party by ID.
- `GetCurrentParty`: read the party the local user is currently in.

### Current Party UI

When `OnCurrentPartyChanged` or `OnLocalUserUpdated` fires:

1. Call `IsInParty`.
2. If true, call `GetCurrentParty` and display its members.
3. If false, show an empty/not-in-party state.

A leave button can call `LeaveCurrentParty`.

### Errors And Button Feedback

Use `OnPartyActionCompleted` for join/leave feedback:

- `bSuccess`: whether the action worked.
- `PartyID`: the party that was requested.
- `ErrorMessage`: text to show if it failed.

Use `OnRequestFailed` for general request errors, especially when the mock service is not running.

## Mock Data

The mock service starts with:

- Local user: `Zilla` with user ID `1000`
- Friends: Alex `1001`, Sam `1002`, Jamie `1003`, Morgan `1004`
- Non-friends: Casey `1005`, Riley `1006`, Taylor `1007`, Jordan `1008`
- Parties: All Friends Party `2001`, Mixed Friends Party `2002`, No Friends Party `2003`

Presence is updated externally:

```text
POST /api/mock/users/1001/logout
POST /api/mock/users/1001/login?status=online
POST /api/mock/users/1002/login?status=in_game
```

## Notes

This is intentionally a simplified showcase project. It is meant to demonstrate Unreal-side social UI architecture and live update handling, not production authentication, matchmaking, or platform account integration.
