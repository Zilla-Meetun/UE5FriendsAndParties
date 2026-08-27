package main

import (
	"encoding/json"
	"errors"
	"log"
	"net/http"
	"strconv"
	"strings"
	"sync"

	"github.com/gorilla/websocket"
)

type FriendStatus string

const (
	StatusOffline FriendStatus = "offline"
	StatusOnline  FriendStatus = "online"
	StatusInGame  FriendStatus = "in_game"
)

type Friend struct {
	UserID      int          `json:"userID"`
	DisplayName string       `json:"displayName"`
	Status      FriendStatus `json:"status"`
	PartyID     int          `json:"partyID,omitempty"`
}

type PartyMember struct {
	UserID      int    `json:"userID"`
	DisplayName string `json:"displayName"`
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

type Event struct {
	Type string `json:"type"`
	Data any    `json:"data,omitempty"`
}

type ServerState struct {
	mu sync.RWMutex

	Me      LocalUser
	Friends []Friend
	Parties []Party
}

func NewMockState() *ServerState {
	return &ServerState{
		Me: LocalUser{UserID: 1000, DisplayName: "Zilla"},
		Friends: []Friend{
			{UserID: 1001, DisplayName: "Alex", Status: StatusOnline, PartyID: 2001},
			{UserID: 1002, DisplayName: "Sam", Status: StatusInGame, PartyID: 2002},
			{UserID: 1003, DisplayName: "Jamie", Status: StatusOffline},
			{UserID: 1004, DisplayName: "Morgan", Status: StatusOnline, PartyID: 2001},
		},
		Parties: []Party{
			{
				PartyID:           2001,
				Name:              "Alex's Party",
				LeaderUserID:      1001,
				LeaderDisplayName: "Alex",
				MaxSize:           4,
				Members: []PartyMember{
					{UserID: 1001, DisplayName: "Alex"},
					{UserID: 1004, DisplayName: "Morgan"},
				},
			},
			{
				PartyID:           2002,
				Name:              "Sam's Party",
				LeaderUserID:      1002,
				LeaderDisplayName: "Sam",
				MaxSize:           4,
				Members: []PartyMember{
					{UserID: 1002, DisplayName: "Sam"},
				},
			},
		},
	}
}

type Client struct {
	Conn *websocket.Conn
	Send chan []byte
}

type Hub struct {
	mu      sync.RWMutex
	clients map[*Client]struct{}
}

func NewHub() *Hub {
	return &Hub{clients: make(map[*Client]struct{})}
}

func (h *Hub) Register(client *Client) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.clients[client] = struct{}{}
}

func (h *Hub) Unregister(client *Client) {
	h.mu.Lock()
	defer h.mu.Unlock()

	if _, exists := h.clients[client]; exists {
		delete(h.clients, client)
		close(client.Send)
	}
}

func (h *Hub) Broadcast(event Event) {
	data, err := json.Marshal(event)
	if err != nil {
		log.Printf("failed to serialise event: %v", err)
		return
	}

	h.mu.RLock()
	defer h.mu.RUnlock()

	for client := range h.clients {
		select {
		case client.Send <- data:
		default:
			log.Println("dropping event for slow client")
		}
	}
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

func HandleWebSocket(hub *Hub, w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WebSocket upgrade failed: %v", err)
		return
	}

	client := &Client{Conn: conn, Send: make(chan []byte, 16)}
	hub.Register(client)
	log.Println("WebSocket client connected")

	go client.WritePump()
	client.ReadPump(hub)
}

