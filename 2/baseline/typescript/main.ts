import http, { IncomingMessage, ServerResponse } from "http";

type Poll = { title: string; options: string[]; status: string };
let poll: Poll | null = null;
let selected: string | null = null;

function view(): unknown {
    const current = poll as Poll;
    const ranked = current.options.map((option, index) => ({ option, index, votes: option === selected ? 1 : 0 }));
    ranked.sort((a, b) => b.votes - a.votes || a.index - b.index);
    return { id: "poll_1", title: current.title, status: current.status, results: ranked.map(({ option, votes }) => ({ option, votes })), total_votes: selected === null ? 0 : 1 };
}

function send(response: ServerResponse, status: number, value: unknown): void {
    const body = JSON.stringify(value);
    response.writeHead(status, { "Content-Type": "application/json; charset=utf-8", "Content-Length": Buffer.byteLength(body) });
    response.end(body);
}

function readBody(request: IncomingMessage): Promise<Record<string, unknown>> {
    return new Promise((resolve, reject) => {
        let text = "";
        request.on("data", (chunk) => { text += chunk; });
        request.on("end", () => {
            try {
                const value = text ? JSON.parse(text) : {};
                if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error();
                resolve(value);
            } catch (error) { reject(error); }
        });
        request.on("error", reject);
    });
}

http.createServer(async (request, response) => {
    try {
        const url = new URL(request.url || "/", "http://localhost");
        if (request.method === "GET" && url.pathname === "/health") return send(response, 200, { status: "ok" });
        const body = await readBody(request);
        if (request.method === "POST" && url.pathname === "/reset") {
            poll = null;
            selected = null;
            return send(response, 200, { ok: true });
        }
        if (request.method === "POST" && url.pathname === "/polls") {
            if (typeof body.title !== "string" || !body.title.trim() || !Array.isArray(body.options) || body.options.length < 2 || body.options.length > 8) throw new Error();
            const options = body.options.map((item) => typeof item === "string" ? item.trim() : "");
            if (options.some((item) => !item)) throw new Error();
            poll = { title: body.title.trim(), options, status: "open" };
            selected = null;
            return send(response, 200, view());
        }
        if (request.method === "PUT" && /^\/polls\/poll_1\/votes\/[A-Za-z0-9_-]+$/.test(url.pathname) && poll) {
            if (poll.status !== "open" || typeof body.option !== "string" || !poll.options.includes(body.option)) return send(response, 200, view());
            if (selected !== null && selected !== body.option) return send(response, 200, view());
            selected = body.option;
            return send(response, 200, view());
        }
        if (request.method === "POST" && url.pathname === "/polls/poll_1/close" && poll) {
            poll.status = "closed";
            return send(response, 200, view());
        }
        send(response, 404, { error: "not found" });
    } catch (_error) { send(response, 400, { error: "invalid request" }); }
}).listen(Number(process.env.PORT || 8000), "0.0.0.0");
