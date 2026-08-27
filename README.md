# UE5 Friends And Parties

An Unreal Engine 5.8 portfolio project that showcases a simple friends, presence, and party UI integration.

The project uses a small local Go mock service instead of a real platform backend. Unreal talks to that service through HTTP for initial state and actions, then listens to WebSocket events for live friend and party updates. The goal is to demonstrate the client-side integration pattern clearly without shipping proprietary code, credentials, or third-party platform dependencies.

## What This Demonstrates

- A Blueprint-friendly `USocialSubsystem` for social data.
- HTTP requests for local user, friends, parties, join, leave, and mock debug actions.
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

Useful test calls:

```powershell
curl http://localhost:8080/api/me
curl http://localhost:8080/api/friends
curl http://localhost:8080/api/parties
curl -X POST http://localhost:8080/api/mock/friends/1001/toggle
curl -X POST http://localhost:8080/api/parties/2001/join
curl -X POST http://localhost:8080/api/parties/2001/leave
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
- `OnMockPresenceToggled`
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

Use `GetFriendStatusText` to display the enum as text. For a demo/debug button, call `ToggleMockFriendPresence(UserID)`. The mock users include Alex at user ID `1001`.

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
- Parties: Alex's Party `2001`, Sam's Party `2002`

Calling the mock presence endpoint cycles a friend's status:

```text
offline -> online -> in_game -> offline
```

## Notes

This is intentionally a simplified showcase project. It is meant to demonstrate Unreal-side social UI architecture and live update handling, not production authentication, matchmaking, or platform account integration.
