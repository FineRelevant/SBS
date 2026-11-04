#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <list>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "esp_http_server.h"

namespace internet
{
    class HttpServer final 
    {
    public:
        using Handler = std::move_only_function<esp_err_t(httpd_req_t*)>;
        
        HttpServer() = default;
        ~HttpServer() noexcept;

        HttpServer(const HttpServer&) = delete;
        HttpServer& operator=(const HttpServer&) = delete;

        HttpServer(HttpServer&& other) noexcept;

        HttpServer& operator=(HttpServer&& other) = delete;

        [[nodiscard]]
        esp_err_t start(httpd_config_t config = HTTPD_DEFAULT_CONFIG()) noexcept;

        [[nodiscard]]
        esp_err_t stop() noexcept;

        [[nodiscard]]
        bool is_running() const noexcept;

        [[nodiscard]]
        esp_err_t register_handler(
            std::string uri, 
            httpd_method_t metod, 
            Handler handler) noexcept;
        
        [[nodiscard]]
        esp_err_t unregister_handler(
            std::string_view uri,
            httpd_method_t method) noexcept;

        [[nodiscard]] httpd_handle_t native_handle() const noexcept;
    private:
        struct Route
        {
            std::string uri;
            httpd_method_t method;
            Handler handler;
        };
        
        httpd_handle_t handle_{nullptr};

        std::list<Route> routes_;

        static esp_err_t dispatch(httpd_req_t* req);
    };
} // namespace internet