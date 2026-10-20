package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

type FriendStatus string

const (
	StatusOffline FriendStatus = "offline"
	StatusOnline  FriendStatus = "online"
	StatusInGame  FriendStatus = "in_game"
)

type User struct {
	UserID      int          `json:"userID"`
	DisplayName string       `json:"displayName"`
	Status      FriendStatus `json:"status"`
	PartyID     int          `json:"partyID,omitempty"`
}

type PartyMember struct {
	UserID      int          `json:"userID"`
	DisplayName string       `json:"displayName"`
	Status      FriendStatus `json:"status"`
	PartyID     int          `json:"partyID,omitempty"`
}

type Party struct {
	PartyID           int           `json:"partyID"`
	Name              string        `json:"name"`
	LeaderUserID      int           `json:"leaderUserID"`
	LeaderDisplayName string        `json:"leaderDisplayName"`
	Members           []PartyMember `json:"members"`
	MaxSize           int           `json:"maxSize"`
}

type LocalUser struct {
	UserID      int    `json:"userID"`
	DisplayName string `json:"displayName"`
	PartyID     int    `json:"partyID,omitempty"`
}

type SessionRequest struct {
	LocalUser LocalUser `json:"localUser"`
	FriendIDs []int     `json:"friendIDs"`
}

type SessionSnapshot struct {
	LocalUser     LocalUser `json:"localUser"`
	Friends       []User    `json:"friends"`
	OnlineFriends []User    `json:"onlineFriends"`
	Parties       []Party   `json:"parties"`
}

type Event struct {
	Type string `json:"type"`
	Data any    `json:"data,omitempty"`
}

type ServerState struct {
	mu sync.RWMutex

	Users       map[int]*User
	PartyOrder  []int
	Parties     map[int]*Party
	Friendships map[int][]int
}

func NewMockState() *ServerState {
	users := map[int]*User{
		1000: {UserID: 1000, DisplayName: "Zilla", Status: StatusOffline},
		1001: {UserID: 1001, DisplayName: "Alex", Status: StatusOnline, PartyID: 2001},
		1002: {UserID: 1002, DisplayName: "Sam", Status: StatusInGame, PartyID: 2002},
		1003: {UserID: 1003, DisplayName: "Jamie", Status: StatusOffline},
		1004: {UserID: 1004, DisplayName: "Morgan", Status: StatusOnline, PartyID: 2001},
		1005: {UserID: 1005, DisplayName: "Casey", Status: StatusOnline, PartyID: 2002},
		1006: {UserID: 1006, DisplayName: "Riley", Status: StatusInGame, PartyID: 2003},
		1007: {UserID: 1007, DisplayName: "Taylor", Status: StatusOnline, PartyID: 2003},
		1008: {UserID: 1008, DisplayName: "Jordan", Status: StatusOffline},
		1013: {UserID: 1013, DisplayName: "Robin", Status: StatusOnline},
		1014: {UserID: 1014, DisplayName: "Cameron", Status: StatusOnline},
		1009: {UserID: 1009, DisplayName: "Drew", Status: StatusOnline, PartyID: 2004},
		1010: {UserID: 1010, DisplayName: "Avery", Status: StatusOnline, PartyID: 2004},
		1011: {UserID: 1011, DisplayName: "Quinn", Status: StatusOnline, PartyID: 2004},
		1012: {UserID: 1012, DisplayName: "Blake", Status: StatusOnline, PartyID: 2004},
	}

	parties := map[int]*Party{
		2001: {
			PartyID:           2001,
			Name:              "Friday Night Squad",
			LeaderUserID:      1001,
			LeaderDisplayName: "Alex",
			MaxSize:           4,
			Members: []PartyMember{
				memberFromUser(users[1001]),
				memberFromUser(users[1004]),
			},
		},
		2002: {
			PartyID:           2002,
			Name:              "Ranked Warmup",
			LeaderUserID:      1002,
			LeaderDisplayName: "Sam",
			MaxSize:           4,
			Members: []PartyMember{
				memberFromUser(users[1002]),
				memberFromUser(users[1005]),
			},
		},
		2003: {
			PartyID:           2003,
			Name:              "Casual Queue",
			LeaderUserID:      1006,
			LeaderDisplayName: "Riley",
			MaxSize:           4,
			Members: []PartyMember{
				memberFromUser(users[1006]),
				memberFromUser(users[1007]),
			},
		},
		2004: {
			PartyID:           2004,
			Name:              "The Regulars",
			LeaderUserID:      1009,
			LeaderDisplayName: "Drew",
			MaxSize:           4,
			Members: []PartyMember{
				memberFromUser(users[1009]),
				memberFromUser(users[1010]),
				memberFromUser(users[1011]),
				memberFromUser(users[1012]),
			},
		},
	}

	return &ServerState{
		Users:      users,
		Parties:    parties,
		PartyOrder: []int{2001, 2002, 2003, 2004},
		Friendships: map[int][]int{
			1000: {1001, 1002, 1003, 1004, 1013},
		},
	}
}

