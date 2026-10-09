#include "velocita/v2/truemarkets_transport.hpp"

#include "v2_truemarkets_test_support.hpp"
#include "velocita/v2/signer_service.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

using namespace velocita::v2;
using namespace velocita::v2::truemarkets;
using namespace velocita::v2::truemarkets::transport;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string built(const HttpRequest& request) {
    std::array<char, kMaxHttpRequestBytes> out{};
    const auto encoded = build_http_request(request, out);
    return encoded.ok ? std::string(out.data(), encoded.bytes_written) : std::string("<refused>");
}

void requests_are_written_exactly_and_unsafe_text_is_refused() {
    require(built({HttpMethod::post, "mock.local", "/mcp", "tok-1", "{\"id\":7}"}) ==
                "POST /mcp HTTP/1.1\r\nHost: mock.local\r\nUser-Agent: velocita-v2\r\n"
                "Accept: application/json, text/event-stream\r\nConnection: close\r\n"
                "Authorization: Bearer tok-1\r\nContent-Type: application/json\r\nContent-Length: 8\r\n\r\n{\"id\":7}",
            "the POST request bytes changed: " + built({HttpMethod::post, "mock.local", "/mcp", "tok-1", "{\"id\":7}"}));
    require(built({HttpMethod::get, "mock.local", "/v1/conductor/orders/ord-1/status", "", ""}) ==
                "GET /v1/conductor/orders/ord-1/status HTTP/1.1\r\nHost: mock.local\r\nUser-Agent: velocita-v2\r\n"
                "Accept: application/json, text/event-stream\r\nConnection: close\r\n\r\n",
            "the GET request bytes changed");
    const std::string long_body(kMaxTmRequestBytes + 1, 'x');
    for (const auto& [name, request] : std::vector<std::pair<const char*, HttpRequest>>{
             {"a path with a line break", {HttpMethod::get, "h", "/a\r\nX-Injected: 1", "", ""}},
             {"a path with a space", {HttpMethod::get, "h", "/a b", "", ""}},
             {"a path that is not absolute", {HttpMethod::get, "h", "orders", "", ""}},
             {"an empty path", {HttpMethod::get, "h", "", "", ""}},
             {"a host with a line break", {HttpMethod::get, "h\r\nHost: evil", "/a", "", ""}},
             {"an empty host", {HttpMethod::get, "", "/a", "", ""}},
             {"a token with a line break", {HttpMethod::get, "h", "/a", "t\r\nX: 1", ""}},
             {"a body on a GET", {HttpMethod::get, "h", "/a", "", "x"}},
             {"a body over the limit", {HttpMethod::post, "h", "/a", "", long_body}},
             {"an unknown method", {static_cast<HttpMethod>(3), "h", "/a", "", ""}}}) {
        require(built(request) == "<refused>", std::string("a request was built with ") + name);
    }
    std::array<char, 10> tiny{};
    require(!build_http_request({HttpMethod::get, "h", "/a", "", ""}, tiny).ok, "a request was written past its buffer");
}

