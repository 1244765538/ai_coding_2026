package main

import (
	"encoding/json"
	"net/http"
	"os"
	"regexp"
	"sort"
	"strings"
	"sync"
)

type Poll struct {
	Title   string
	Options []string
	Status  string
}

type Ranked struct {
	Option string `json:"option"`
	Votes  int    `json:"votes"`
	Index  int    `json:"-"`
}

var lock sync.Mutex
var poll *Poll
var selected string
var votePath = regexp.MustCompile(`^/polls/poll_1/votes/[A-Za-z0-9_-]+$`)

func send(writer http.ResponseWriter, status int, value any) {
	writer.Header().Set("Content-Type", "application/json; charset=utf-8")
	writer.WriteHeader(status)
	_ = json.NewEncoder(writer).Encode(value)
}

func view() map[string]any {
	results := make([]Ranked, 0, len(poll.Options))
	for index, option := range poll.Options {
		votes := 0
		if option == selected {
			votes = 1
		}
		results = append(results, Ranked{option, votes, index})
	}
	sort.SliceStable(results, func(i, j int) bool {
		if results[i].Votes != results[j].Votes {
			return results[i].Votes > results[j].Votes
		}
		return results[i].Index < results[j].Index
	})
	total := 0
	if selected != "" {
		total = 1
	}
	return map[string]any{"id": "poll_1", "title": poll.Title, "status": poll.Status, "results": results, "total_votes": total}
}

func handler(writer http.ResponseWriter, request *http.Request) {
	lock.Lock()
	defer lock.Unlock()
	if request.Method == http.MethodGet && request.URL.Path == "/health" {
		send(writer, 200, map[string]any{"status": "ok"})
		return
	}
	var body map[string]any
	if json.NewDecoder(request.Body).Decode(&body) != nil {
		send(writer, 400, map[string]any{"error": "invalid request"})
		return
	}
	if request.Method == http.MethodPost && request.URL.Path == "/reset" {
		poll = nil
		selected = ""
		send(writer, 200, map[string]any{"ok": true})
		return
	}
	if request.Method == http.MethodPost && request.URL.Path == "/polls" {
		title, ok := body["title"].(string)
		raw, optionsOK := body["options"].([]any)
		if !ok || strings.TrimSpace(title) == "" || !optionsOK || len(raw) < 2 || len(raw) > 8 {
			send(writer, 400, map[string]any{"error": "invalid request"})
			return
		}
		options := make([]string, 0, len(raw))
		for _, item := range raw {
			option, stringOK := item.(string)
			option = strings.TrimSpace(option)
			if !stringOK || option == "" {
				send(writer, 400, map[string]any{"error": "invalid request"})
				return
			}
			options = append(options, option)
		}
		poll = &Poll{strings.TrimSpace(title), options, "open"}
		selected = ""
		send(writer, 200, view())
		return
	}
	if request.Method == http.MethodPut && votePath.MatchString(request.URL.Path) && poll != nil {
		option, ok := body["option"].(string)
		known := false
		for _, item := range poll.Options {
			known = known || item == option
		}
		if !ok || !known || poll.Status != "open" {
			send(writer, 200, view())
			return
		}
		if selected != "" && selected != option {
			send(writer, 200, view())
			return
		}
		selected = option
		send(writer, 200, view())
		return
	}
	if request.Method == http.MethodPost && request.URL.Path == "/polls/poll_1/close" && poll != nil {
		poll.Status = "closed"
		send(writer, 200, view())
		return
	}
	send(writer, 404, map[string]any{"error": "not found"})
}

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8000"
	}
	_ = http.ListenAndServe("0.0.0.0:"+port, http.HandlerFunc(handler))
}