func memberFromUser(user *User) PartyMember {
	if user == nil {
		return PartyMember{}
	}

	return PartyMember{
		UserID:      user.UserID,
		DisplayName: user.DisplayName,
		Status:      user.Status,
		PartyID:     user.PartyID,
	}
}

func (s *ServerState) ensureUser(localUser LocalUser) LocalUser {
	if existing, ok := s.Users[localUser.UserID]; ok {
		if localUser.DisplayName != "" {
			existing.DisplayName = localUser.DisplayName
		}
		existing.Status = StatusOnline
		return LocalUser{UserID: existing.UserID, DisplayName: existing.DisplayName, PartyID: existing.PartyID}
	}

	user := &User{
		UserID:      localUser.UserID,
		DisplayName: localUser.DisplayName,
		Status:      StatusOnline,
		PartyID:     localUser.PartyID,
	}
	s.Users[user.UserID] = user
	return LocalUser{UserID: user.UserID, DisplayName: user.DisplayName, PartyID: user.PartyID}
}

func (s *ServerState) friendIDsFor(userID int, supplied []int) []int {
	if len(supplied) > 0 {
		return uniqueSortedIDs(supplied)
	}

	return uniqueSortedIDs(s.Friendships[userID])
}

func (s *ServerState) snapshot(localUser LocalUser, suppliedFriendIDs []int) SessionSnapshot {
	s.mu.Lock()
	me := s.ensureUser(localUser)
	friendIDs := s.friendIDsFor(me.UserID, suppliedFriendIDs)
	friends := s.usersForIDsLocked(friendIDs)
	onlineFriends := onlineOnly(friends)
	parties := s.allPartiesLocked()
	s.mu.Unlock()

	return SessionSnapshot{
		LocalUser:     me,
		Friends:       friends,
		OnlineFriends: onlineFriends,
		Parties:       parties,
	}
}

func (s *ServerState) usersForIDsLocked(ids []int) []User {
	users := make([]User, 0, len(ids))
	for _, id := range ids {
		if user, ok := s.Users[id]; ok {
			users = append(users, *user)
		}
	}
	sortUsers(users)
	return users
}

func (s *ServerState) allUsers() []User {
	s.mu.RLock()
	users := make([]User, 0, len(s.Users))
	for _, user := range s.Users {
		users = append(users, *user)
	}
	s.mu.RUnlock()

	sortUsers(users)
	return users
}

func (s *ServerState) userByID(userID int) (User, bool) {
	s.mu.RLock()
	user, ok := s.Users[userID]
	if !ok {
		s.mu.RUnlock()
		return User{}, false
	}
	result := *user
	s.mu.RUnlock()
	return result, true
}

func (s *ServerState) allPartiesLocked() []Party {
	parties := make([]Party, 0, len(s.Parties))
	for _, partyID := range s.PartyOrder {
		if party, ok := s.Parties[partyID]; ok {
			parties = append(parties, s.copyPartyLocked(party))
		}
	}
	return parties
}

