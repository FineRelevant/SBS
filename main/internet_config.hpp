#pragma once

#include <string>
#include <expected>
#include <cstdint>

#include "esp_err.h"

namespace internet
{   
	struct StationConfig
	{
		std::string ssid;
		std::string password;
	};

	struct AccessPointConfig
	{
		std::string ssid;
		std::string password;

		std::uint32_t channel{1};
		std::uint32_t max_connections{4};

		bool hidden{false};
	};

	struct ReconnectPolicy
	{
		bool enabled{true};

		std::uint32_t initial_delay_ms{1000};
		std::uint32_t max_delay_ms{30000};
	};

    struct WifiConfig
    {
        StationConfig station;
        AccessPointConfig access_point;
        ReconnectPolicy reconnect;
    };

	struct ProvisioningConfig
	{
		bool enabled{true};
		bool start_if_unconfigured{true};
	};

	struct InternetConfig
	{
		WifiConfig wifi;
        ProvisioningConfig provisioning;
	};

	class InternetConfigLoader final
	{
	  public:
		std::expected<InternetConfig, esp_err_t> load_from_nvs();
	};
}