void responses_are_parsed_by_length_chunks_or_close() {
    const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
    const auto fixed = head + "Content-Length: 5\r\n\r\nhello";
    auto parsed = parse_http_response(fixed, false);
    require(parsed.state == HttpParse::complete && parsed.status == 200 && parsed.body == "hello",
            "a response with a content length did not parse");
    for (std::size_t cut = 0; cut < fixed.size(); ++cut) {
        require(parse_http_response(fixed.substr(0, cut), false).state == HttpParse::incomplete,
                "a response cut at byte " + std::to_string(cut) + " was not incomplete");
        require(parse_http_response(fixed.substr(0, cut), true).state == HttpParse::malformed,
                "a response cut at byte " + std::to_string(cut) + " and closed was accepted");
    }
    require(parse_http_response(fixed + "x", false).state == HttpParse::malformed,
            "bytes after the declared length were accepted");

    const auto chunked = head + "Transfer-Encoding: chunked\r\n\r\n3\r\nhel\r\n2;ext=1\r\nlo\r\n0\r\n\r\n";
    parsed = parse_http_response(chunked, false);
    require(parsed.state == HttpParse::complete && parsed.body == "hello", "a chunked response did not parse");
    require(parse_http_response(chunked.substr(0, chunked.size() - 2), false).state == HttpParse::incomplete &&
                parse_http_response(chunked.substr(0, chunked.size() - 9), true).state == HttpParse::malformed,
            "a cut chunked response was not handled");
    require(parse_http_response(head + "Transfer-Encoding: chunked\r\n\r\nzz\r\nhel\r\n0\r\n\r\n", false).state ==
                    HttpParse::malformed &&
                parse_http_response(head + "Transfer-Encoding: chunked\r\n\r\n3\r\nhelXX0\r\n\r\n", false).state ==
                    HttpParse::malformed,
            "a malformed chunk was accepted");
    parsed = parse_http_response(head + "Transfer-Encoding: chunked\r\n\r\n3\r\nhel\r\n0\r\nX-Trailer: 1\r\n\r\n", false);
    require(parsed.state == HttpParse::complete && parsed.body == "hel", "a chunked response with a trailer did not parse");

    const auto until_close = head + "\r\nhello";
    require(parse_http_response(until_close, false).state == HttpParse::incomplete, "an unbounded body ended early");
    parsed = parse_http_response(until_close, true);
    require(parsed.state == HttpParse::complete && parsed.body == "hello", "a body ended by close did not parse");

    parsed = parse_http_response("HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\n\r\n", false);
    require(parsed.state == HttpParse::complete && parsed.status == 429 && parsed.body.empty(), "a 429 did not parse");
    parsed = parse_http_response("HTTP/1.0 204 No Content\r\n\r\n", false);
    require(parsed.state == HttpParse::complete && parsed.status == 204, "a 204 did not parse");
    for (const char* bad : {"HTTP/2 200 OK\r\n\r\n", "HTTP/1.1 20 OK\r\n\r\n", "HTTP/1.1 2000 OK\r\n\r\n",
                            "HTTP/1.1 100 Continue\r\n\r\n", "HTTP/1.1 abc OK\r\n\r\n", "garbage\r\n\r\n",
                            "HTTP/1.1 200 OK\r\nno-colon\r\n\r\n",
                            "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello",
                            "HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n",
                            "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n",
                            "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n"}) {
        require(parse_http_response(bad, true).state == HttpParse::malformed, std::string("accepted: ") + bad);
    }
    const std::string chunk_body = "5\r\nhello\r\n0\r\n\r\n";
    require(parse_http_response(head + "Content-Length: " + std::to_string(chunk_body.size()) +
                                    "\r\nTransfer-Encoding: chunked\r\n\r\n" + chunk_body, true).state ==
                    HttpParse::malformed,
            "a reply with both a length and chunks was accepted");
    require(parse_http_response(head + "Content-Length: 6\r\nContent-Length: 5\r\n\r\nhello", true).state ==
                    HttpParse::malformed,
            "a reply with two different lengths was accepted");
    require(parse_http_response(head + "Transfer-Encoding: gzip\r\n\r\n" + chunk_body, true).state ==
                    HttpParse::malformed,
            "a transfer encoding other than chunked was read as chunked");
    {
        const std::string half(kMaxHttpBodyBytes / 2 + 1, 'c');
        std::ostringstream size;
        size << std::hex << half.size();
        const auto piece = size.str() + "\r\n" + half + "\r\n";
        require(parse_http_response(head + "Transfer-Encoding: chunked\r\n\r\n" + piece + piece + "0\r\n\r\n", true)
                        .state == HttpParse::too_large,
                "chunks adding up to more than the body limit were accepted");
    }
    require(parse_http_response("HTTP/1.1 200 OK\r\ncontent-LENGTH: 2\r\n\r\nhi", false).body == "hi",
            "header names were matched with case");
    require(parse_http_response(std::string(kMaxHttpHeaderBytes + 1, 'h'), false).state == HttpParse::too_large &&
                parse_http_response("HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(kMaxHttpBodyBytes + 1) +
                                        "\r\n\r\n", false).state == HttpParse::too_large &&
                parse_http_response("HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(kMaxHttpBodyBytes) +
                                        "\r\n\r\n", false).state == HttpParse::incomplete &&
                parse_http_response(head + "\r\n" + std::string(kMaxHttpBodyBytes + 1, 'b'), false).state ==
                    HttpParse::too_large,
            "the size bounds are not enforced exactly");
}