func (s *ServerState) allParties() []Party {
	s.mu.RLock()
	parties := s.allPartiesLocked()
	s.mu.RUnlock()
	return parties
}

func (s *ServerState) partyByID(partyID int) (Party, bool) {
	s.mu.RLock()
	party, ok := s.Parties[partyID]
	if !ok {
		s.mu.RUnlock()
		return Party{}, false
	}
	result := s.copyPartyLocked(party)
	s.mu.RUnlock()
	return result, true
}

func (s *ServerState) copyPartyLocked(party *Party) Party {
	result := *party
	result.Members = make([]PartyMember, 0, len(party.Members))
	for _, member := range party.Members {
		if user, ok := s.Users[member.UserID]; ok {
			result.Members = append(result.Members, memberFromUser(user))
			continue
		}
		result.Members = append(result.Members, member)
	}
	return result
}

func (s *ServerState) setUserStatus(userID int, status FriendStatus) (User, []Party, error) {
	s.mu.Lock()
	user, ok := s.Users[userID]
	if !ok {
		s.mu.Unlock()
		return User{}, nil, errors.New("user not found")
	}

	user.Status = status
	changedParties := make([]Party, 0, 1)

	if status == StatusOffline && user.PartyID != 0 {
		partyID := user.PartyID
		if party, ok := s.Parties[partyID]; ok {
			filtered := party.Members[:0]
			for _, member := range party.Members {
				if member.UserID != userID {
					filtered = append(filtered, member)
				}
			}
			party.Members = filtered
			changedParties = append(changedParties, s.copyPartyLocked(party))
		}
		user.PartyID = 0
	}

	updated := *user
	s.mu.Unlock()
	return updated, changedParties, nil
}

func (s *ServerState) joinParty(userID int, partyID int) (User, Party, error) {
	s.mu.Lock()
	user, ok := s.Users[userID]
	if !ok {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("user not found")
	}
	if user.Status == StatusOffline {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("user must be online before joining a party")
	}
	if user.PartyID != 0 {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("leave the current party before joining another")
	}

	party, ok := s.Parties[partyID]
	if !ok {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("party not found")
	}
	if len(party.Members) >= party.MaxSize {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("party is full")
	}

	party.Members = append(party.Members, memberFromUser(user))
	user.PartyID = partyID
	updatedUser := *user
	updatedParty := s.copyPartyLocked(party)
	s.mu.Unlock()

	return updatedUser, updatedParty, nil
}

func (s *ServerState) leaveParty(userID int, partyID int) (User, Party, error) {
	s.mu.Lock()
	user, ok := s.Users[userID]
	if !ok {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("user not found")
	}
	if user.PartyID == 0 {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("user is not currently in a party")
	}
	if user.PartyID != partyID {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("user is not a member of that party")
	}

	party, ok := s.Parties[partyID]
	if !ok {
		s.mu.Unlock()
		return User{}, Party{}, errors.New("party not found")
	}

	filtered := party.Members[:0]
	for _, member := range party.Members {
		if member.UserID != userID {
			filtered = append(filtered, member)
		}
	}
	party.Members = filtered
	user.PartyID = 0

	updatedUser := *user
	updatedParty := s.copyPartyLocked(party)
	s.mu.Unlock()

	return updatedUser, updatedParty, nil
}

type Client struct {
	Conn        *websocket.Conn
	Send        chan []byte
	UserID      int
	DisplayName string
	FriendIDs   map[int]struct{}
}

func (c *Client) HasFriend(userID int) bool {
	_, ok := c.FriendIDs[userID]
	return ok
}

type Hub struct {
	mu       sync.RWMutex
	clients  map[*Client]struct{}
	demoOnce sync.Once
}

