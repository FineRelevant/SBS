#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <expected>

#include "events.hpp"
#include "internet_config.hpp"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

namespace internet
{
	class WifiService final
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
			WifiConfig config,
			event::EventDispatcher &event_dispatcher);

		~WifiService() noexcept;

		WifiService(const WifiService &) = delete;
		WifiService &operator=(const WifiService &) = delete;

		WifiService(WifiService &&) = delete;
		WifiService &operator=(WifiService &&) = delete;

		[[nodiscard]]
		std::expected<void, esp_err_t> start();

		[[nodiscard]]
		std::expected<void, esp_err_t> stop();

		[[nodiscard]]
		std::expected<void, esp_err_t> start_station();

		[[nodiscard]]
		std::expected<void, esp_err_t> stop_station();

		[[nodiscard]]
		std::expected<void, esp_err_t> connect();

		[[nodiscard]]
		std::expected<void, esp_err_t> disconnect();

		[[nodiscard]]
		std::expected<void, esp_err_t> start_access_point();

		[[nodiscard]]
		std::expected<void, esp_err_t> stop_access_point();

		[[nodiscard]]
		std::expected<void, esp_err_t> configure_wifi_service(WifiConfig config);

		void set_reconnect_enabled(bool enabled);

		[[nodiscard]]
		StationState station_state() const noexcept
		{
			return station_state_;
		}

		[[nodiscard]]
		AccessPointState access_point_state() const noexcept
		{
			return access_point_state_;
		}

		[[nodiscard]]
		bool reconnect_enabled() const noexcept
		{
			return config_.reconnect.enabled;
		}

		[[nodiscard]]
		esp_netif_ip_info_t station_ip_info() const noexcept;

	  private:
		[[nodiscard]]
		esp_err_t configure_station(const StationConfig &config);

		[[nodiscard]]
		esp_err_t configure_access_point(const AccessPointConfig &config);

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
		WifiConfig config_;
		event::EventDispatcher &event_dispatcher_;

		StationState station_state_{StationState::Stopped};
		AccessPointState access_point_state_{AccessPointState::Stopped};

		bool started_{false};
		bool station_enabled_{false};
		bool access_point_enabled_{false};
		bool reconnect_suppressed_{false};

		std::uint32_t reconnect_delay_ms_{0};

		esp_netif_t *station_netif_{nullptr};
		esp_netif_t *access_point_netif_{nullptr};

		esp_event_handler_instance_t wifi_event_handler_{nullptr};
		esp_event_handler_instance_t ip_event_handler_{nullptr};

		TimerHandle_t reconnect_timer_{nullptr};
	};
}