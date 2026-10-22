#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <expected>

#include "esp_err.h"
#include "esp_http_server.h"

namespace internet
{
    class HttpServer final {
    private:
        struct State;

    public:
        using Handler = std::function<esp_err_t(httpd_req_t*)>;

        class Route final {
        public:
            Route() = default;

            Route(const Route&) = delete;
            Route& operator=(const Route&) = delete;

            Route(Route&& other) noexcept;
            Route& operator=(Route&& other) noexcept;

            ~Route();

            void reset() noexcept;

            [[nodiscard]]
            explicit operator bool() const noexcept;

        private:
            friend class HttpServer;

            Route(std::weak_ptr<State> state, std::uint64_t id) noexcept;

            std::weak_ptr<State> state_;
            std::uint64_t id_{};
        };

        HttpServer();
        explicit HttpServer(const httpd_config_t& config);

        HttpServer(const HttpServer&) = delete;
        HttpServer& operator=(const HttpServer&) = delete;

        HttpServer(HttpServer&&) noexcept;
        HttpServer& operator=(HttpServer&&) noexcept;

        ~HttpServer();

        [[nodiscard]]
        std::expected<void, esp_err_t> start();

        [[nodiscard]]
        std::expected<void, esp_err_t> stop() noexcept;

        [[nodiscard]]
        bool running() const noexcept;

        [[nodiscard]]
        std::expected<Route, esp_err_t> register_handler(
            std::string uri,
            httpd_method_t method,
            Handler handler);

    private:
        std::shared_ptr<State> state_;
        httpd_config_t config_ = HTTPD_DEFAULT_CONFIG();
    };
} // namespace internet