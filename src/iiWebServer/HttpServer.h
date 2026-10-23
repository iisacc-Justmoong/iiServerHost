#pragma once

#include <iiWebServer/HttpExport.h>
#include <iiNetwork/Server.h>
#include <functional>
#include <memory>

namespace iiWebServer {

using HttpRequest = ii::network::ServerRequest;
using HttpResponse = ii::network::ServerResponse;
using HttpLimits = ii::network::ServerLimits;
using RouteParameters = ii::network::HttpRouter::Parameters;

// Native application HTTP layer. The host owns and drives the Core event loop.
// Handlers and middleware are synchronous and run on that loop's owner thread.
class IIWEBSERVER_HTTP_EXPORT HttpServer final {
public:
    using Handler = ii::network::HttpRouter::Handler;
    using Next = std::function<HttpResponse(const HttpRequest&)>;
    using Middleware = std::function<HttpResponse(const HttpRequest&, const Next&)>;

    explicit HttpServer(ii::runtime::EventLoop& loop);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Configure before listening. Route parameters use /items/{id} syntax.
    ii::network::Result<void> route(std::string method, std::string path, Handler handler);
    ii::network::Result<void> use(Middleware middleware);
    bool listen(ii::network::IpEndpoint endpoint = {ii::network::IpAddress::loopback(), 0}, HttpLimits limits = {});
    bool listenLocal(ii::network::LocalEndpoint endpoint, HttpLimits limits = {});
    // Stop admission and drain existing requests without blocking the host.
    // Keep pumping the event loop until draining() becomes false.
    ii::network::Result<void> shutdown(std::chrono::milliseconds grace = std::chrono::seconds(5));
    void close();
    [[nodiscard]] bool listening() const;
    [[nodiscard]] bool draining() const;
    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] std::size_t activeConnections() const;
    [[nodiscard]] std::string localPath() const;
    [[nodiscard]] std::string error() const;

private:
    struct Impl;
    std::shared_ptr<Impl> d;
};

} // namespace iiWebServer
