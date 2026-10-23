#include <iiWebServer/HttpServer.h>
#include <vector>

namespace iiWebServer {
namespace {
auto failure(ii::network::ErrorCode code, std::string message) {
    return std::unexpected(ii::network::Error{code, ii::network::Layer::application, std::move(message)});
}
}
struct HttpServer::Impl : std::enable_shared_from_this<HttpServer::Impl> {
    ii::network::HttpServer server;
    ii::network::HttpRouter router;
    std::vector<Middleware> middleware;
    std::string error;
    explicit Impl(ii::runtime::EventLoop& loop) : server(loop) {}
    bool active() const { return server.listening() || server.draining() || server.active_connections(); }
    HttpResponse dispatch(std::size_t index, const HttpRequest& request) {
        if (index == middleware.size()) return router.dispatch(request);
        const auto self = shared_from_this();
        Next next = [self, index](const HttpRequest& forwarded) { return self->dispatch(index + 1, forwarded); };
        return middleware[index](request, next);
    }
    ii::network::HttpServer::Handler handler() {
        return [weak = weak_from_this()](const HttpRequest& request) {
            const auto self = weak.lock();
            return self ? self->dispatch(0, request) : HttpResponse{503, {}, "Application is unavailable"};
        };
    }
};
HttpServer::HttpServer(ii::runtime::EventLoop& loop) : d(std::make_shared<Impl>(loop)) {}
HttpServer::~HttpServer() { d->server.close(); }
ii::network::Result<void> HttpServer::route(std::string method, std::string path, Handler handler) {
    if (d->active()) return failure(ii::network::ErrorCode::invalid_state, "Configure routes before listening");
    return d->router.add(std::move(method), std::move(path), std::move(handler));
}
ii::network::Result<void> HttpServer::use(Middleware middleware) {
    if (d->active()) return failure(ii::network::ErrorCode::invalid_state, "Configure middleware before listening");
    if (!middleware) return failure(ii::network::ErrorCode::invalid_argument, "Middleware is empty");
    if (d->middleware.size() == 64) return failure(ii::network::ErrorCode::too_large, "Middleware chain exceeds 64 entries");
    d->middleware.push_back(std::move(middleware)); return {};
}
bool HttpServer::listen(ii::network::IpEndpoint endpoint, HttpLimits limits) {
    if (d->active()) { d->error = "HTTP server is already active"; return false; }
    d->error.clear(); return d->server.listen(std::move(endpoint), d->handler(), limits);
}
bool HttpServer::listenLocal(ii::network::LocalEndpoint endpoint, HttpLimits limits) {
    if (d->active()) { d->error = "HTTP server is already active"; return false; }
    d->error.clear(); return d->server.listen_local(std::move(endpoint), d->handler(), limits);
}
ii::network::Result<void> HttpServer::shutdown(std::chrono::milliseconds grace) { return d->server.shutdown(grace); }
void HttpServer::close() { d->server.close(); }
bool HttpServer::listening() const { return d->server.listening(); }
bool HttpServer::draining() const { return d->server.draining(); }
std::uint16_t HttpServer::port() const { return d->server.port(); }
std::size_t HttpServer::activeConnections() const { return d->server.active_connections(); }
std::string HttpServer::localPath() const { return d->server.local_path(); }
std::string HttpServer::error() const { return d->error.empty() ? d->server.error() : d->error; }
} // namespace iiWebServer