std::string envelope(const std::string& text, const std::uint64_t id, const bool is_error = false) {
    std::string escaped;
    for (const char c : text) {
        if (c == '"' || c == '\\') escaped.push_back('\\');
        escaped.push_back(c);
    }
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"" +
           escaped + "\"}],\"isError\":" + (is_error ? "true" : "false") + "}}";
}

std::pair<EnvelopeVerdict, std::string> unwrapped(const std::string& body, const std::uint64_t id,
                                                  const std::size_t room = 4'096) {
    std::string out(room, '\0');
    const auto result = unwrap_mcp_result(body, id, out);
    return {result.verdict, out.substr(0, result.length)};
}

void the_envelope_yields_exactly_the_tool_text() {
    const std::string document = R"({"quote_id":"qt-1","qty":"0.2","note":"a \"b\" c\\d"})";
    require(unwrapped(envelope(document, 7), 7) == std::pair{EnvelopeVerdict::completed, document},
            "the tool text was not recovered byte for byte");
    require(unwrapped(envelope(document, 7, true), 7) == std::pair{EnvelopeVerdict::tool_error, document},
            "a tool error was not reported as one");
    require(unwrapped("event: message\r\ndata: " + envelope(document, 7) + "\r\n\r\n", 7) ==
                std::pair{EnvelopeVerdict::completed, document},
            "an event-stream reply was not read");
    require(unwrapped("data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\ndata: " +
                          envelope(document, 7) + "\n", 7).first == EnvelopeVerdict::completed,
            "a notification before the reply hid the reply");
    require(unwrapped(R"({"jsonrpc":"2.0","id":7,"error":{"code":-32000,"message":"no"}})", 7) ==
                std::pair{EnvelopeVerdict::tool_error, std::string(R"({"code":-32000,"message":"no"})")},
            "a JSON-RPC error was not reported with its object");
    require(unwrapped(R"({"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"text","text":"a\/b\nc\tdA"}]}})", 7) ==
                std::pair{EnvelopeVerdict::completed, std::string("a/b\nc\tdA")},
            "escapes were not decoded");
    require(unwrapped(R"({"jsonrpc":"2.0","id":7,"error":{"code":-32000,"message":"no"}})", 7, 8).first ==
                EnvelopeVerdict::malformed,
            "an error object that does not fit was reported as an empty tool error");
    const std::vector<std::pair<const char*, std::string>> refused{
        {"another id", envelope(document, 8)},
        {"no id", R"({"jsonrpc":"2.0","result":{"content":[{"type":"text","text":"x"}]}})"},
        {"a string id", R"({"jsonrpc":"2.0","id":"7","result":{"content":[{"type":"text","text":"x"}]}})"},
        {"another protocol version", R"({"jsonrpc":"1.0","id":7,"result":{"content":[{"type":"text","text":"x"}]}})"},
        {"no result", R"({"jsonrpc":"2.0","id":7})"},
        {"a result that is not an object", R"({"jsonrpc":"2.0","id":7,"result":"x"})"},
        {"no content", R"({"jsonrpc":"2.0","id":7,"result":{"isError":false}})"},
        {"empty content", R"({"jsonrpc":"2.0","id":7,"result":{"content":[]}})"},
        {"content that is not text", R"({"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"image","text":"x"}]}})"},
        {"text that is not a string", R"({"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"text","text":5}]}})"},
        {"isError that is not a boolean", R"({"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"text","text":"x"}],"isError":1}})"},
        {"a non-ASCII escape", R"({"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"text","text":"é"}]}})"},
        {"an unknown escape", R"({"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"text","text":"\q"}]}})"},
        {"a cut unicode escape", R"({"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"text","text":"\u00"}]}})"},
        {"bytes after the document", envelope(document, 7) + "x"},
        {"an empty body", ""},
        {"plain text", "upstream error"},
        {"an event stream with no data", "event: message\n\n"},
    };
    for (const auto& [name, body] : refused)
        require(unwrapped(body, 7).first == EnvelopeVerdict::malformed, std::string("accepted an envelope with ") + name);
    require(unwrapped(envelope(document, 7), 7, document.size()).first == EnvelopeVerdict::completed &&
                unwrapped(envelope(document, 7), 7, document.size() - 1).first == EnvelopeVerdict::malformed,
            "a document that does not fit was truncated instead of refused");
    require(rpc_id_of(R"({"jsonrpc":"2.0","id":41,"method":"tools/call"})") == std::optional<std::uint64_t>{41} &&
                !rpc_id_of(R"({"jsonrpc":"2.0","method":"tools/call"})") && !rpc_id_of("not json") &&
                !rpc_id_of(R"({"id":"41"})"),
            "the request id was not read strictly");
}

