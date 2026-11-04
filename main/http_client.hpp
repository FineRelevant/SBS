#pragma once

#include <string>
#include <string_view>
#include <span>
#include <expected>

#include "esp_http_client.h"

namespace internet
{
    struct Header
    {
        std::string_view name{};
        std::string_view value{};
    };
    struct Request
    {
        esp_http_client_method_t method = HTTP_METHOD_GET;
        std::string_view url{};
        std::span<const Header> headers{};
        std::string_view body{};
        std::size_t max_response_body_size = 64 * 1024;
    };
    struct Response
    {
        int status_code = 0;
        std::string body;

        [[nodiscard]] 
        bool is_successful() const noexcept 
        {
            return status_code >= 200 && status_code < 300;
        }
    };
    class HttpClient 
    {
    public:
        HttpClient() noexcept = default;
        ~HttpClient() noexcept;

        HttpClient(const HttpClient&) = delete;
        HttpClient& operator=(const HttpClient&) = delete;

        HttpClient(HttpClient&& other) noexcept;
        HttpClient& operator=(HttpClient&&) = delete;

        [[nodiscard]] 
        esp_err_t init(const esp_http_client_config_t& config) noexcept;

        [[nodiscard]] 
        esp_err_t cleanup() noexcept;

        [[nodiscard]] 
        bool initialized() const noexcept 
        {
            return handle_ != nullptr;
        }

        [[nodiscard]] 
        esp_http_client_handle_t native_handle() const noexcept 
        {
            return handle_;
        }

        [[nodiscard]] 
        std::expected<Response, esp_err_t> perform(const Request& request);

    private:
        esp_http_client_handle_t handle_{nullptr};

        http_event_handle_cb user_event_handler_{nullptr};
        void* user_data_{nullptr};

        std::string* response_body_{nullptr};
        std::size_t max_response_body_size_{0};
        esp_err_t capture_error_{ESP_OK};

        static esp_err_t dispatch_event(esp_http_client_event_t* event);
    };
}