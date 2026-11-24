import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class Main {
    static class Poll {
        String title;
        List<String> options;
        String status = "open";
        Poll(String title, List<String> options) { this.title = title; this.options = options; }
    }

    static Poll poll;
    static String selected;

    static String escape(String value) {
        return value.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n");
    }

    static String stringField(String body, String field) {
        Matcher matcher = Pattern.compile("\\\"" + field + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"").matcher(body);
        return matcher.find() ? matcher.group(1) : null;
    }

    static List<String> optionsField(String body) {
        Matcher matcher = Pattern.compile("\\\"options\\\"\\s*:\\s*\\[(.*?)\\]").matcher(body);
        if (!matcher.find()) return null;
        Matcher items = Pattern.compile("\\\"([^\\\"]*)\\\"").matcher(matcher.group(1));
        List<String> result = new ArrayList<>();
        while (items.find()) result.add(items.group(1).trim());
        return result;
    }

    static String view() {
        List<Integer> order = new ArrayList<>();
        for (int index = 0; index < poll.options.size(); index++) order.add(index);
        order.sort(Comparator.comparingInt((Integer index) -> poll.options.get(index).equals(selected) ? -1 : 0).thenComparingInt(index -> index));
        StringBuilder results = new StringBuilder("[");
        for (int position = 0; position < order.size(); position++) {
            if (position > 0) results.append(',');
            String option = poll.options.get(order.get(position));
            results.append("{\"option\":\"").append(escape(option)).append("\",\"votes\":").append(option.equals(selected) ? 1 : 0).append('}');
        }
        results.append(']');
        return "{\"id\":\"poll_1\",\"title\":\"" + escape(poll.title) + "\",\"status\":\"" + poll.status + "\",\"results\":" + results + ",\"total_votes\":" + (selected == null ? 0 : 1) + "}";
    }

    static void send(HttpExchange exchange, int status, String body) throws IOException {
        byte[] data = body.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().set("Content-Type", "application/json; charset=utf-8");
        exchange.sendResponseHeaders(status, data.length);
        exchange.getResponseBody().write(data);
        exchange.close();
    }

    static synchronized void handle(HttpExchange exchange) throws IOException {
        try {
            String method = exchange.getRequestMethod();
            String path = exchange.getRequestURI().getPath();
            if (method.equals("GET") && path.equals("/health")) {
                send(exchange, 200, "{\"status\":\"ok\"}");
                return;
            }
            String body = new String(exchange.getRequestBody().readAllBytes(), StandardCharsets.UTF_8);
            if (method.equals("POST") && path.equals("/reset")) {
                poll = null;
                selected = null;
                send(exchange, 200, "{\"ok\":true}");
                return;
            }
            if (method.equals("POST") && path.equals("/polls")) {
                String title = stringField(body, "title");
                List<String> options = optionsField(body);
                if (title == null || title.trim().isEmpty() || options == null || options.size() < 2 || options.size() > 8 || options.stream().anyMatch(String::isEmpty)) {
                    send(exchange, 400, "{\"error\":\"invalid request\"}");
                    return;
                }
                poll = new Poll(title.trim(), options);
                selected = null;
                send(exchange, 200, view());
                return;
            }
            if (method.equals("PUT") && path.matches("/polls/poll_1/votes/[A-Za-z0-9_-]+") && poll != null) {
                String option = stringField(body, "option");
                if (!poll.status.equals("open") || option == null || !poll.options.contains(option)) {
                    send(exchange, 200, view());
                    return;
                }
                if (selected != null && !selected.equals(option)) {
                    send(exchange, 200, view());
                    return;
                }
                selected = option;
                send(exchange, 200, view());
                return;
            }
            if (method.equals("POST") && path.equals("/polls/poll_1/close") && poll != null) {
                poll.status = "closed";
                send(exchange, 200, view());
                return;
            }
            send(exchange, 404, "{\"error\":\"not found\"}");
        } catch (Exception error) {
            send(exchange, 400, "{\"error\":\"invalid request\"}");
        }
    }

    public static void main(String[] args) throws Exception {
        int port = Integer.parseInt(System.getenv().getOrDefault("PORT", "8000"));
        HttpServer server = HttpServer.create(new InetSocketAddress("0.0.0.0", port), 0);
        server.createContext("/", Main::handle);
        server.setExecutor(java.util.concurrent.Executors.newCachedThreadPool());
        server.start();
    }
}