// Start once after the first client receives its initial snapshot.
func (h *Hub) StartDemoActivity(state *ServerState) {
	h.demoOnce.Do(func() {
		go func() {
			time.Sleep(20 * time.Second)
			user, _, err := state.setUserStatus(1013, StatusOffline)
			if err != nil {
				log.Printf("demo friend logout failed: %v", err)
				return
			}
			h.BroadcastFriendPresence(user)
			log.Println("Demo: Robin went offline")
		}()
		go func() {
			ticker := time.NewTicker(30 * time.Second)
			defer ticker.Stop()
			for range ticker.C {
				current, ok := state.userByID(1014)
				if !ok {
					continue
				}
				var user User
				var party Party
				var err error
				if current.PartyID == 2002 {
					user, party, err = state.leaveParty(1014, 2002)
				} else {
					user, party, err = state.joinParty(1014, 2002)
				}
				if err != nil {
					log.Printf("demo party activity skipped: %v", err)
					continue
				}
				h.BroadcastFriendPresence(user)
				h.BroadcastPartyUpdated(party)
				h.BroadcastSelfPartyChanged(user)
				log.Printf("Demo: Cameron partyID=%d", user.PartyID)
			}
		}()
	})
}

func NewHub() *Hub {
	return &Hub{clients: make(map[*Client]struct{})}
}

func (h *Hub) Register(client *Client) {
	h.mu.Lock()
	h.clients[client] = struct{}{}
	h.mu.Unlock()
}

func (h *Hub) Unregister(client *Client) {
	h.mu.Lock()
	if _, exists := h.clients[client]; exists {
		delete(h.clients, client)
		close(client.Send)
	}
	h.mu.Unlock()
}

func (h *Hub) Send(client *Client, event Event) {
	data, err := json.Marshal(event)
	if err != nil {
		log.Printf("failed to serialise event: %v", err)
		return
	}

	select {
	case client.Send <- data:
	default:
		log.Println("dropping event for slow client")
	}
}

func (h *Hub) BroadcastFriendPresence(user User) {
	event := Event{Type: "friend.presence_changed", Data: user}
	data, err := json.Marshal(event)
	if err != nil {
		log.Printf("failed to serialise presence event: %v", err)
		return
	}

	h.mu.RLock()
	for client := range h.clients {
		if !client.HasFriend(user.UserID) {
			continue
		}
		select {
		case client.Send <- data:
		default:
			log.Println("dropping presence event for slow client")
		}
	}
	h.mu.RUnlock()
}

func (h *Hub) BroadcastPartyUpdated(party Party) {
	event := Event{Type: "party.updated", Data: party}
	data, err := json.Marshal(event)
	if err != nil {
		log.Printf("failed to serialise party event: %v", err)
		return
	}

	h.mu.RLock()
	for client := range h.clients {
		select {
		case client.Send <- data:
		default:
			log.Println("dropping party event for slow client")
		}
	}
	h.mu.RUnlock()
}

