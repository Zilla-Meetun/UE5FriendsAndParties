# Friends & Parties Mock Server

A small local Go backend for the Unreal Engine **Friends & Parties** portfolio showcase.

It provides:

- Mock local-user data
- Mock friends and presence/status data
- Mock active parties
- REST endpoints for initial state
- WebSocket broadcasts for live updates
- Join/leave party actions
- A mock presence-toggle endpoint for demonstrating live UI updates

## Requirements

- Go 1.22+ recommended
- Internet access the first time dependencies are downloaded

## Run

Open a terminal in this folder:

```bash
go mod download
go run .
```

The service runs at:

- HTTP: `http://localhost:8080`
- WebSocket: `ws://localhost:8080/ws`

## REST endpoints

### Local user

```http
GET /api/me
```

Example:

```json
{
  "userID": 1000,
  "displayName": "Zilla",
  "partyID": 0
}
```

### Friends

```http
GET /api/friends
```

Example:

```json
[
  {
    "userID": 1001,
    "displayName": "Alex",
    "status": "online",
    "partyID": 2001
  }
]
```

### Active parties

```http
GET /api/parties
```

### Join a party

```http
POST /api/parties/2001/join
```

### Leave a party

```http
POST /api/parties/2001/leave
```

### Toggle a friend's presence

This is a showcase/debug endpoint that simulates a platform presence update.

```http
POST /api/mock/friends/1001/toggle
```

Each call cycles:

```text
offline -> online -> in_game -> offline
```

A successful change is also broadcast to connected WebSocket clients.

## WebSocket

Connect to:

```text
ws://localhost:8080/ws
```

### Presence event

```json
{
  "type": "friend.presence_changed",
  "data": {
    "userID": 1001,
    "displayName": "Alex",
    "status": "in_game",
    "partyID": 2001
  }
}
```

### Party update

```json
{
  "type": "party.updated",
  "data": {
    "partyID": 2001,
    "name": "Alex's Party",
    "leaderUserID": 1001,
    "leaderDisplayName": "Alex",
    "members": [
      {
        "userID": 1001,
        "displayName": "Alex"
      },
      {
        "userID": 1000,
        "displayName": "Zilla"
      }
    ],
    "maxSize": 4
  }
}
```

### Local-user party change

```json
{
  "type": "self.party_changed",
  "data": {
    "partyID": 2001
  }
}
```

When leaving:

```json
{
  "type": "self.party_changed",
  "data": {
    "partyID": 0
  }
}
```

## Useful test commands

Get friends:

```bash
curl http://localhost:8080/api/friends
```

Get parties:

```bash
curl http://localhost:8080/api/parties
```

Toggle Alex's presence:

```bash
curl -X POST http://localhost:8080/api/mock/friends/1001/toggle
```

Join Alex's party:

```bash
curl -X POST http://localhost:8080/api/parties/2001/join
```

Leave Alex's party:

```bash
curl -X POST http://localhost:8080/api/parties/2001/leave
```

## Suggested Unreal integration order

1. Start the mock server with `go run .`.
2. In a widget, get the Game Instance subsystem `SocialSubsystem`.
3. Bind `OnFriendsUpdated`, `OnPartiesUpdated`, `OnLocalUserUpdated`, `OnCurrentPartyChanged`, `OnPartyActionCompleted`, and `OnRequestFailed`.
4. Call `ConnectWebSocket` and `RefreshAll` when the social menu opens.
5. Populate `WBP_Friends` from `GetFriends`.
6. Populate `WBP_Parties` from `GetParties`.
7. Use `JoinParty`, `LeaveParty`, or `LeaveCurrentParty` from button clicks.
8. Use `ToggleMockFriendPresence` for a showcase/debug button that demonstrates live presence updates.

## Portfolio note

This project is intentionally a simplified personal mock of an integration pattern used professionally. It should not contain proprietary source code, credentials, assets, or company-specific implementation details.
