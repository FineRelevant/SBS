
#include "http_server.hpp"

#include <mutex>
#include <vector>

struct HttpServer::State final
    : std::enable_shared_from_this<HttpServer::State> {

    enum class Lifecycle {
        stopped,
        running,
        stopping
    };

    struct RouteContext final {
        std::string uri;
        httpd_method_t method;
        Handler handler;
        bool registered{false};
        bool pending_unregister{false};
    };

    mutable std::mutex mutex;
    httpd_handle_t server{nullptr};
    Lifecycle lifecycle{Lifecycle::stopped};

    // Context addresses remain stable even when the vector reallocates.
    // Contexts are retained until successful server shutdown.
    std::vector<std::unique_ptr<RouteContext>> routes;

    ~State() {
        if (stop() != ESP_OK) {
            // Preserve callback contexts if shutdown failed. Releasing
            // ownership intentionally avoids dangling user_ctx pointers.
            // This is a last-resort safety measure; it leaks resources.
            for (auto& route : routes) {
                (void)route.release();
            }
        }
    }

    static esp_err_t dispatch(httpd_req_t* req) {
        auto* route = static_cast<RouteContext*>(req->user_ctx);

        if (route == nullptr || !route->handler) {
            return ESP_FAIL;
        }

        return route->handler(req);
    }

    [[nodiscard]] esp_err_t start(const httpd_config_t& config) {
        std::lock_guard lock(mutex);

        if (lifecycle != Lifecycle::stopped || server != nullptr) {
            return ESP_ERR_INVALID_STATE;
        }

        const esp_err_t err = httpd_start(&server, &config);

        if (err == ESP_OK) {
            lifecycle = Lifecycle::running;
        } else {
            server = nullptr;
        }

        return err;
    }

    [[nodiscard]] esp_err_t stop() noexcept {
        httpd_handle_t handle{nullptr};

        {
            std::lock_guard lock(mutex);

            if (lifecycle == Lifecycle::stopped) {
                return ESP_OK;
            }

            if (lifecycle == Lifecycle::stopping) {
                return ESP_ERR_INVALID_STATE;
            }

            lifecycle = Lifecycle::stopping;
            handle = server;
        }

        // Do not hold mutex while stopping: shutdown may wait for
        // active handlers, and handlers may call wrapper operations.
        const esp_err_t err = httpd_stop(handle);

        std::lock_guard lock(mutex);

        if (err == ESP_OK) {
            server = nullptr;
            routes.clear();
            lifecycle = Lifecycle::stopped;
            return ESP_OK;
        }

        // Shutdown failed. The server may still be running, so retain
        // every context. Restore operation and process deferred removals.
        lifecycle = Lifecycle::running;

        for (auto& route : routes) {
            if (!route->pending_unregister || !route->registered) {
                continue;
            }

            const esp_err_t unregister_err =
                httpd_unregister_uri_handler(
                    server,
                    route->uri.c_str(),
                    route->method);

            if (unregister_err == ESP_OK ||
                unregister_err == ESP_ERR_NOT_FOUND) {
                route->registered = false;
                route->pending_unregister = false;
            }
        }

        return err;
    }

    [[nodiscard]] bool running() const noexcept {
        std::lock_guard lock(mutex);
        return lifecycle == Lifecycle::running && server != nullptr;
    }

    [[nodiscard]] esp_err_t register_handler(
        std::string uri,
        httpd_method_t method,
        Handler handler,
        Route& out)
    {
        std::lock_guard lock(mutex);

        if (lifecycle != Lifecycle::running || server == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }

        auto context = std::make_unique<RouteContext>(
            RouteContext{
                .uri = std::move(uri),
                .method = method,
                .handler = std::move(handler),
                .registered = false,
                .pending_unregister = false
            });

        auto* const raw = context.get();

        // Keep the context alive before handing its pointer to ESP-IDF.
        routes.push_back(std::move(context));

        httpd_uri_t endpoint{};
        endpoint.uri = raw->uri.c_str();
        endpoint.method = raw->method;
        endpoint.handler = &State::dispatch;
        endpoint.user_ctx = raw;

        const esp_err_t err =
            httpd_register_uri_handler(server, &endpoint);

        if (err != ESP_OK) {
            routes.pop_back();
            return err;
        }

        raw->registered = true;

        out = Route{
            weak_from_this(),
            raw->uri,
            raw->method
        };

        return ESP_OK;
    }

    [[nodiscard]] esp_err_t unregister_handler(
        const std::string& uri,
        httpd_method_t method) noexcept
    {
        std::lock_guard lock(mutex);

        // Locate the exact registration. Do not assume a URI/method
        // pair uniquely identifies a context without checking the state.
        RouteContext* context = nullptr;

        for (auto& route : routes) {
            if (route->uri == uri && route->method == method) {
                context = route.get();
                break;
            }
        }

        if (context == nullptr || !context->registered) {
            return ESP_OK;
        }

        if (lifecycle == Lifecycle::stopping) {
            // The stop operation will remove all registrations if it
            // succeeds. Retry this removal if shutdown fails.
            context->pending_unregister = true;
            return ESP_OK;
        }

        if (lifecycle != Lifecycle::running || server == nullptr) {
            return ESP_OK;
        }

        const esp_err_t err =
            httpd_unregister_uri_handler(server, uri.c_str(), method);

        if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) {
            context->registered = false;
            context->pending_unregister = false;
            return ESP_OK;
        }

        return err;
    }
};

HttpServer::Route::Route(
    std::weak_ptr<State> state,
    std::string uri,
    httpd_method_t method)
    : state_(std::move(state)),
      uri_(std::move(uri)),
      method_(method) {}

HttpServer::Route::Route(Route&& other) noexcept
    : state_(std::move(other.state_)),
      uri_(std::move(other.uri_)),
      method_(other.method_) {
    other.uri_.clear();
}

HttpServer::Route& HttpServer::Route::operator=(Route&& other) noexcept {
    if (this != &other) {
        reset();

        state_ = std::move(other.state_);
        uri_ = std::move(other.uri_);
        method_ = other.method_;

        other.uri_.clear();
    }

    return *this;
}

HttpServer::Route::~Route() {
    reset();
}

void HttpServer::Route::reset() noexcept {
    if (!uri_.empty()) {
        if (auto state = state_.lock()) {
            (void)state->unregister_handler(uri_, method_);
        }
    }

    uri_.clear();
    state_.reset();
}

HttpServer::Route::operator bool() const noexcept {
    return !state_.expired() && !uri_.empty();
}

HttpServer::HttpServer()
    : state_(std::make_shared<State>()) {}

HttpServer::HttpServer(const httpd_config_t& config)
    : state_(std::make_shared<State>()),
      config_(config) {}

HttpServer::HttpServer(HttpServer&&) noexcept = default;

HttpServer& HttpServer::operator=(HttpServer&&) noexcept = default;

HttpServer::~HttpServer() = default;

esp_err_t HttpServer::start() {
    return state_ ? state_->start(config_) : ESP_ERR_INVALID_STATE;
}

esp_err_t HttpServer::stop() noexcept {
    return state_ ? state_->stop() : ESP_OK;
}

bool HttpServer::running() const noexcept {
    return state_ && state_->running();
}

esp_err_t HttpServer::register_handler(
    std::string uri,
    httpd_method_t method,
    Handler handler,
    Route& out)
{
    out.reset();

    if (!state_ || uri.empty() || !handler) {
        return ESP_ERR_INVALID_ARG;
    }

    return state_->register_handler(
        std::move(uri), method, std::move(handler), out);
}