enum class Fault : std::uint8_t { none, close_silently, never_answer, wrong_id, event_stream, chunked, garbage, http_500 };

struct MockVenueServer {
    tm_test::FakeTrueMarkets fake;
    std::deque<Fault> faults;
    std::vector<std::string> seen;
    std::mutex mutex;
    int listener{-1};
    std::uint16_t port{};
    std::atomic<bool> stop{false};
    std::thread thread;

    MockVenueServer() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const int reuse = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        require(listener >= 0 && ::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
                    ::listen(listener, 8) == 0,
                "the mock venue could not listen on the loopback address");
        socklen_t size = sizeof(address);
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size);
        port = ntohs(address.sin_port);
        thread = std::thread([this] { run(); });
    }
    ~MockVenueServer() {
        stop.store(true);
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        thread.join();
    }

    static std::string reply(const unsigned status, const std::string& body, const bool chunked = false) {
        std::string text = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\n";
        if (!chunked) return text + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        std::ostringstream chunks;
        const auto half = body.size() / 2;
        chunks << std::hex << half << "\r\n" << body.substr(0, half) << "\r\n" << (body.size() - half) << "\r\n"
               << body.substr(half) << "\r\n0\r\n\r\n";
        return text + "Transfer-Encoding: chunked\r\n\r\n" + chunks.str();
    }

    void run() {
        while (!stop.load()) {
            const int connection = ::accept(listener, nullptr, nullptr);
            if (connection < 0) break;
            std::string request;
            std::array<char, 4'096> chunk{};
            std::size_t body_at = std::string::npos, expected = 0;
            while (body_at == std::string::npos || request.size() < body_at + expected) {
                const auto got = ::recv(connection, chunk.data(), chunk.size(), 0);
                if (got <= 0) break;
                request.append(chunk.data(), static_cast<std::size_t>(got));
                if (body_at == std::string::npos) {
                    const auto end = request.find("\r\n\r\n");
                    if (end == std::string::npos) continue;
                    body_at = end + 4;
                    const auto length = request.find("Content-Length: ");
                    expected = length == std::string::npos ? 0 : std::stoul(request.substr(length + 16));
                }
            }
            Fault fault = Fault::none;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                seen.push_back(request);
                if (!faults.empty()) {
                    fault = faults.front();
                    faults.pop_front();
                }
            }
            const auto answer = respond(request, body_at == std::string::npos ? "" : request.substr(body_at), fault);
            if (fault == Fault::never_answer) {
                for (int waited = 0; waited < 60 && !stop.load(); ++waited)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else if (!answer.empty()) {
                (void)::send(connection, answer.data(), answer.size(), 0);
            }
            ::close(connection);
        }
    }

    std::string respond(const std::string& request, const std::string& body, const Fault fault) {
        if (fault == Fault::close_silently || fault == Fault::never_answer) return {};
        if (fault == Fault::garbage) return "this is not HTTP\r\n\r\n";
        if (fault == Fault::http_500) return reply(500, "{\"message\":\"upstream\"}");
        std::array<char, kMaxTmResponseBytes> inner{};
        if (request.rfind("GET ", 0) == 0) {
            const auto path = request.substr(4, request.find(' ', 4) - 4);
            const auto answered = fake.serve(ApiCall::order_status, path, inner, 0);
            if (answered.status == ApiStatus::rate_limited) return reply(429, "");
            if (answered.status == ApiStatus::no_response) return {};
            return reply(answered.status == ApiStatus::completed ? 200 : 404, std::string(inner.data(), answered.length));
        }
        const auto name = tm_test::field(body, "name");
        const auto call = name == "get_quote" ? ApiCall::get_quote
                        : name == "trade_prepare" ? ApiCall::trade_prepare : ApiCall::trade_execute;
        const auto answered = fake.serve(call, body, inner, 0);
        if (answered.status == ApiStatus::rate_limited) return reply(429, "");
        if (answered.status == ApiStatus::no_response) return {};
        const auto id = *rpc_id_of(body) + (fault == Fault::wrong_id ? 1 : 0);
        const auto wrapped = envelope(std::string(inner.data(), answered.length), id,
                                      answered.status == ApiStatus::tool_error);
        if (fault == Fault::event_stream) return reply(200, "event: message\ndata: " + wrapped + "\n\n");
        return reply(200, wrapped, fault == Fault::chunked);
    }
};

