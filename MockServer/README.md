# Friends & Parties Mock Server

A small local Go backend for the Unreal Engine **Friends & Parties** portfolio showcase.

The service deliberately behaves like a simple social backend:

- Unreal connects as a local user and supplies its configured friend IDs.
- The service marks that local user online for the session.
- HTTP endpoints return one user status, all configured friend statuses, online friends, one party, or all active parties.
- WebSocket clients receive an initial `session.snapshot`.
- External mock requests can log users in or out, and connected clients only receive presence events for users in their supplied friend list.
- Parties contain an all-friends example, a mixed friend/non-friend example, and a no-friends example.

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

## Demo Data

Automatic live activity starts on the first WebSocket connection after each server restart:

- Robin (`1013`) starts online with no party and goes offline after 20 seconds, once per run.
- Cameron (`1014`), a non-friend, joins Ranked Warmup (`2002`) after 30 seconds, leaves at 60 seconds, and continues alternating every 30 seconds. If the party is full, joining is skipped and retried on the next tick.
- Unreal's default configured friend list includes Robin. Recompile the Unreal C++ module to pick up this new default; if a Blueprint supplies its own friend list, add Robin there too.

Restart the service to reset the users and timers for another demonstration.

Local demo user:

```text
1000 Zilla
```

Default friends supplied by Unreal:

```text
1001 Alex    online, in All Friends Party
1002 Sam     in_game, in Mixed Friends Party
1003 Jamie   offline
1004 Morgan  online, in All Friends Party
```

Non-friend users:

```text
1005 Casey   online, in Mixed Friends Party
1006 Riley   in_game, in No Friends Party
1007 Taylor  online, in No Friends Party
1008 Jordan  offline
```

Active parties:

```text
2001 All Friends Party
2002 Mixed Friends Party
2003 No Friends Party
```

## Session Contract

Unreal should connect with a local user and a static friend ID list. The C++ subsystem sends this automatically, but this is the raw request shape:

```http
POST /api/session/connect
Content-Type: application/json
```

```json
{
  "localUser": {
    "userID": 1000,
    "displayName": "Zilla"
  },
  "friendIDs": [1001, 1002, 1003, 1004]
}
```

The response includes the local user, every supplied friend with status, only online friends, and all active parties:

```json
{
  "localUser": {
    "userID": 1000,
    "displayName": "Zilla"
  },
  "friends": [],
  "onlineFriends": [],
  "parties": []
}
```

## REST Endpoints

### Connect A Session

```http
POST /api/session/connect
```

### Local User

```http
GET /api/me?userID=1000&displayName=Zilla&friendIDs=1001,1002,1003,1004
```

### All Known Users

```http
GET /api/users
```

### Single User Status

```http
GET /api/users/1001/status
```

### All Configured Friend Statuses

```http
GET /api/friends?userID=1000&displayName=Zilla&friendIDs=1001,1002,1003,1004
```

### Online Configured Friends

```http
GET /api/friends/online?userID=1000&displayName=Zilla&friendIDs=1001,1002,1003,1004
```

### Active Parties

```http
GET /api/parties
```

### Single Active Party

```http
GET /api/parties/2002
```

### Join A Party

```http
POST /api/parties/2001/join?userID=1000
```

### Leave A Party

```http
POST /api/parties/2001/leave?userID=1000
```

### Mock User Login

```http
POST /api/mock/users/1001/login?status=online
POST /api/mock/users/1002/login?status=in_game
```

### Mock User Logout

```http
POST /api/mock/users/1001/logout
```

Logout sets that user offline, removes them from their party if needed, and broadcasts the update to WebSocket clients that listed that user as a friend.

## WebSocket

Connect with the same session context:

```text
ws://localhost:8080/ws?userID=1000&displayName=Zilla&friendIDs=1001,1002,1003,1004
```

### Session Snapshot

Sent immediately after connection:

```json
{
  "type": "session.snapshot",
  "data": {
    "localUser": {
      "userID": 1000,
      "displayName": "Zilla"
    },
    "friends": [],
    "onlineFriends": [],
    "parties": []
  }
}
```

### Friend Presence Event

Sent only when the changed user is in the connected client's friend list:

```json
{
  "type": "friend.presence_changed",
  "data": {
    "userID": 1001,
    "displayName": "Alex",
    "status": "offline"
  }
}
```

### Party Update

```json
{
  "type": "party.updated",
  "data": {
    "partyID": 2001,
    "name": "All Friends Party",
    "leaderUserID": 1001,
    "leaderDisplayName": "Alex",
    "members": [],
    "maxSize": 4
  }
}
```

### Local-User Party Change

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

## Useful Test Commands

PowerShell examples:

```powershell
$body = '{"localUser":{"userID":1000,"displayName":"Zilla"},"friendIDs":[1001,1002,1003,1004]}'
Invoke-RestMethod -Method Post "http://localhost:8080/api/session/connect" -ContentType "application/json" -Body $body
Invoke-RestMethod "http://localhost:8080/api/friends/online?userID=1000&displayName=Zilla&friendIDs=1001,1002,1003,1004"
Invoke-RestMethod "http://localhost:8080/api/users/1001/status"
Invoke-RestMethod "http://localhost:8080/api/parties"
Invoke-RestMethod "http://localhost:8080/api/parties/2003"
Invoke-RestMethod -Method Post "http://localhost:8080/api/mock/users/1001/logout"
Invoke-RestMethod -Method Post "http://localhost:8080/api/mock/users/1001/login?status=online"
```

## Suggested Unreal Integration Order

1. Start the mock server with `go run .`.
2. In a widget, get the Game Instance subsystem `SocialSubsystem`.
3. Bind `OnFriendsUpdated`, `OnFriendStatusUpdated`, `OnPartiesUpdated`, `OnLocalUserUpdated`, `OnCurrentPartyChanged`, `OnPartyActionCompleted`, and `OnRequestFailed`.
4. Call `ConnectWebSocket` and `RefreshAll` when the social menu opens.
5. Populate `WBP_FriendsList` from `GetFriends`.
6. Populate `WBP_PartyList` from `GetParties`.
7. Use `RequestFriendStatus`, `RequestAllFriendStatuses`, `RequestParty`, or `RequestAllParties` for manual refresh buttons or startup refreshes.
8. Use `JoinParty`, `LeaveParty`, or `LeaveCurrentParty` from button clicks.
9. Use Postman or an external script with the mock login/logout endpoints to demonstrate live presence changes.

## Portfolio Note

This project is intentionally a simplified personal mock of an integration pattern used professionally. It should not contain proprietary source code, credentials, assets, or company-specific implementation details.
