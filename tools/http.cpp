#include <iiWebServer/HttpServer.h>
#include <charconv>
#include <csignal>
#include <iostream>
#include <stdexcept>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void stop(int) { stop_requested = 1; }
unsigned number(const std::string& value, unsigned maximum) {
    unsigned result = 0; const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result > maximum)
        throw std::runtime_error("Invalid numeric option");
    return result;
}
void pass(ii::network::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
}
int main(int argc, char** argv) {
    try {
        std::string address = "127.0.0.1"; unsigned port = 8080, grace = 5000;
        iiWebServer::HttpLimits limits;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout << "Usage: ii-http-server [--address 127.0.0.1] [--port 8080] [--grace-ms 5000] "
                             "[--max-body-bytes 2097152] [--request-timeout-ms 10000]\n";
                return 0;
            }
            if (i + 1 == argc) throw std::runtime_error("Missing option value");
            const std::string value = argv[++i];
            if (option == "--address") address = value;
            else if (option == "--port") port = number(value, 65535);
            else if (option == "--grace-ms") grace = number(value, 60000);
            else if (option == "--max-body-bytes") limits.body_bytes = number(value, 512U * 1024 * 1024);
            else if (option == "--request-timeout-ms") limits.request_timeout = std::chrono::milliseconds(number(value, 60000));
            else throw std::runtime_error("Unknown option");
        }
        auto ip = ii::network::IpAddress::parse(address);
        if (!ip) throw std::runtime_error(ip.error().message);
        ii::runtime::EventLoop loop(4096); iiWebServer::HttpServer server(loop);
        pass(server.route("GET", "/health", [](const auto&, const auto&) {
            return iiWebServer::HttpResponse{200, {{"Content-Type", "application/json"}}, "{\"status\":\"ok\"}"};
        }));
        pass(server.route("POST", "/api/echo", [](const auto& request, const auto&) {
            return iiWebServer::HttpResponse{200, {{"Content-Type", "application/octet-stream"}}, request.body};
        }));
        if (!server.listen({*ip, static_cast<std::uint16_t>(port)}, limits)) throw std::runtime_error(server.error());
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        bool stopping = false;
        const auto monitor = loop.every(std::chrono::milliseconds(25), [&] {
            if (stop_requested && !stopping) { pass(server.shutdown(std::chrono::milliseconds(grace))); stopping = true; }
            if (stopping && !server.draining()) loop.request_stop();
        });
        if (!monitor) throw std::runtime_error("Cannot schedule shutdown monitor");
        std::cout << "http://" << ii::network::IpEndpoint{*ip, server.port()}.string() << std::endl;
        loop.run(); return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
