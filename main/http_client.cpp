#include "http_client.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace internet
{

    HttpClient::~HttpClient() noexcept 
    {
        if (handle_ != nullptr) {
            (void)cleanup();
        }
    }

    HttpClient::HttpClient(HttpClient&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
        user_event_handler_(other.user_event_handler_),
        user_data_(other.user_data_) 
    {
        other.user_event_handler_ = nullptr;
        other.user_data_ = nullptr;
    }

    esp_err_t HttpClient::init(const esp_http_client_config_t& config) noexcept 
    {
        if (handle_ != nullptr) 
        {
            return ESP_ERR_INVALID_STATE;
        }

        user_event_handler_ = config.event_handler;
        user_data_ = config.user_data;

        auto wrapped_config = config;
        wrapped_config.event_handler = &HttpClient::dispatch_event;
        wrapped_config.user_data = this;

        handle_ = esp_http_client_init(&wrapped_config);

        if (handle_ == nullptr) 
        {
            user_event_handler_ = nullptr;
            user_data_ = nullptr;
            return ESP_FAIL;
        }

        return ESP_OK;
    }

    esp_err_t HttpClient::cleanup() noexcept 
    {
        if (handle_ == nullptr) 
        {
            return ESP_ERR_INVALID_STATE;
        }

        const esp_err_t result = esp_http_client_cleanup(handle_);

        if (result == ESP_OK) 
        {
            handle_ = nullptr;
            user_event_handler_ = nullptr;
            user_data_ = nullptr;
            response_body_ = nullptr;
        }

        return result;
    }

    esp_err_t HttpClient::dispatch_event(esp_http_client_event_t* event) 
    {
        if (event == nullptr || event->user_data == nullptr) 
        {
            return ESP_ERR_INVALID_ARG;
        }

        auto& self = *static_cast<HttpClient*>(event->user_data);

        if (event->event_id == HTTP_EVENT_ON_DATA &&
            self.response_body_ != nullptr &&
            event->data_len > 0) 
        {
            const auto length = static_cast<std::size_t>(event->data_len);

            if (length > self.max_response_body_size_ -
                            std::min(self.response_body_->size(),
                                    self.max_response_body_size_)) 
            {
                self.capture_error_ = ESP_ERR_INVALID_SIZE;
                return ESP_FAIL;
            }
        }

        if (self.user_event_handler_ != nullptr) 
        {
            auto forwarded_event = *event;
            forwarded_event.user_data = self.user_data_;
            return self.user_event_handler_(&forwarded_event);
        }

        return ESP_OK;
    }

    std::expected<Response, esp_err_t> HttpClient::perform(
        const Request& request)
    {
        if (handle_ == nullptr)
        {
            return std::unexpected(ESP_ERR_INVALID_STATE);
        }

        if (request.url.empty() ||
            request.body.size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max())) 
        {
            return std::unexpected(ESP_ERR_INVALID_ARG);
        }

        std::string url;

        if (const auto result = esp_http_client_set_url(handle_, url.c_str());
            result != ESP_OK) 
        {
            return std::unexpected(result);
        }

        if (const auto result =
                esp_http_client_set_method(handle_, request.method);
            result != ESP_OK) 
        {
            return std::unexpected(result);
        }

        if (const auto result = esp_http_client_delete_all_headers(handle_);
            result != ESP_OK) 
        {
            return std::unexpected(result);
        }

        for (const auto& header : request.headers) 
        {
            if (header.name.empty()) 
            {
                return std::unexpected(ESP_ERR_INVALID_ARG);
            }

            std::string name;
            std::string value;

            if (const auto result = esp_http_client_set_header(
                    handle_, name.c_str(), value.c_str());
                result != ESP_OK) 
            {
                return std::unexpected(result);
            }
        }

        const char* body = request.body.empty()
            ? nullptr
            : request.body.data();

        if (const auto result = esp_http_client_set_post_field(
                handle_, body, static_cast<int>(request.body.size()));
            result != ESP_OK) 
        {
            return std::unexpected(result);
        }

        Response response;

        response_body_ = &response.body;
        max_response_body_size_ = request.max_response_body_size;
        capture_error_ = ESP_OK;

        const esp_err_t result = esp_http_client_perform(handle_);

        response_body_ = nullptr;
        max_response_body_size_ = 0;

        if (capture_error_ != ESP_OK) 
        {
            return std::unexpected(capture_error_);
        }

        if (result != ESP_OK) 
        {
            return std::unexpected(result);
        }

        response.status_code = esp_http_client_get_status_code(handle_);
        return response;
    }
} // namespace internet