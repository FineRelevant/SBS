#include "internet_config.hpp"
#include <expected>
#include <utility>
#include "esp_err.h"
#include "nvs.hpp"

namespace internet 
{
    std::expected<InternetConfig, esp_err_t> InternetConfigManager::load_from_nvs() noexcept
    {
        auto nvs = storage::NvsService::open("config");
        if (!nvs.has_value())
        {
            return std::unexpected(nvs.error());
        }

        auto station_ssid = nvs->get_string("sta_ssid");
        if (!station_ssid.has_value())
        {
            return std::unexpected(station_ssid.error());
        }

        auto station_password = nvs->get_string("sta_password");
        if (!station_password.has_value())
        {
            return std::unexpected(station_password.error());
        }

        auto ap_ssid = nvs->get_string("ap_ssid");
        if (!ap_ssid.has_value())
        {
            return std::unexpected(ap_ssid.error());
        }

        auto ap_password = nvs->get_string("ap_password");
        if (!ap_password.has_value())
        {
            return std::unexpected(ap_password.error());
        }

        auto ap_channel = nvs->get_u32("ap_channel");
        if (!ap_channel.has_value())
        {
            return std::unexpected(ap_channel.error());
        }
        
        auto ap_max_connections = nvs->get_u32("max_connection");
        if (!ap_max_connections.has_value())
        {
            return std::unexpected(ap_max_connections.error());
        }

        auto ap_hidden = nvs->get_bool("ap_hidden");
        if (!ap_hidden.has_value())
        {
            return std::unexpected(ap_hidden.error());
        }

        auto reconnect_enabled = nvs->get_bool("reconnect_on");
        if (!reconnect_enabled.has_value())
        {
            return std::unexpected(reconnect_enabled.error());
        }

        auto stop_if_reached_threshold = nvs->get_bool("stop_threshold");
        if (!stop_if_reached_threshold.has_value())
        {
            return std::unexpected(stop_if_reached_threshold.error());
        }
        
        auto reconnect_initial_delay_ms = nvs->get_u32("delay_ms");
        if (!reconnect_initial_delay_ms.has_value())
        {
            return std::unexpected(reconnect_initial_delay_ms.error());
        }
        
        auto reconnect_max_delay_ms = nvs->get_u32("max_delay_ms");
        if (!reconnect_max_delay_ms.has_value())
        {
            return std::unexpected(reconnect_max_delay_ms.error());
        }

        auto consecutive_failures_threshold = nvs->get_u32("fail_threshold");
        if (!consecutive_failures_threshold.has_value())
        {
            return std::unexpected(consecutive_failures_threshold.error());
        }

        auto configuration_service_mode = nvs->get_u32("service_mode");
        if (!configuration_service_mode.has_value())
        {
            return std::unexpected(configuration_service_mode.error());
        }

        if (configuration_service_mode.value() > 2)
        {
            return std::unexpected(ESP_ERR_INVALID_ARG);
        }

        auto mode = static_cast<ConfigurationServiceConfig::OperatingMode>(configuration_service_mode.value());
        
        return InternetConfig{
            .wifi = {
                .station = {
                    .ssid = *station_ssid,
                    .password = *station_password,
                },
                .access_point = {
                    .ssid = *ap_ssid,
                    .password = *ap_password,
                    .channel = *ap_channel,
                    .max_connections = *ap_max_connections,
                    .hidden = *ap_hidden,
                },
                .reconnect = {
                    .enabled = *reconnect_enabled,
                    .stop_if_reached_threshold = *stop_if_reached_threshold,
                    .initial_delay_ms = *reconnect_initial_delay_ms,
                    .max_delay_ms = *reconnect_max_delay_ms,
                    .consecutive_failures_threshold = *consecutive_failures_threshold,
                },
            },
            .configuration_service{
                .mode = mode
            },
        };
    }

    esp_err_t InternetConfigManager::save_to_nvs(const InternetConfig& config) noexcept
    {
        auto nvs = storage::NvsService::open("config");
        if (!nvs.has_value())
        {
            return nvs.error();
        }

        auto station_ssid = nvs->set_string("sta_ssid", config.wifi.station.ssid);
        if (station_ssid != ESP_OK)
        {
            return station_ssid;
        }

        auto station_password = nvs->set_string("sta_password", config.wifi.station.password);
        if (station_password != ESP_OK)
        {
            return station_password;
        }

        auto ap_ssid = nvs->set_string("ap_ssid", config.wifi.access_point.ssid);
        if (ap_ssid != ESP_OK)
        {
            return ap_ssid;
        }

        auto ap_password = nvs->set_string("ap_password", config.wifi.access_point.password);
        if (ap_password != ESP_OK)
        {
            return ap_password;
        }

        auto ap_channel = nvs->set_u32("ap_channel", config.wifi.access_point.channel);
        if (ap_channel != ESP_OK)
        {
            return ap_channel;
        }

        auto ap_max_connections = nvs->set_u32("max_connection", config.wifi.access_point.max_connections);
        if (ap_max_connections != ESP_OK)
        {
            return ap_max_connections;
        }

        auto ap_hidden = nvs->set_bool("ap_hidden", config.wifi.access_point.hidden);
        if (ap_hidden != ESP_OK)
        {
            return ap_hidden;
        }

        auto reconnect_enabled = nvs->set_bool("reconnect_on", config.wifi.reconnect.enabled);
        if (reconnect_enabled != ESP_OK)
        {
            return reconnect_enabled;
        }

        auto stop_if_reached_threshold = nvs->set_bool("stop_threshold", config.wifi.reconnect.stop_if_reached_threshold);
        if (stop_if_reached_threshold != ESP_OK)
        {
            return stop_if_reached_threshold;
        }

        auto reconnect_initial_delay_ms = nvs->set_u32("delay_ms", config.wifi.reconnect.initial_delay_ms);
        if (reconnect_initial_delay_ms != ESP_OK)
        {
            return reconnect_initial_delay_ms;
        }

        auto reconnect_max_delay_ms = nvs->set_u32("max_delay_ms", config.wifi.reconnect.max_delay_ms);
        if (reconnect_max_delay_ms != ESP_OK)
        {
            return reconnect_max_delay_ms;
        }

        auto consecutive_failures_threshold = nvs->set_u32("fail_threshold", config.wifi.reconnect.consecutive_failures_threshold);
        if (consecutive_failures_threshold != ESP_OK)
        {
            return consecutive_failures_threshold;
        }

        auto mode = static_cast<uint32_t>(std::to_underlying(config.configuration_service.mode));
        auto configuration_service_mode = nvs->set_u32("service_mode", mode);
        if (configuration_service_mode != ESP_OK)
        {
            return configuration_service_mode;
        }

        return nvs->commit();
    }
} // namespace internet