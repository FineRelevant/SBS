#include "http_server.hpp"
#include <algorithm>
#include <ranges>
#include <iterator>

namespace internet
{
    HttpServer::~HttpServer() noexcept
    {
        if (handle_ != nullptr)
        {
            (void)stop();
        }
    }

    HttpServer::HttpServer(HttpServer&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)), 
        routes_(std::move(other.routes_))
        {}
    
    esp_err_t HttpServer::start(httpd_config_t config) noexcept
    {
        if (handle_ != nullptr)
        {
            return ESP_ERR_INVALID_STATE;
        }

        config.max_uri_handlers = 20;

        return httpd_start(&handle_, &config);
    }

    esp_err_t HttpServer::stop() noexcept
    {
        if (handle_ == nullptr)
        {
            return ESP_ERR_INVALID_STATE;
        }

        const esp_err_t result = httpd_stop(handle_);

        if (result ==  ESP_OK)
        {
            handle_ = nullptr;
            routes_.clear();
        }

        return result;
    }

    bool HttpServer::is_running() const noexcept
    {
        return handle_ != nullptr;
    }

    esp_err_t HttpServer::register_handler(
        std::string uri, 
        httpd_method_t method, 
        Handler handler) noexcept
    {
        if (handle_ == nullptr)
        {
            return ESP_ERR_INVALID_STATE;
        }
        if (uri.empty() || !handler)
        {
            return ESP_ERR_INVALID_ARG;
        }

        routes_.push_back(Route{
            .uri = std::move(uri),
            .method = method,
            .handler = std::move(handler),
        });

        Route& route = routes_.back();

        httpd_uri_t registration{};
        registration.uri = route.uri.c_str();
        registration.method = route.method;
        registration.handler = &HttpServer::dispatch;
        registration.user_ctx = &route;

        const esp_err_t result = httpd_register_uri_handler(handle_, &registration);

        if (result != ESP_OK)
        {
            routes_.pop_back();
        }

        return result;
    }

    esp_err_t HttpServer::unregister_handler(
        std::string_view uri,
        httpd_method_t method) noexcept
    {
        if (handle_ == nullptr)
        {
            return ESP_ERR_INVALID_STATE;
        }

        const auto it = std::ranges::find_if(
            routes_,
            [&](const Route& route)-> bool
            {
                return route.uri == uri && route.method == method;
            });
        if (it == routes_.end())
        {
            return ESP_ERR_NOT_FOUND;
        }

        const esp_err_t result = httpd_unregister_uri_handler(handle_, it->uri.c_str(), it->method);

        if (result == ESP_OK)
        {
            routes_.erase(it);
        }
        
        return result;
    }

    httpd_handle_t HttpServer::native_handle() const noexcept
    {
        return handle_;
    }

    esp_err_t HttpServer::dispatch(httpd_req_t* req)
    {
        auto& route = *static_cast<Route*>(req->user_ctx);
        return route.handler(req);
    }
} // namespace internet