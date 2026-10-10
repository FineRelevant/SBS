#pragma once

#include <cstdint>
#include <string>

#include "events.hpp"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

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

		std::uint8_t channel{1};
		std::uint8_t max_connections{4};

		bool hidden{false};
	};

	struct ReconnectPolicy
	{
		bool enabled{true};

		std::uint32_t initial_delay_ms{1000};
		std::uint32_t max_delay_ms{30000};
	};

	struct ProvisioningConfig
	{
		bool enabled{true};
		bool start_if_unconfigured{true};
	};

	struct InternetConfig
	{
		StationConfig station;
		AccessPointConfig access_point;

		ReconnectPolicy reconnect;
		ProvisioningConfig provisioning;
	};

	class InternetConfigLoader
	{
	  public:
		InternetConfig load_from_flash();
		InternetConfig load_from_website();
	};

	class WifiService
	{
	  public:
		enum class StationState
		{
			Stopped,
			Disconnected,
			Connecting,
			Connected
		};

		enum class AccessPointState
		{
			Stopped,
			Started
		};

	  public:
		WifiService(
			InternetConfig config,
			event::EventDispatcher &event_dispatcher);

		~WifiService() noexcept;

		WifiService(const WifiService &) = delete;
		WifiService &operator=(const WifiService &) = delete;

		WifiService(WifiService &&) = delete;
		WifiService &operator=(WifiService &&) = delete;

		[[nodiscard]]
		esp_err_t start();

		[[nodiscard]]
		esp_err_t stop();

		[[nodiscard]]
		esp_err_t start_station();

		[[nodiscard]]
		esp_err_t stop_station();

		[[nodiscard]]
		esp_err_t connect();

		[[nodiscard]]
		esp_err_t disconnect();

		[[nodiscard]]
		esp_err_t start_access_point();

		[[nodiscard]]
		esp_err_t stop_access_point();

		[[nodiscard]]
		esp_err_t configure_station(StationConfig config);

		[[nodiscard]]
		esp_err_t begin_provisioning();

		[[nodiscard]]
		esp_err_t end_provisioning();

		void set_reconnect_enabled(bool enabled);

		[[nodiscard]]
		StationState station_state() const noexcept
		{
			return _station_state;
		}

		[[nodiscard]]
		AccessPointState access_point_state() const noexcept
		{
			return _access_point_state;
		}

		[[nodiscard]]
		bool provisioning_active() const noexcept
		{
			return _provisioning_active;
		}

		[[nodiscard]]
		bool reconnect_enabled() const noexcept
		{
			return _config.reconnect.enabled;
		}

		[[nodiscard]]
		esp_netif_ip_info_t station_ip_info() const noexcept;

	  private:
		[[nodiscard]]
		esp_err_t configure_station();

		[[nodiscard]]
		esp_err_t configure_access_point();

		[[nodiscard]]
		esp_err_t update_mode();

		void handle_wifi_event(int32_t event_id, void *event_data);

		void handle_ip_event(int32_t event_id, void *event_data);

		static void esp_event_handler(
			void *arg,
			esp_event_base_t event_base,
			int32_t event_id,
			void *event_data);

		static void reconnect_timer_callback(TimerHandle_t timer);

		void handle_reconnect_timer();

		void schedule_reconnect();
		void cancel_reconnect();

		void publish_station_started();
		void publish_station_stopped();

		void publish_station_connected(
			const wifi_event_sta_connected_t &info);

		void publish_station_disconnected(
			const wifi_event_sta_disconnected_t &info);

		void publish_station_got_ip(
			const ip_event_got_ip_t &info);

		void publish_station_lost_ip();

		void publish_ap_started();
		void publish_ap_stopped();

		void publish_ap_client_connected(
			const wifi_event_ap_staconnected_t &info);

		void publish_ap_client_disconnected(
			const wifi_event_ap_stadisconnected_t &info);

	  private:
		InternetConfig _config;
		event::EventDispatcher &_event_dispatcher;

		StationState _station_state{StationState::Stopped};
		AccessPointState _access_point_state{AccessPointState::Stopped};

		bool _started{false};
		bool _station_enabled{false};
		bool _access_point_enabled{false};
		bool _provisioning_active{false};
		bool _reconnect_suppressed{false};

		std::uint32_t _reconnect_delay_ms{0};

		esp_netif_t *_station_netif{nullptr};
		esp_netif_t *_access_point_netif{nullptr};

		esp_event_handler_instance_t _wifi_event_handler{nullptr};
		esp_event_handler_instance_t _ip_event_handler{nullptr};

		TimerHandle_t _reconnect_timer{nullptr};
	};
}