func (c *Client) ReadPump(hub *Hub) {
	defer func() {
		hub.Unregister(c)
		_ = c.Conn.Close()
		log.Println("WebSocket client disconnected")
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

func ToggleFriendStatus(state *ServerState, hub *Hub, userID int) (Friend, error) {
	state.mu.Lock()

	var updated Friend
	found := false

	for i := range state.Friends {
		friend := &state.Friends[i]
		if friend.UserID != userID {
			continue
		}

		switch friend.Status {
		case StatusOffline:
			friend.Status = StatusOnline
		case StatusOnline:
			friend.Status = StatusInGame
		case StatusInGame:
			friend.Status = StatusOffline
		default:
			friend.Status = StatusOffline
		}

		updated = *friend
		found = true
		break
	}

	state.mu.Unlock()

	if !found {
		return Friend{}, errors.New("friend not found")
	}

	hub.Broadcast(Event{Type: "friend.presence_changed", Data: updated})
	return updated, nil
}

func JoinParty(state *ServerState, hub *Hub, partyID int) (Party, error) {
	state.mu.Lock()

	if state.Me.PartyID != 0 {
		state.mu.Unlock()
		return Party{}, errors.New("leave the current party before joining another")
	}

	for i := range state.Parties {
		party := &state.Parties[i]
		if party.PartyID != partyID {
			continue
		}

		if len(party.Members) >= party.MaxSize {
			state.mu.Unlock()
			return Party{}, errors.New("party is full")
		}

		party.Members = append(party.Members, PartyMember{
			UserID:      state.Me.UserID,
			DisplayName: state.Me.DisplayName,
		})
		state.Me.PartyID = party.PartyID

		updated := *party
		updated.Members = append([]PartyMember(nil), party.Members...)
		state.mu.Unlock()

		hub.Broadcast(Event{Type: "party.updated", Data: updated})
		hub.Broadcast(Event{
			Type: "self.party_changed",
			Data: map[string]int{"partyID": updated.PartyID},
		})

		return updated, nil
	}

	state.mu.Unlock()
	return Party{}, errors.New("party not found")
}

func LeaveParty(state *ServerState, hub *Hub, partyID int) (Party, error) {
	state.mu.Lock()

	if state.Me.PartyID == 0 {
		state.mu.Unlock()
		return Party{}, errors.New("local user is not currently in a party")
	}

	if state.Me.PartyID != partyID {
		state.mu.Unlock()
		return Party{}, errors.New("local user is not a member of that party")
	}

	for i := range state.Parties {
		party := &state.Parties[i]
		if party.PartyID != partyID {
			continue
		}

		filtered := make([]PartyMember, 0, len(party.Members))
		for _, member := range party.Members {
			if member.UserID != state.Me.UserID {
				filtered = append(filtered, member)
			}
		}

		party.Members = filtered
		state.Me.PartyID = 0

		updated := *party
		updated.Members = append([]PartyMember(nil), party.Members...)
		state.mu.Unlock()

		hub.Broadcast(Event{Type: "party.updated", Data: updated})
		hub.Broadcast(Event{
			Type: "self.party_changed",
			Data: map[string]int{"partyID": 0},
		})

		return updated, nil
	}

	state.mu.Unlock()
	return Party{}, errors.New("party not found")
}

func GetMe(state *ServerState, w http.ResponseWriter) {
	state.mu.RLock()
	me := state.Me
	state.mu.RUnlock()
	WriteJSON(w, http.StatusOK, me)
}

func GetFriends(state *ServerState, w http.ResponseWriter) {
	state.mu.RLock()
	friends := append([]Friend(nil), state.Friends...)
	state.mu.RUnlock()
	WriteJSON(w, http.StatusOK, friends)
}

func GetParties(state *ServerState, w http.ResponseWriter) {
	state.mu.RLock()
	parties := make([]Party, len(state.Parties))
	for i, party := range state.Parties {
		parties[i] = party
		parties[i].Members = append([]PartyMember(nil), party.Members...)
	}
	state.mu.RUnlock()
	WriteJSON(w, http.StatusOK, parties)
}

func WriteJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(value); err != nil {
		log.Printf("failed writing JSON: %v", err)
	}
}

func main() {
	state := NewMockState()
	hub := NewHub()
	mux := http.NewServeMux()

	mux.HandleFunc("GET /api/me", func(w http.ResponseWriter, r *http.Request) {
		GetMe(state, w)
	})

	mux.HandleFunc("GET /api/friends", func(w http.ResponseWriter, r *http.Request) {
		GetFriends(state, w)
	})

	mux.HandleFunc("GET /api/parties", func(w http.ResponseWriter, r *http.Request) {
		GetParties(state, w)
	})

	mux.HandleFunc("GET /ws", func(w http.ResponseWriter, r *http.Request) {
		HandleWebSocket(hub, w, r)
	})

	mux.HandleFunc("POST /api/mock/friends/", func(w http.ResponseWriter, r *http.Request) {
		path := strings.TrimPrefix(r.URL.Path, "/api/mock/friends/")
		parts := strings.Split(path, "/")
		if len(parts) != 2 || parts[1] != "toggle" {
			http.NotFound(w, r)
			return
		}

		userID, err := strconv.Atoi(parts[0])
		if err != nil {
			http.Error(w, "invalid userID", http.StatusBadRequest)
			return
		}

		friend, err := ToggleFriendStatus(state, hub, userID)
		if err != nil {
			http.Error(w, err.Error(), http.StatusNotFound)
			return
		}

		WriteJSON(w, http.StatusOK, friend)
	})

	mux.HandleFunc("POST /api/parties/", func(w http.ResponseWriter, r *http.Request) {
		path := strings.TrimPrefix(r.URL.Path, "/api/parties/")
		parts := strings.Split(path, "/")
		if len(parts) != 2 {
			http.NotFound(w, r)
			return
		}

		partyID, err := strconv.Atoi(parts[0])
		if err != nil {
			http.Error(w, "invalid partyID", http.StatusBadRequest)
			return
		}

		switch parts[1] {
		case "join":
			party, err := JoinParty(state, hub, partyID)
			if err != nil {
				http.Error(w, err.Error(), http.StatusConflict)
				return
			}
			WriteJSON(w, http.StatusOK, party)

		case "leave":
			party, err := LeaveParty(state, hub, partyID)
			if err != nil {
				http.Error(w, err.Error(), http.StatusConflict)
				return
			}
			WriteJSON(w, http.StatusOK, party)

		default:
			http.NotFound(w, r)
		}
	})

	log.Println("Friends & Parties mock service")
	log.Println("HTTP:      http://localhost:8080")
	log.Println("WebSocket: ws://localhost:8080/ws")

	log.Fatal(http.ListenAndServe(":8080", mux))
}
