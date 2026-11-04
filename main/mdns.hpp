#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <expected>
#include <span>
#include <string_view>

#include "esp_err.h"

namespace internet
{
    class Mdns final
    {
    public:
        struct TextRecord
        {
            std::string key{};
            std::string value{};
        };
        struct Service 
        {
            std::string instance;
            std::string type;
            std::string protocol{"_tcp"};
            std::uint16_t port{};
            std::vector<TextRecord> txt{};
        };

        [[nodiscard]]
        static std::expected<Mdns, esp_err_t>
        create(std::string_view hostname,
            std::string_view instance = {});

        explicit Mdns() noexcept = default;
        ~Mdns() noexcept;

        Mdns(const Mdns&) = delete;
        Mdns& operator=(const Mdns&) = delete;

        Mdns(Mdns&& other) noexcept;
        Mdns& operator=(Mdns&& other) noexcept;

        [[nodiscard]]
        esp_err_t shutdown() noexcept;

        [[nodiscard]]
        esp_err_t set_hostname(std::string_view hostname) noexcept;

        [[nodiscard]]
        esp_err_t set_instance_name(std::string_view instance) noexcept;

        [[nodiscard]]
        esp_err_t add_service(const Service& service) noexcept;

        [[nodiscard]]
        esp_err_t remove_service(
            std::string_view type,
            std::string_view protocol = "_tcp") noexcept;

        [[nodiscard]]
        esp_err_t remove_all_services() noexcept;

        [[nodiscard]]
        bool is_running() const noexcept;

    private:
        bool running_{false};
    };
}