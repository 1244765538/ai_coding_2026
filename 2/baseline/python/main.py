import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit


poll = None
selected = None


def result():
    indexed = list(enumerate(poll["options"]))
    indexed.sort(key=lambda item: (-(1 if item[1] == selected else 0), item[0]))
    return {
        "id": "poll_1",
        "title": poll["title"],
        "status": poll["status"],
        "results": [
            {"option": option, "votes": 1 if option == selected else 0}
            for _index, option in indexed
        ],
        "total_votes": 1 if selected is not None else 0,
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, _format, *_args):
        return

    def send_json(self, status, value):
        data = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def read_json(self):
        length = int(self.headers.get("Content-Length", "0"))
        value = json.loads(self.rfile.read(length) or b"{}")
        if not isinstance(value, dict):
            raise ValueError
        return value

    def parts(self):
        return [unquote(item) for item in urlsplit(self.path).path.split("/") if item]

    def do_GET(self):
        if urlsplit(self.path).path == "/health":
            self.send_json(200, {"status": "ok"})
        else:
            self.send_json(404, {"error": "not found"})

    def do_POST(self):
        global poll, selected
        try:
            body = self.read_json()
            parts = self.parts()
            if parts == ["reset"]:
                poll = None
                selected = None
                self.send_json(200, {"ok": True})
                return
            if parts == ["polls"]:
                title = body.get("title")
                options = body.get("options")
                if not isinstance(title, str) or not title.strip():
                    raise ValueError
                if not isinstance(options, list) or not 2 <= len(options) <= 8:
                    raise ValueError
                options = [item.strip() for item in options]
                if any(not item for item in options):
                    raise ValueError
                poll = {"title": title.strip(), "options": options, "status": "open"}
                selected = None
                self.send_json(200, result())
                return
            if len(parts) == 3 and parts[0] == "polls" and parts[2] == "close" and poll:
                poll["status"] = "closed"
                self.send_json(200, result())
                return
            self.send_json(404, {"error": "not found"})
        except Exception:
            self.send_json(400, {"error": "invalid request"})

    def do_PUT(self):
        global selected
        try:
            body = self.read_json()
            parts = self.parts()
            if (
                len(parts) != 4
                or parts[0] != "polls"
                or parts[2] != "votes"
                or not poll
            ):
                self.send_json(404, {"error": "not found"})
                return
            option = body.get("option")
            if poll["status"] != "open" or option not in poll["options"]:
                self.send_json(200, result())
                return
            if selected is not None and selected != option:
                self.send_json(200, result())
                return
            selected = option
            self.send_json(200, result())
        except Exception:
            self.send_json(400, {"error": "invalid request"})


if __name__ == "__main__":
    ThreadingHTTPServer(
        ("0.0.0.0", int(os.environ.get("PORT", "8000"))), Handler
    ).serve_forever()