const std::string kQuote =
    R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_quote","arguments":)"
    R"({"chain":"solana","side":"buy","base_asset":"So11111111111111111111111111111111111111112",)"
    R"("qty":"0.2","qty_unit":"base"}}})";

std::pair<ApiStatus, std::string> ask(HttpApi& api, const ApiCall call, const std::string& request) {
    std::array<char, kMaxTmResponseBytes> response{};
    const auto reply = api.call(call, request, response);
    return {reply.status, std::string(response.data(), reply.length)};
}

void calls_reach_a_local_mock_and_every_failure_has_one_meaning() {
    MockVenueServer server;
    LoopbackStream stream(server.port);
    HttpApi api({"mock.local", "/mcp", "test-token", 400}, stream.stream());

    const auto quote = ask(api, ApiCall::get_quote, kQuote);
    require(quote.first == ApiStatus::completed && tm_test::field(quote.second, "quote_id") == "qt-1" &&
                quote.second.find("MOCKTX1;side=buy;base=200000000") != std::string::npos,
            "a quote did not come back through HTTP and the envelope: " + quote.second);
    {
        const std::lock_guard<std::mutex> lock(server.mutex);
        require(server.seen.size() == 1 && server.seen[0].rfind("POST /mcp HTTP/1.1\r\nHost: mock.local\r\n", 0) == 0 &&
                    server.seen[0].find("Authorization: Bearer test-token\r\n") != std::string::npos &&
                    server.seen[0].substr(server.seen[0].find("\r\n\r\n") + 4) == kQuote,
                "the mock did not receive the request as written");
    }
    for (const auto fault : {Fault::event_stream, Fault::chunked}) {
        server.faults.push_back(fault);
        require(ask(api, ApiCall::get_quote, kQuote).first == ApiStatus::completed,
                "a reply as an event stream or in chunks was not read");
    }
    server.fake.scripts[ApiCall::get_quote].push_back(tm_test::Script::tool_error);
    require(ask(api, ApiCall::get_quote, kQuote) ==
                std::pair{ApiStatus::tool_error, std::string("{\"message\":\"internal\"}")},
            "a tool error did not arrive as one with its text");
    server.fake.scripts[ApiCall::get_quote].push_back(tm_test::Script::rate_limited);
    require(ask(api, ApiCall::get_quote, kQuote).first == ApiStatus::rate_limited, "a 429 was not a rate limit");
    server.faults.push_back(Fault::http_500);
    require(ask(api, ApiCall::get_quote, kQuote) ==
                std::pair{ApiStatus::tool_error, std::string("{\"message\":\"upstream\"}")},
            "a 500 was not an answered error");
    for (const auto fault : {Fault::close_silently, Fault::wrong_id, Fault::garbage}) {
        server.faults.push_back(fault);
        const auto answer = ask(api, ApiCall::get_quote, kQuote);
        require(answer.first == ApiStatus::no_response && answer.second.empty(),
                "a closed connection, a reply to another request or a non-HTTP reply produced an answer");
    }
    server.faults.push_back(Fault::never_answer);
    const auto started = std::chrono::steady_clock::now();
    require(ask(api, ApiCall::get_quote, kQuote).first == ApiStatus::no_response, "a silent venue produced an answer");
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    require(waited >= 350 && waited < 2'500, "the call did not honour its 400 ms timeout: " + std::to_string(waited));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    require(ask(api, ApiCall::get_quote, "not a json-rpc request").first == ApiStatus::no_response,
            "a request with no id was sent");

    const auto missing = ask(api, ApiCall::order_status, "/v1/conductor/orders/ord-none/status");
    require(missing.first == ApiStatus::tool_error && missing.second == "{\"type\":\"not_found\"}",
            "a 404 status read was not an answered error with its body: status " +
                std::to_string(static_cast<int>(missing.first)) + " body " + missing.second);
    {
        const std::lock_guard<std::mutex> lock(server.mutex);
        require(server.seen.back().rfind("GET /v1/conductor/orders/ord-none/status HTTP/1.1\r\n", 0) == 0,
                "a status read was not a GET of its path");
    }
    require(ask(api, ApiCall::order_status, "v1/no-leading-slash").first == ApiStatus::no_response,
            "a status path that is not absolute was sent");
    const auto& counters = api.counters();
    require(counters.rate_limited == 1 && counters.http_errors == 2 && counters.malformed_replies == 3 &&
                counters.transport_failures == 3 && counters.calls == 13,
            "the transport counters do not match what happened: rate " + std::to_string(counters.rate_limited) +
                " http " + std::to_string(counters.http_errors) + " malformed " +
                std::to_string(counters.malformed_replies) + " transport " + std::to_string(counters.transport_failures));

    std::uint16_t closed_port = 0;
    {
        MockVenueServer gone;
        closed_port = gone.port;
    }
    LoopbackStream nowhere(closed_port);
    HttpApi unreachable({"mock.local", "/mcp", "", 300}, nowhere.stream());
    require(ask(unreachable, ApiCall::get_quote, kQuote).first == ApiStatus::no_response,
            "a venue that is not listening produced an answer");
}

