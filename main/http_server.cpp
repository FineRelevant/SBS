#include "http_server.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace internet
{
    struct HttpServer::State final
        : std::enable_shared_from_this<HttpServer::State> 
    {

        struct RouteContext final 
        {
            std::uint64_t id{};
            std::string uri;
            httpd_method_t method{HTTP_GET};
            Handler handler;

            // Whether ESP-IDF currently has this handler registered.
            bool registered{false};

            // Removal was requested while stop() was in progress.
            bool remove_pending{false};
        };

        enum class Lifecycle 
        {
            stopped,
            running,
            stopping
        };

        mutable std::mutex mutex;
        httpd_handle_t server{nullptr};
        Lifecycle lifecycle{Lifecycle::stopped};

        std::uint64_t next_id{1};
        std::vector<std::unique_ptr<RouteContext>> routes;

        ~State() {
            if (stop() != ESP_OK) 
            {
                // A failed stop means ESP-IDF may still reference route
                // contexts. Leak those contexts rather than leave dangling
                // user_ctx pointers. This is a last-resort failure path.
                std::lock_guard lock(mutex);

                for (auto& route : routes) 
                {
                    (void)route.release();
                }
            }
        }

        static esp_err_t dispatch(httpd_req_t* req) 
        {
            auto* const route =
                static_cast<RouteContext*>(req->user_ctx);

            if (route == nullptr || !route->handler) 
            {
                return ESP_FAIL;
            }

            return route->handler(req);
        }

        [[nodiscard]]
        esp_err_t start(const httpd_config_t& config) 
        {
            std::lock_guard lock(mutex);

            if (lifecycle != Lifecycle::stopped || server != nullptr) 
            {
                return ESP_ERR_INVALID_STATE;
            }

            const esp_err_t err = httpd_start(&server, &config);

            if (err == ESP_OK) 
            {
                lifecycle = Lifecycle::running;
            } 
            else 
            {
                server = nullptr;
            }

            return err;
        }

        [[nodiscard]]
        esp_err_t stop() noexcept 
        {
            httpd_handle_t handle{};

            {
                std::lock_guard lock(mutex);

                if (lifecycle == Lifecycle::stopped) 
                {
                    return ESP_OK;
                }

                if (lifecycle == Lifecycle::stopping)
                {
                    return ESP_ERR_INVALID_STATE;
                }

                lifecycle = Lifecycle::stopping;
                handle = server;
            }

            // Do not hold mutex here: httpd_stop() may wait for an
            // active handler, and that handler may need this mutex.
            const esp_err_t err = httpd_stop(handle);

            {
                std::lock_guard lock(mutex);

                if (err == ESP_OK) 
                {
                    server = nullptr;
                    lifecycle = Lifecycle::stopped;

                    // Safe only after successful server shutdown.
                    routes.clear();
                    return ESP_OK;
                }

                // Shutdown failed. The server and contexts must remain
                // alive. Restore the running state and process deferred
                // removals where possible.
                lifecycle = Lifecycle::running;

                for (auto& route : routes) 
                {
                    if (!route->remove_pending) 
                    {
                        continue;
                    }

                    route->remove_pending = false;

                    if (!route->registered) 
                    {
                        continue;
                    }

                    const esp_err_t unregister_err =
                        httpd_unregister_uri_handler(
                            server,
                            route->uri.c_str(),
                            route->method);

                    if (unregister_err == ESP_OK ||
                        unregister_err == ESP_ERR_NOT_FOUND) 
                    {
                        route->registered = false;
                    }
                }
            }

            return err;
        }

        [[nodiscard]]
        bool running() const noexcept 
        {
            std::lock_guard lock(mutex);
            return lifecycle == Lifecycle::running &&
                server != nullptr;
        }

        [[nodiscard]]
        esp_err_t register_handler(
            std::string uri,
            httpd_method_t method,
            Handler handler,
            Route& out)
        {
            if (uri.empty() || !handler) 
            {
                return ESP_ERR_INVALID_ARG;
            }

            std::lock_guard lock(mutex);

            if (lifecycle != Lifecycle::running || server == nullptr) 
            {
                return ESP_ERR_INVALID_STATE;
            }

            if (next_id == std::numeric_limits<std::uint64_t>::max()) 
            {
                return ESP_ERR_NO_MEM;
            }

            auto context = std::make_unique<RouteContext>(
                RouteContext{
                    .id = next_id++,
                    .uri = std::move(uri),
                    .method = method,
                    .handler = std::move(handler),
                    .registered = false,
                    .remove_pending = false
                });

            auto* const raw = context.get();
            const auto id = raw->id;

            // Allocate and retain the context before registering it.
            // Its address remains stable even if the vector reallocates.
            routes.push_back(std::move(context));

            httpd_uri_t endpoint{};
            endpoint.uri = raw->uri.c_str();
            endpoint.method = raw->method;
            endpoint.handler = &State::dispatch;
            endpoint.user_ctx = raw;

            const esp_err_t err =
                httpd_register_uri_handler(server, &endpoint);

            if (err != ESP_OK) 
            {
                routes.pop_back();
                return err;
            }

            raw->registered = true;

            out = Route{weak_from_this(), id};
            return ESP_OK;
        }

        [[nodiscard]]
        esp_err_t unregister_handler(std::uint64_t id) noexcept 
        {
            std::lock_guard lock(mutex);

            const auto it = std::ranges::find_if(
                routes,
                [id](const auto& route) {
                    return route->id == id;
                });

            if (it == routes.end()) 
            {
                return ESP_OK;
            }

            auto& route = **it;

            if (lifecycle == Lifecycle::stopping) 
            {
                route.remove_pending = true;
                return ESP_OK;
            }

            if (!route.registered) 
            {
                return ESP_OK;
            }

            if (server == nullptr) 
            {
                return ESP_ERR_INVALID_STATE;
            }

            const esp_err_t err =
                httpd_unregister_uri_handler(
                    server,
                    route.uri.c_str(),
                    route.method);

            if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) 
            {
                // Keep the context alive until a successful stop.
                route.registered = false;
                return ESP_OK;
            }

            return err;
        }

        [[nodiscard]]
        bool contains(std::uint64_t id) const noexcept 
        {
            std::lock_guard lock(mutex);

            const auto it = std::ranges::find_if(
                routes,
                [id](const auto& route) {
                    return route->id == id;
                });

            return it != routes.end() && (*it)->registered;
        }
    };

    HttpServer::Route::Route(
        std::weak_ptr<State> state,
        std::uint64_t id) noexcept
        : state_(std::move(state)), id_(id) {}

    HttpServer::Route::Route(Route&& other) noexcept
        : state_(std::move(other.state_)),
        id_(std::exchange(other.id_, 0)) {}

    HttpServer::Route& HttpServer::Route::operator=(
        Route&& other) noexcept {
        if (this != &other) 
        {
            reset();
            state_ = std::move(other.state_);
            id_ = std::exchange(other.id_, 0);
        }

        return *this;
    }

    HttpServer::Route::~Route() 
    {
        reset();
    }

    void HttpServer::Route::reset() noexcept 
    {
        if (id_ == 0) 
        {
            state_.reset();
            return;
        }

        if (auto state = state_.lock()) 
        {
            (void)state->unregister_handler(id_);
        }

        id_ = 0;
        state_.reset();
    }

    HttpServer::Route::operator bool() const noexcept 
    {
        if (id_ == 0) 
        {
            return false;
        }

        if (auto state = state_.lock()) 
        {
            return state->contains(id_);
        }

        return false;
    }

    HttpServer::HttpServer()
        : state_(std::make_shared<State>()) {}

    HttpServer::HttpServer(const httpd_config_t& config)
        : HttpServer() 
    {
        config_ = config;
    }

    HttpServer::HttpServer(HttpServer&&) noexcept = default;

    HttpServer& HttpServer::operator=(HttpServer&&) noexcept = default;

    HttpServer::~HttpServer() = default;

    std::expected<void, esp_err_t> HttpServer::start() 
    {
        if (!state_) 
        {
            return ESP_ERR_INVALID_STATE;
        }

        return state_->start(config_);
    }

    esp_err_t HttpServer::stop() noexcept 
    {
        return state_ ? state_->stop() : ESP_OK;
    }

    bool HttpServer::running() const noexcept 
    {
        return state_ && state_->running();
    }

    esp_err_t HttpServer::register_handler(
        std::string uri,
        httpd_method_t method,
        Handler handler,
        Route& out) 
    {

        if (!state_ || uri.empty() || !handler) 
        {
            return ESP_ERR_INVALID_ARG;
        }

        out.reset();

        return state_->register_handler(
            std::move(uri),
            method,
            std::move(handler),
            out);
    }
} // namespace internet