func (h *Hub) BroadcastSelfPartyChanged(user User) {
	h.mu.RLock()
	for client := range h.clients {
		if client.UserID != user.UserID {
			continue
		}
		h.Send(client, Event{
			Type: "self.party_changed",
			Data: map[string]int{"partyID": user.PartyID},
		})
	}
	h.mu.RUnlock()
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

func HandleWebSocket(state *ServerState, hub *Hub, w http.ResponseWriter, r *http.Request) {
	session, err := sessionFromQuery(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WebSocket upgrade failed: %v", err)
		return
	}

	client := &Client{
		Conn:        conn,
		Send:        make(chan []byte, 16),
		UserID:      session.LocalUser.UserID,
		DisplayName: session.LocalUser.DisplayName,
		FriendIDs:   idSet(state.friendIDsFor(session.LocalUser.UserID, session.FriendIDs)),
	}

	hub.Register(client)
	log.Printf("WebSocket client connected userID=%d friends=%v", client.UserID, sortedIDs(client.FriendIDs))

	go client.WritePump()

	snapshot := state.snapshot(session.LocalUser, session.FriendIDs)
	client.FriendIDs = idSet(state.friendIDsFor(snapshot.LocalUser.UserID, session.FriendIDs))
	hub.Send(client, Event{Type: "session.snapshot", Data: snapshot})
	hub.StartDemoActivity(state)

	client.ReadPump(hub)
}

func (c *Client) ReadPump(hub *Hub) {
	defer func() {
		hub.Unregister(c)
		_ = c.Conn.Close()
		log.Printf("WebSocket client disconnected userID=%d", c.UserID)
	}()

	for {
		if _, _, err := c.Conn.ReadMessage(); err != nil {
			return
		}
	}
}

func (c *Client) WritePump() {
	defer func() { _ = c.Conn.Close() }()

	for message := range c.Send {
		if err := c.Conn.WriteMessage(websocket.TextMessage, message); err != nil {
			return
		}
	}
}

func GetSessionSnapshot(state *ServerState, w http.ResponseWriter, r *http.Request) {
	session, err := sessionFromRequest(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	WriteJSON(w, http.StatusOK, state.snapshot(session.LocalUser, session.FriendIDs))
}

func GetMe(state *ServerState, w http.ResponseWriter, r *http.Request) {
	session, err := sessionFromQuery(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	snapshot := state.snapshot(session.LocalUser, session.FriendIDs)
	WriteJSON(w, http.StatusOK, snapshot.LocalUser)
}

func GetAllUsers(state *ServerState, w http.ResponseWriter) {
	WriteJSON(w, http.StatusOK, state.allUsers())
}

func GetUserStatus(state *ServerState, w http.ResponseWriter, userID int) {
	user, ok := state.userByID(userID)
	if !ok {
		http.Error(w, "user not found", http.StatusNotFound)
		return
	}

	WriteJSON(w, http.StatusOK, user)
}

func GetFriends(state *ServerState, w http.ResponseWriter, r *http.Request) {
	session, err := sessionFromQuery(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	snapshot := state.snapshot(session.LocalUser, session.FriendIDs)
	WriteJSON(w, http.StatusOK, snapshot.Friends)
}

func GetOnlineFriends(state *ServerState, w http.ResponseWriter, r *http.Request) {
	session, err := sessionFromQuery(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	snapshot := state.snapshot(session.LocalUser, session.FriendIDs)
	WriteJSON(w, http.StatusOK, snapshot.OnlineFriends)
}

func GetParties(state *ServerState, w http.ResponseWriter) {
	WriteJSON(w, http.StatusOK, state.allParties())
}

func GetParty(state *ServerState, w http.ResponseWriter, partyID int) {
	party, ok := state.partyByID(partyID)
	if !ok {
		http.Error(w, "party not found", http.StatusNotFound)
		return
	}

	WriteJSON(w, http.StatusOK, party)
}

func MockLogin(state *ServerState, hub *Hub, w http.ResponseWriter, userID int, status FriendStatus) {
	user, _, err := state.setUserStatus(userID, status)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}

	hub.BroadcastFriendPresence(user)
	WriteJSON(w, http.StatusOK, user)
}

func MockLogout(state *ServerState, hub *Hub, w http.ResponseWriter, userID int) {
	user, changedParties, err := state.setUserStatus(userID, StatusOffline)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}

	hub.BroadcastFriendPresence(user)
	for _, party := range changedParties {
		hub.BroadcastPartyUpdated(party)
	}
	WriteJSON(w, http.StatusOK, user)
}

func JoinParty(state *ServerState, hub *Hub, w http.ResponseWriter, r *http.Request, partyID int) {
	userID, err := userIDFromRequest(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	user, party, err := state.joinParty(userID, partyID)
	if err != nil {
		http.Error(w, err.Error(), http.StatusConflict)
		return
	}

	hub.BroadcastFriendPresence(user)
	hub.BroadcastPartyUpdated(party)
	hub.BroadcastSelfPartyChanged(user)
	WriteJSON(w, http.StatusOK, party)
}

func LeaveParty(state *ServerState, hub *Hub, w http.ResponseWriter, r *http.Request, partyID int) {
	userID, err := userIDFromRequest(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	user, party, err := state.leaveParty(userID, partyID)
	if err != nil {
		http.Error(w, err.Error(), http.StatusConflict)
		return
	}

	hub.BroadcastFriendPresence(user)
	hub.BroadcastPartyUpdated(party)
	hub.BroadcastSelfPartyChanged(user)
	WriteJSON(w, http.StatusOK, party)
}

func WriteJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(value); err != nil {
		log.Printf("failed writing JSON: %v", err)
	}
}

func sessionFromRequest(r *http.Request) (SessionRequest, error) {
	if r.Body == nil {
		return sessionFromQuery(r)
	}

	defer r.Body.Close()
	var session SessionRequest
	if err := json.NewDecoder(r.Body).Decode(&session); err != nil {
		return SessionRequest{}, fmt.Errorf("invalid session JSON: %w", err)
	}
	if session.LocalUser.UserID == 0 {
		return SessionRequest{}, errors.New("localUser.userID is required")
	}
	return session, nil
}

func sessionFromQuery(r *http.Request) (SessionRequest, error) {
	query := r.URL.Query()
	userID, err := strconv.Atoi(query.Get("userID"))
	if err != nil || userID == 0 {
		return SessionRequest{}, errors.New("query parameter userID is required")
	}

	displayName := query.Get("displayName")
	if displayName == "" {
		displayName = fmt.Sprintf("User %d", userID)
	}

	friendIDs := parseIDs(query.Get("friendIDs"))
	return SessionRequest{
		LocalUser: LocalUser{UserID: userID, DisplayName: displayName},
		FriendIDs: friendIDs,
	}, nil
}

func userIDFromRequest(r *http.Request) (int, error) {
	userID, err := strconv.Atoi(r.URL.Query().Get("userID"))
	if err != nil || userID == 0 {
		return 0, errors.New("query parameter userID is required")
	}
	return userID, nil
}

func parseIDs(value string) []int {
	parts := strings.Split(value, ",")
	ids := make([]int, 0, len(parts))
	for _, part := range parts {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		id, err := strconv.Atoi(part)
		if err == nil && id != 0 {
			ids = append(ids, id)
		}
	}
	return uniqueSortedIDs(ids)
}

func parseStatus(value string) (FriendStatus, error) {
	switch strings.ToLower(strings.TrimSpace(value)) {
	case "", string(StatusOnline):
		return StatusOnline, nil
	case string(StatusInGame), "ingame", "in-game":
		return StatusInGame, nil
	case string(StatusOffline):
		return StatusOffline, nil
	default:
		return "", fmt.Errorf("unsupported status %q", value)
	}
}

func onlineOnly(users []User) []User {
	online := make([]User, 0, len(users))
	for _, user := range users {
		if user.Status != StatusOffline {
			online = append(online, user)
		}
	}
	return online
}

func sortUsers(users []User) {
	sort.Slice(users, func(i, j int) bool {
		if users[i].Status != users[j].Status {
			return statusRank(users[i].Status) < statusRank(users[j].Status)
		}
		if users[i].DisplayName != users[j].DisplayName {
			return users[i].DisplayName < users[j].DisplayName
		}
		return users[i].UserID < users[j].UserID
	})
}

func statusRank(status FriendStatus) int {
	switch status {
	case StatusInGame:
		return 0
	case StatusOnline:
		return 1
	default:
		return 2
	}
}

func uniqueSortedIDs(ids []int) []int {
	seen := make(map[int]struct{}, len(ids))
	for _, id := range ids {
		if id != 0 {
			seen[id] = struct{}{}
		}
	}

	result := make([]int, 0, len(seen))
	for id := range seen {
		result = append(result, id)
	}
	sort.Ints(result)
	return result
}

func idSet(ids []int) map[int]struct{} {
	set := make(map[int]struct{}, len(ids))
	for _, id := range ids {
		set[id] = struct{}{}
	}
	return set
}

func sortedIDs(set map[int]struct{}) []int {
	ids := make([]int, 0, len(set))
	for id := range set {
		ids = append(ids, id)
	}
	sort.Ints(ids)
	return ids
}

func main() {
	state := NewMockState()
	hub := NewHub()
	mux := http.NewServeMux()

	mux.HandleFunc("POST /api/session/connect", func(w http.ResponseWriter, r *http.Request) {
		GetSessionSnapshot(state, w, r)
	})

	mux.HandleFunc("GET /api/me", func(w http.ResponseWriter, r *http.Request) {
		GetMe(state, w, r)
	})

	mux.HandleFunc("GET /api/users", func(w http.ResponseWriter, r *http.Request) {
		GetAllUsers(state, w)
	})

	mux.HandleFunc("GET /api/users/{userID}/status", func(w http.ResponseWriter, r *http.Request) {
		userID, err := strconv.Atoi(r.PathValue("userID"))
		if err != nil {
			http.Error(w, "invalid userID", http.StatusBadRequest)
			return
		}
		GetUserStatus(state, w, userID)
	})

	mux.HandleFunc("GET /api/friends", func(w http.ResponseWriter, r *http.Request) {
		GetFriends(state, w, r)
	})

	mux.HandleFunc("GET /api/friends/online", func(w http.ResponseWriter, r *http.Request) {
		GetOnlineFriends(state, w, r)
	})

	mux.HandleFunc("GET /api/parties", func(w http.ResponseWriter, r *http.Request) {
		GetParties(state, w)
	})

	mux.HandleFunc("GET /api/parties/{partyID}", func(w http.ResponseWriter, r *http.Request) {
		partyID, err := strconv.Atoi(r.PathValue("partyID"))
		if err != nil {
			http.Error(w, "invalid partyID", http.StatusBadRequest)
			return
		}
		GetParty(state, w, partyID)
	})

	mux.HandleFunc("GET /ws", func(w http.ResponseWriter, r *http.Request) {
		HandleWebSocket(state, hub, w, r)
	})

	mux.HandleFunc("POST /api/mock/users/{userID}/login", func(w http.ResponseWriter, r *http.Request) {
		userID, err := strconv.Atoi(r.PathValue("userID"))
		if err != nil {
			http.Error(w, "invalid userID", http.StatusBadRequest)
			return
		}
		status, err := parseStatus(r.URL.Query().Get("status"))
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		MockLogin(state, hub, w, userID, status)
	})

	mux.HandleFunc("POST /api/mock/users/{userID}/logout", func(w http.ResponseWriter, r *http.Request) {
		userID, err := strconv.Atoi(r.PathValue("userID"))
		if err != nil {
			http.Error(w, "invalid userID", http.StatusBadRequest)
			return
		}
		MockLogout(state, hub, w, userID)
	})

	mux.HandleFunc("POST /api/parties/{partyID}/join", func(w http.ResponseWriter, r *http.Request) {
		partyID, err := strconv.Atoi(r.PathValue("partyID"))
		if err != nil {
			http.Error(w, "invalid partyID", http.StatusBadRequest)
			return
		}
		JoinParty(state, hub, w, r, partyID)
	})

	mux.HandleFunc("POST /api/parties/{partyID}/leave", func(w http.ResponseWriter, r *http.Request) {
		partyID, err := strconv.Atoi(r.PathValue("partyID"))
		if err != nil {
			http.Error(w, "invalid partyID", http.StatusBadRequest)
			return
		}
		LeaveParty(state, hub, w, r, partyID)
	})

	log.Println("Friends & Parties mock service")
	log.Println("HTTP:      http://localhost:8080")
	log.Println("WebSocket: ws://localhost:8080/ws?userID=1000&displayName=Zilla&friendIDs=1001,1002,1003,1004")

	log.Fatal(http.ListenAndServe(":8080", mux))
}
