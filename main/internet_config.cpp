#include "internet_config.hpp"
#include "nvs.hpp"

namespace internet 
{
    std::expected<InternetConfig, esp_err_t> InternetConfigLoader::load_from_nvs()
    {
        auto nvs = storage::NvsService::open("config");
        if(!nvs)
            return std::unexpected(nvs.error());

        auto station_ssid = nvs->get_string("station_ssid");
        if(!station_ssid)
            return std::unexpected(station_ssid.error());

        auto station_password = nvs->get_string("station_password");
        if(!station_password)
            return std::unexpected(station_password.error());

        auto ap_ssid = nvs->get_string("ap_ssid");
        if(!ap_ssid)
            return std::unexpected(ap_ssid.error());

        auto ap_password = nvs->get_string("ap_password");
        if(!ap_password)
            return std::unexpected(ap_password.error());

        auto ap_channel = nvs->get_u32("ap_channel");
        if(!ap_channel)
            return std::unexpected(ap_channel.error());
        
        auto max_connections = nvs->get_u32("ap_max_connections");
        if(!max_connections)
            return std::unexpected(max_connections.error());
        
        auto hidden = nvs->get_bool("ap_hidden");
        if(!hidden)
            return std::unexpected(hidden.error());

        auto reconnect_enabled = nvs->get_bool("reconnect_enabled");
        if(!reconnect_enabled)
            return std::unexpected(reconnect_enabled.error());
        
        auto reconnect_initial_delay_ms = nvs->get_u32("reconnect_initial_delay_ms");
        if(!reconnect_initial_delay_ms)
            return std::unexpected(reconnect_initial_delay_ms.error());
        
        auto reconnect_max_delay_ms = nvs->get_u32("reconnect_max_delay_ms");
        if(!reconnect_max_delay_ms)
            return std::unexpected(reconnect_max_delay_ms.error());
        
        return InternetConfig{
            .wifi = {
                .station = {
                    .ssid = *station_ssid,
                    .password = *station_password
                },
                .access_point = {
                    .ssid = *ap_ssid,
                    .password = *ap_password,
                    .channel = *ap_channel,
                    .max_connections = *max_connections,
                    .hidden = *hidden
                },
                .reconnect = {
                    .enabled = *reconnect_enabled,
                    .initial_delay_ms = *reconnect_initial_delay_ms,
                    .max_delay_ms = *reconnect_max_delay_ms
                }
            },
            .provisioning = {}
        };
    }
} // namespace internet