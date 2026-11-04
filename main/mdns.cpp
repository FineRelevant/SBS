#include "mdns.hpp"

#include <utility>

#include "mdns.h"

namespace internet
{
    std::expected<Mdns, esp_err_t>
    Mdns::create(std::string_view hostname, std::string_view instance)
    {
        if (hostname.empty())
        {
            return std::unexpected(ESP_ERR_INVALID_ARG);
        }

        Mdns mdns;

        auto err = mdns_init();

        if (err != ESP_OK)
        {
            return std::unexpected(err);
        }

        mdns.running_ = true;

        err = mdns.set_hostname(hostname);

        if (err != ESP_OK)
        {
            return std::unexpected(err);
        }

        if (!instance.empty())
        {
            err = mdns.set_instance_name(instance);

            if (err != ESP_OK)
            {
                return std::unexpected(err);
            }
        }

        return mdns;
    }

    esp_err_t Mdns::shutdown() noexcept
    {
        if (!running_)
        {
            return ESP_ERR_INVALID_STATE;
        }

        mdns_free();
        running_ = false;

        return ESP_OK;
    }

    Mdns::~Mdns() noexcept
    {
        if (running_)
        {
            mdns_free();
            running_ = false;
        }
    }
    Mdns::Mdns(Mdns&& other) noexcept
        : running_(std::exchange(other.running_, false))
    {
    }

    Mdns& Mdns::operator=(Mdns&& other) noexcept
    {
        if (this != &other)
        {
            if (running_)
            {
                mdns_free();
            }

            running_ = std::exchange(other.running_, false);
        }

        return *this;
    }

    esp_err_t Mdns::set_hostname(std::string_view hostname) noexcept
    {
        if (!running_)
        {
            return ESP_ERR_INVALID_STATE;
        }

        if (hostname.empty())
        {
            return ESP_ERR_INVALID_ARG;
        }

        const std::string value{hostname};

        return mdns_hostname_set(value.c_str());
    }

    esp_err_t Mdns::set_instance_name(std::string_view instance) noexcept
    {
        if (!running_)
        {
            return ESP_ERR_INVALID_STATE;
        }

        if (instance.empty())
        {
            return ESP_ERR_INVALID_ARG;
        }

        const std::string value{instance};

        return mdns_instance_name_set(value.c_str());
    }

    esp_err_t Mdns::add_service(const Service& service) noexcept
    {
        if (!running_)
        {
            return ESP_ERR_INVALID_STATE;
        }

        if (service.type.empty() ||
            service.protocol.empty() ||
            service.port == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }

        std::vector<std::string> storage;
        std::vector<mdns_txt_item_t> txt;

        storage.reserve(service.txt.size() * 2);
        txt.reserve(service.txt.size());

        for (const auto& record : service.txt)
        {
            if (record.key.empty())
            {
                return ESP_ERR_INVALID_ARG;
            }

            storage.emplace_back(record.key);
            storage.emplace_back(record.value);
        }

        for (std::size_t i = 0; i < service.txt.size(); ++i)
        {
            txt.push_back(
            {
                .key = storage[i * 2].data(),
                .value = storage[i * 2 + 1].data()
            });
        }

        const char* instance = service.instance.empty()
            ? nullptr
            : service.instance.c_str();

        return mdns_service_add(
            instance,
            service.type.c_str(),
            service.protocol.c_str(),
            service.port,
            txt.data(),
            txt.size()
        );
    }

    esp_err_t Mdns::remove_service(
        std::string_view type,
        std::string_view protocol) noexcept
    {
        if (!running_)
        {
            return ESP_ERR_INVALID_STATE;
        }

        if (type.empty() || protocol.empty())
        {
            return ESP_ERR_INVALID_ARG;
        }

        const std::string service_type{type};
        const std::string service_protocol{protocol};

        return mdns_service_remove(
            service_type.c_str(),
            service_protocol.c_str()
        );
    }

    esp_err_t Mdns::remove_all_services() noexcept
    {
        if (!running_)
        {
            return ESP_ERR_INVALID_STATE;
        }

        return mdns_service_remove_all();
    }

    bool Mdns::is_running() const noexcept
    {
        return running_;
    }
}