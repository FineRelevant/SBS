#pragma once

#include <string>
#include <expected>
#include <cstdint>

#include "esp_err.h"

namespace internet
{   
	struct StationConfig
	{
		std::string ssid{"Test"};
		std::string password{"Test1234"};
	};

	struct AccessPointConfig
	{
		std::string ssid{"Setup"};
		std::string password{"Setup123"};

		std::uint32_t channel{1};
		std::uint32_t max_connections{4};

		bool hidden{false};
	};

	struct ReconnectPolicy
	{
		bool enabled{true};
		bool stop_if_reached_threshold{false};

		std::uint32_t initial_delay_ms{1000};
		std::uint32_t max_delay_ms{3000};

		// Zero means unlimited retries.
		std::uint32_t consecutive_failures_threshold{5};
	};

    struct WifiConfig
    {
        StationConfig station;
        AccessPointConfig access_point;
        ReconnectPolicy reconnect;
    };

	struct ConfigurationServiceConfig
	{
		enum class OperatingMode : std::uint8_t
		{
			Sta = 0,
			Ap,
			ApSta,
		};
		OperatingMode mode{OperatingMode::Ap};
	};

	struct InternetConfig
	{
		WifiConfig wifi{};
        ConfigurationServiceConfig configuration_service{};
	};

	class InternetConfigManager final
	{
	  public:
		InternetConfigManager() = delete;
		static std::expected<InternetConfig, esp_err_t> load_from_nvs() noexcept;
		static esp_err_t save_to_nvs(const InternetConfig& config) noexcept;
	};
}