#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdlib>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
using namespace std;

struct Poll { string title; vector<string> options; string status; };
Poll poll;
bool hasPoll = false;
string selected;

string trim(string value) {
  while (!value.empty() && isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
  while (!value.empty() && isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  return value;
}

string escapeJson(const string &value) {
  string result;
  for (char ch : value) { if (ch == '\\' || ch == '"') result += '\\'; result += ch; }
  return result;
}

bool stringField(const string &body, const string &field, string &value) {
  size_t key = body.find("\"" + field + "\"");
  if (key == string::npos) return false;
  size_t colon = body.find(':', key), start = body.find('"', colon + 1);
  if (colon == string::npos || start == string::npos) return false;
  size_t end = body.find('"', start + 1);
  if (end == string::npos) return false;
  value = body.substr(start + 1, end - start - 1);
  return true;
}

bool optionsField(const string &body, vector<string> &options) {
  size_t key = body.find("\"options\"");
  if (key == string::npos) return false;
  size_t start = body.find('[', key), end = body.find(']', start);
  if (start == string::npos || end == string::npos) return false;
  size_t pos = start + 1;
  while (pos < end) {
    size_t left = body.find('"', pos);
    if (left == string::npos || left >= end) break;
    size_t right = body.find('"', left + 1);
    if (right == string::npos || right > end) return false;
    options.push_back(trim(body.substr(left + 1, right - left - 1)));
    pos = right + 1;
  }
  return true;
}

string view() {
  vector<int> order;
  for (int index = 0; index < static_cast<int>(poll.options.size()); index++) order.push_back(index);
  stable_sort(order.begin(), order.end(), [](int left, int right) {
    int leftVotes = poll.options[left] == selected ? 1 : 0;
    int rightVotes = poll.options[right] == selected ? 1 : 0;
    return leftVotes != rightVotes ? leftVotes > rightVotes : left < right;
  });
  string results = "[";
  for (size_t position = 0; position < order.size(); position++) {
    if (position) results += ',';
    const string &option = poll.options[order[position]];
    results += "{\"option\":\"" + escapeJson(option) + "\",\"votes\":" + to_string(option == selected ? 1 : 0) + "}";
  }
  results += "]";
  return "{\"id\":\"poll_1\",\"title\":\"" + escapeJson(poll.title) + "\",\"status\":\"" + poll.status + "\",\"results\":" + results + ",\"total_votes\":" + to_string(selected.empty() ? 0 : 1) + "}";
}

string response(int status, const string &body) {
  string label = status == 200 ? "200 OK" : status == 404 ? "404 Not Found" : "400 Bad Request";
  return "HTTP/1.1 " + label + "\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " + to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

bool startsWith(const string &value, const string &prefix) { return value.rfind(prefix, 0) == 0; }

string route(const string &method, const string &path, const string &body, int &status) {
  if (method == "GET" && path == "/health") { status = 200; return "{\"status\":\"ok\"}"; }
  if (method == "POST" && path == "/reset") { hasPoll = false; selected.clear(); status = 200; return "{\"ok\":true}"; }
  if (method == "POST" && path == "/polls") {
    string title;
    vector<string> options;
    if (!stringField(body, "title", title) || trim(title).empty() || !optionsField(body, options) || options.size() < 2 || options.size() > 8) {
      status = 400; return "{\"error\":\"invalid request\"}";
    }
    for (const string &option : options) if (option.empty()) { status = 400; return "{\"error\":\"invalid request\"}"; }
    poll = {trim(title), options, "open"}; hasPoll = true; selected.clear(); status = 200; return view();
  }
  if (method == "PUT" && startsWith(path, "/polls/poll_1/votes/") && hasPoll) {
    string option;
    if (!stringField(body, "option", option) || find(poll.options.begin(), poll.options.end(), option) == poll.options.end() || poll.status != "open") {
      status = 200; return view();
    }
    if (!selected.empty() && selected != option) { status = 200; return view(); }
    selected = option; status = 200; return view();
  }
  if (method == "POST" && path == "/polls/poll_1/close" && hasPoll) {
    poll.status = "closed"; status = 200; return view();
  }
  status = 404; return "{\"error\":\"not found\"}";
}

int main() {
  int port = getenv("PORT") ? atoi(getenv("PORT")) : 8000;
  int server = socket(AF_INET, SOCK_STREAM, 0), enabled = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
  sockaddr_in address{};
  address.sin_family = AF_INET; address.sin_addr.s_addr = INADDR_ANY; address.sin_port = htons(port);
  if (::bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 || listen(server, 64) < 0) return 1;
  while (true) {
    int client = accept(server, nullptr, nullptr);
    if (client < 0) continue;
    string request;
    char buffer[8192];
    size_t contentLength = 0;
    while (true) {
      ssize_t count = recv(client, buffer, sizeof(buffer), 0);
      if (count <= 0) break;
      request.append(buffer, count);
      size_t split = request.find("\r\n\r\n");
      if (split != string::npos) {
        string header = request.substr(0, split);
        size_t key = header.find("Content-Length:");
        if (key != string::npos) contentLength = stoul(header.substr(key + 15));
        if (request.size() >= split + 4 + contentLength) break;
      }
    }
    size_t first = request.find(' '), second = request.find(' ', first + 1);
    string method = request.substr(0, first), path = request.substr(first + 1, second - first - 1);
    size_t split = request.find("\r\n\r\n");
    string body = split == string::npos ? "" : request.substr(split + 4);
    int status = 200;
    string responseBody = route(method, path, body, status);
    string output = response(status, responseBody);
    send(client, output.data(), output.size(), 0);
    close(client);
  }
}
