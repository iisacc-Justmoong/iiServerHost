#include <iiWebServer/HttpServer.h>
#include <iiNetwork/Transport.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace iiWebServer;
namespace net = ii::network;
using namespace std::chrono_literals;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void pass(net::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
template<class Predicate>
void until(ii::runtime::EventLoop& loop, Predicate predicate, std::chrono::milliseconds budget = 3s) {
    const auto end = ii::runtime::Clock::now() + budget;
    while (!predicate() && ii::runtime::Clock::now() < end) { loop.poll(); std::this_thread::sleep_for(1ms); }
    require(predicate(), "HTTP application test deadline elapsed");
}
net::HttpResponse exchange(ii::runtime::EventLoop& loop, std::uint16_t port, std::string method, std::string target, std::string body = {}, bool authorized = true) {
    net::Http client(loop); net::HttpRequest request;
    request.url = "http://127.0.0.1:" + std::to_string(port) + target;
    request.method = std::move(method); request.body = std::move(body); request.proxy.type = net::ProxyType::disabled;
    request.headers["X-Forwarded-For"] = "203.0.113.9";
    request.headers["X-Forwarded-Proto"] = "https";
    if (authorized) request.headers["Authorization"] = "Bearer test-only";
    bool ready = false; net::HttpResponse response;
    pass(client.request(std::move(request), [&](auto value) { response = std::move(value); ready = true; }));
    until(loop, [&] { return ready; }); require(response.error.empty() && response.status > 0, "HTTP transport failed"); return response;
}
void routing() {
    ii::runtime::EventLoop loop; HttpServer server(loop); std::string order; unsigned calls = 0;
    pass(server.use([&](const HttpRequest& request, const HttpServer::Next& next) {
        order += 'a'; auto response = next(request); order += 'd'; response.headers["X-Middleware"] = "complete"; return response;
    }));
    pass(server.use([&](const HttpRequest& request, const HttpServer::Next& next) {
        order += 'b';
        if (!request.headers.contains("authorization")) return HttpResponse{401, {}, "Unauthorized"};
        auto response = next(request); order += 'c'; return response;
    }));
    pass(server.route("POST", "/items/{id}", [&](const HttpRequest& request, const RouteParameters& parameters) {
        ++calls; require(parameters.at("id") == "hello world", "decoded route parameter");
        require(request.peer_endpoint && request.peer_endpoint->address.is_loopback() && !request.secure, "native connection metadata");
        return HttpResponse{201, {{"Content-Type", "application/octet-stream"}}, request.body};
    }));
    require(!server.route("POST", "/items/{other}", [](const auto&, const auto&) { return HttpResponse{}; }), "duplicate route shape accepted");
    require(!server.use({}), "empty middleware accepted");
    require(server.listen(), "loopback listen");
    require(!server.use([](const auto& r, const auto& next) { return next(r); }), "live middleware mutation accepted");
    const std::string binary("bytes\0with\xff", 11);
    auto response = exchange(loop, server.port(), "POST", "/items/hello%20world?ignored=1", binary);
    require(response.status == 201 && response.body == binary && response.headers.at("x-middleware") == "complete" && order == "abcd", "route/middleware binary response");
    order.clear(); response = exchange(loop, server.port(), "POST", "/items/hello%20world", {}, false);
    require(response.status == 401 && calls == 1 && order == "abd", "middleware short-circuit");
    require(exchange(loop, server.port(), "GET", "/missing").status == 404, "missing route");
    require(exchange(loop, server.port(), "GET", "/items/hello%20world").status == 405, "method mismatch");
    pass(server.shutdown()); require(!server.listening(), "listener still active");
    until(loop, [&] { return !server.draining(); });
}
std::string raw(ii::runtime::EventLoop& loop, std::uint16_t port, std::string bytes) {
    net::Tcp client(loop); std::string response; bool closed = false;
    client.connected = [&] { require(client.write(bytes), "raw request write"); };
    client.readable = [&] { response += client.read(); }; client.disconnected = [&] { closed = true; };
    pass(client.connect({net::IpAddress::loopback(), port})); until(loop, [&] { return closed; }); return response;
}
void protocol() {
    ii::runtime::EventLoop loop; HttpServer server(loop); HttpLimits limits;
    limits.body_bytes = 8; limits.header_bytes = 256; limits.request_timeout = 500ms; unsigned calls = 0;
    pass(server.route("POST", "/", [&](const auto& request, const auto&) { ++calls; return HttpResponse{200, {}, request.body}; }));
    auto invalid = limits; invalid.request_timeout = 0ms;
    require(!server.listen({net::IpAddress::loopback(), 0}, invalid), "unbounded request deadline accepted");
    require(server.listen({net::IpAddress::loopback(), 0}, limits), "bounded listen"); const auto port = server.port();
    require(raw(loop, port, "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 9\r\n\r\n").find("413 Response") != std::string::npos, "body bound");
    require(raw(loop, port, "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").find("400 Response") != std::string::npos, "ambiguous framing");
    const auto largeHeader = raw(loop, port, "POST / HTTP/1.1\r\nHost: localhost\r\nX-Large: " + std::string(260, 'x') + "\r\n\r\n");
    if (largeHeader.find("431 Response") == std::string::npos)
        throw std::runtime_error("header bound response: " + largeHeader);
    server.close(); limits.request_timeout = 35ms;
    require(server.listen({net::IpAddress::loopback(), 0}, limits), "deadline listen");
    require(raw(loop, server.port(), "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 8\r\n\r\nx").find("408 Response") != std::string::npos, "absolute request deadline");
    require(calls == 0, "rejected request reached application");
    server.close();
    HttpServer boundary(loop); HttpLimits responseLimits; responseLimits.response_bytes = 8;
    pass(boundary.route("GET", "/", [](const auto&, const auto&) {
        return HttpResponse{200, {{"X-Large", std::string(65536 - 7, 'x')}}, "12345678"};
    }));
    require(boundary.listen({net::IpAddress::loopback(), 0}, responseLimits), "response boundary listen");
    const auto boundedResponse = raw(loop, boundary.port(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    require(boundedResponse.starts_with("HTTP/1.1 200 ") && boundedResponse.ends_with("12345678"),
            "valid response at header/body limits lost to transport framing overhead");
}
void lifetime() {
    ii::runtime::EventLoop loop; HttpServer server(loop);
    pass(server.route("GET", "/error", [](const auto&, const auto&) -> HttpResponse { throw std::runtime_error("private detail"); }));
    require(server.listen(), "exception listen"); auto response = exchange(loop, server.port(), "GET", "/error");
    require(response.status == 500 && response.body.find("private detail") == std::string::npos, "exception containment");
    server.close();
    auto owned = std::make_unique<HttpServer>(loop);
    pass(owned->route("GET", "/", [&](const auto&, const auto&) { owned.reset(); return HttpResponse{}; }));
    require(owned->listen(), "destruction listen");
    net::Http client(loop); net::HttpRequest request; request.url = "http://127.0.0.1:" + std::to_string(owned->port()) + '/'; request.proxy.type = net::ProxyType::disabled;
    bool done = false; pass(client.request(std::move(request), [&](auto) { done = true; }));
    until(loop, [&] { return done; }); require(!owned, "owner was not destroyed in handler");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected case"); const std::string name = argv[1];
        if (name == "routing") routing(); else if (name == "protocol") protocol(); else if (name == "lifetime") lifetime();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS http." << name << '\n'; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