std::filesystem::path fresh_directory() {
    auto pattern = (std::filesystem::temp_directory_path() / "v2-tm-transport-XXXXXX").string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
    return std::filesystem::canonical(bytes.data());
}

Tick real_clock(void*) noexcept { return monotonic_ms(); }

void an_order_goes_through_http_the_envelope_and_the_signer_socket() {
    const auto directory = fresh_directory();
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{directory};

    std::array<std::byte, signer::kTestKeyBytes> key_bytes{};
    for (std::size_t i = 0; i < key_bytes.size(); ++i) key_bytes[i] = static_cast<std::byte>(i + 1);
    std::ofstream(directory / "test.key", std::ios::binary) << signer::TestKey::file_text(key_bytes);
    auto key = signer::TestKey::load(directory / "test.key");
    require(key.has_value() && signer::FloorFile::create(directory / "floor.bin") == signer::FloorError::none,
            "fixture: signer key or floor");
    signer::FloorFile floor;
    signer::AuditFile audit;
    require(signer::FloorFile::open(directory / "floor.bin", floor) == signer::FloorError::none &&
                audit.open(directory / "audit.log"),
            "fixture: signer files");
    signer::SignerCore core({CallerId{22}, VenueId{9}, AccountId{5}, InstrumentId{4}}, &signer::decode_mock_swap_payload,
                            key->holder(1), audit.sink(), floor.store(), signer::monotonic_clock(), floor.floor());
    signer::SignerServer signer_server;
    require(signer_server.listen(directory / "s.sock", static_cast<std::uint32_t>(::geteuid())), "fixture: signer socket");
    std::atomic<bool> stop{false};
    std::thread signer_thread([&] {
        while (!stop.load()) (void)signer_server.serve_once(core, 20);
    });
    struct Join {
        std::atomic<bool>& stop;
        std::thread& thread;
        ~Join() { stop.store(true); thread.join(); }
    } join{stop, signer_thread};
    signer::SocketSigner signer_client(directory / "s.sock", 2'000);

    MockVenueServer server;
    LoopbackStream stream(server.port);
    HttpApi api({"mock.local", "/mcp", "test-token", 2'000}, stream.stream());

    TrueMarketsVenueConfig config;
    config.spec = {VenueId{9}, InstrumentId{4}, 10'000, 10'000'000, 9};
    config.profile = {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4}, InstrumentSpecVersion{7},
                      MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1), PriceTicks::from_atoms(1'000'000),
                      QuantityLots::from_atoms(1), QuantityLots::from_atoms(100'000)};
    config.flow = ExecutionFlow::quote_then_execute;
    config.chain = Chain::solana;
    config.base_asset = *tm_text("So11111111111111111111111111111111111111112");
    config.quote_asset = *tm_text("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v");
    config.quote_decimals = 6;
    config.max_fee_bps = 120;
    config.quote_ttl = 2'000;
    config.status_interval = 20;
    config.order_rate = {10, 100};
    config.query_rate = {10, 100};
    config.caller = CallerId{22};
    config.session = VenueSessionId{1};
    TrueMarketsVenue adapter(config, api.handle(), signer_client.handle(), TrueMarketsClock{&real_clock, nullptr});
    require(adapter.configured(), "the adapter refused its configuration");

    OrderEffect effect;
    effect.effect_id = EffectId{701};
    effect.cluster_order_id = ClusterOrderId{801};
    effect.committed_directive_index = 1;
    effect.executor_generation = ExecutorGeneration{3};
    effect.kill_epoch = KillEpoch{2};
    effect.venue_id = VenueId{9};
    effect.account_id = AccountId{5};
    effect.instrument_id = InstrumentId{4};
    effect.operation = EffectOperation::submit;
    effect.side = Side::buy;
    effect.price = PriceTicks::from_atoms(12'000);
    effect.quantity = QuantityLots::from_atoms(20);
    effect.time_in_force = TimeInForce::fill_or_kill;
    effect.policy_id = PolicyId{10};
    effect.policy_version = PolicyVersion{1};
    effect.decision_id = DecisionId{20};
    effect.risk_version = RiskVersion{1};
    effect.source_generation = SourceGeneration{1};
    effect.source_sequence = SourceSequence{1};
    effect.payload_digest.bytes[0] = std::byte{0x77};

    require(adapter.send(effect, monotonic_ms()) == sim::SendOutcome::accepted,
            "an order was not accepted through HTTP and the signer socket");
    require(server.fake.calls == std::vector<ApiCall>{ApiCall::get_quote, ApiCall::trade_execute},
            "the venue did not see quote then execute");
    const auto& swap = server.fake.swaps.begin()->second;
    Signature expected;
    const auto holder = key->holder(1);
    const auto payload = server.fake.payload_for(swap);
    require(holder.sign_fn(holder.context, payload, expected) && server.fake.received_signatures.size() == 1 &&
                server.fake.received_signatures[0].find(std::string(expected.view())) != std::string::npos,
            "the venue did not receive the test key's signature of the quoted payload");

    std::array<VenueObservation, 8> observations{};
    std::size_t total = 0;
    bool acknowledged = false, filled = false;
    for (int waited = 0; waited < 300 && !filled; ++waited) {
        const auto count = adapter.poll(std::span(observations).subspan(total), monotonic_ms());
        for (std::size_t i = total; i < total + count; ++i) {
            acknowledged |= observations[i].kind == VenueObservationKind::order_acknowledged;
            filled |= observations[i].kind == VenueObservationKind::fill &&
                      observations[i].quantity == QuantityLots::from_atoms(20) &&
                      observations[i].fee.atoms() == 239'000 && observations[i].price == PriceTicks::from_atoms(11'950);
        }
        total += count;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(acknowledged && filled, "the fill of 20 lots at 11,950 ticks with a 239,000 fee did not come back");
    std::ifstream log(directory / "audit.log");
    std::string line, last;
    std::size_t lines = 0;
    while (std::getline(log, line)) {
        last = line;
        ++lines;
    }
    require(lines == 1 && last.rfind("sequence=1 effect=701 verdict=1 key_version=1 policy_version=1 signature_id=1", 0) == 0,
            "the signer's audit log does not hold the one signature: " + last);
    require(floor.floor() == 1 && api.counters().calls >= 3 && api.counters().transport_failures == 0,
            "the signer's floor did not advance, or a call failed in transport");

    require(adapter.send(effect, monotonic_ms()) == sim::SendOutcome::venue_duplicate,
            "the same order was sent to the venue twice");
}

}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"requests_are_written_exactly_and_unsafe_text_is_refused", requests_are_written_exactly_and_unsafe_text_is_refused},
        {"responses_are_parsed_by_length_chunks_or_close", responses_are_parsed_by_length_chunks_or_close},
        {"the_envelope_yields_exactly_the_tool_text", the_envelope_yields_exactly_the_tool_text},
        {"calls_reach_a_local_mock_and_every_failure_has_one_meaning",
         calls_reach_a_local_mock_and_every_failure_has_one_meaning},
        {"an_order_goes_through_http_the_envelope_and_the_signer_socket",
         an_order_goes_through_http_the_envelope_and_the_signer_socket},
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        try {
            run();
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    if (failures == 0) std::cout << "all True Markets transport tests passed\n";
    return failures == 0 ? 0 : 1;
}
