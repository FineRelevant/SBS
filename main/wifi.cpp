#include "wifi.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

#include "esp_log.h"

namespace internet
{
	namespace
	{
		constexpr char TAG[] = "WifiService";

		constexpr std::size_t mac_address_size = 6;

		event::MacAddress make_mac(
			const std::uint8_t (&mac)[mac_address_size])
		{
			event::MacAddress result{};

			std::copy(
				std::begin(mac),
				std::end(mac),
				result.begin());

			return result;
		}

		template <typename T, std::size_t N>
		bool copy_string(
			std::string_view source,
			T (&destination)[N])
		{
			if (source.size() > N)
				return false;

			std::memset(destination, 0, sizeof(destination));

			if (!source.empty())
			{
				std::memcpy(
					destination,
					source.data(),
					source.size());
			}

			return true;
		}

		bool valid_station_config(const StationConfig &config)
		{
			return !config.ssid.empty() &&
				   config.ssid.size() <= 32 &&
				   config.password.size() <= 64;
		}

		bool valid_access_point_config(
			const AccessPointConfig &config)
		{
			if (config.ssid.empty() ||
				config.ssid.size() > 32 ||
				config.channel < 1 ||
				config.channel > 14 ||
				config.max_connections == 0)
			{
				return false;
			}

			// An empty password is allowed for an open AP.
			// A secured AP requires a password of at least 8 characters.
			return config.password.empty() ||
				   (config.password.size() >= 8 &&
					config.password.size() <= 63);
		}
	} // namespace

	WifiService::WifiService(
		WifiConfig config,
		event::EventDispatcher &event_dispatcher)
		: config_(std::move(config)),
		  event_dispatcher_(event_dispatcher)
	{
	}

	WifiService::~WifiService() noexcept
	{
		// The application must destroy the service in task context,
		// before deinitializing the ESP-IDF Wi-Fi driver.

		cancel_reconnect();

		if (reconnect_timer_ != nullptr)
		{
			xTimerDelete(reconnect_timer_, portMAX_DELAY);
			reconnect_timer_ = nullptr;
		}

		if (started_)
		{
			esp_wifi_stop();
			started_ = false;
		}

		if (ip_event_handler_ != nullptr)
		{
			esp_event_handler_instance_unregister(
				IP_EVENT,
				ESP_EVENT_ANY_ID,
				ip_event_handler_);

			ip_event_handler_ = nullptr;
		}

		if (wifi_event_handler_ != nullptr)
		{
			esp_event_handler_instance_unregister(
				WIFI_EVENT,
				ESP_EVENT_ANY_ID,
				wifi_event_handler_);

			wifi_event_handler_ = nullptr;
		}

		if (access_point_netif_ != nullptr)
		{
			esp_netif_destroy(access_point_netif_);
			access_point_netif_ = nullptr;
		}

		if (station_netif_ != nullptr)
		{
			esp_netif_destroy(station_netif_);
			station_netif_ = nullptr;
		}
	}

	std::expected<void, esp_err_t> WifiService::start()
	{
		if (started_)
			return std::unexpected{ESP_ERR_INVALID_STATE};

		if (!valid_access_point_config(config_.access_point))
			return std::unexpected{ESP_ERR_INVALID_ARG};

		if (config_.reconnect.initial_delay_ms == 0 ||
			config_.reconnect.max_delay_ms <
				config_.reconnect.initial_delay_ms)
		{
			return std::unexpected{ESP_ERR_INVALID_ARG};
		}

		station_netif_ = esp_netif_create_default_wifi_sta();

		if (station_netif_ == nullptr)
			return std::unexpected{ESP_ERR_NO_MEM};

		access_point_netif_ = esp_netif_create_default_wifi_ap();

		if (access_point_netif_ == nullptr)
		{
			esp_netif_destroy(station_netif_);
			station_netif_ = nullptr;
			return std::unexpected{ESP_ERR_NO_MEM};
		}

		// Both interfaces are configured below.
		auto result = esp_wifi_set_mode(WIFI_MODE_APSTA);

		if (result != ESP_OK)
			goto cleanup;

		result = configure_station(config_.station);

		if (result != ESP_OK)
			goto cleanup;

		result = configure_access_point(config_.access_point);

		if (result != ESP_OK)
			goto cleanup;

		station_enabled_ = valid_station_config(config_.station);
		access_point_enabled_ = true;
		reconnect_suppressed_ = false;
		reconnect_delay_ms_ = 0;

		result = update_mode();

		if (result != ESP_OK)
			goto cleanup;

		reconnect_timer_ = xTimerCreate(
			"wifi_retry",
			pdMS_TO_TICKS(config_.reconnect.initial_delay_ms),
			pdFALSE,
			this,
			&WifiService::reconnect_timer_callback);

		if (reconnect_timer_ == nullptr)
		{
			result = ESP_ERR_NO_MEM;
			goto cleanup;
		}

		result = esp_event_handler_instance_register(
			WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			&WifiService::esp_event_handler,
			this,
			&wifi_event_handler_);

		if (result != ESP_OK)
			goto cleanup;

		result = esp_event_handler_instance_register(
			IP_EVENT,
			ESP_EVENT_ANY_ID,
			&WifiService::esp_event_handler,
			this,
			&ip_event_handler_);

		if (result != ESP_OK)
			goto cleanup;

		result = esp_wifi_start();

		if (result != ESP_OK)
			goto cleanup;

		started_ = true;

		if (station_enabled_)
			station_state_ = StationState::Connecting;
		else
			station_state_ = StationState::Stopped;

		return {};

	cleanup:
		if (started_)
		{
			esp_wifi_stop();
			started_ = false;
		}

		if (ip_event_handler_ != nullptr)
		{
			esp_event_handler_instance_unregister(
				IP_EVENT,
				ESP_EVENT_ANY_ID,
				ip_event_handler_);

			ip_event_handler_ = nullptr;
		}

		if (wifi_event_handler_ != nullptr)
		{
			esp_event_handler_instance_unregister(
				WIFI_EVENT,
				ESP_EVENT_ANY_ID,
				wifi_event_handler_);

			wifi_event_handler_ = nullptr;
		}

		if (reconnect_timer_ != nullptr)
		{
			xTimerDelete(reconnect_timer_, portMAX_DELAY);
			reconnect_timer_ = nullptr;
		}

		if (access_point_netif_ != nullptr)
		{
			esp_netif_destroy(access_point_netif_);
			access_point_netif_ = nullptr;
		}

		if (station_netif_ != nullptr)
		{
			esp_netif_destroy(station_netif_);
			station_netif_ = nullptr;
		}

		station_enabled_ = false;
		access_point_enabled_ = false;
		station_state_ = StationState::Stopped;
		access_point_state_ = AccessPointState::Stopped;

		reconnect_suppressed_ = false;
		reconnect_delay_ms_ = 0;

		return std::unexpected{result};
	}

	std::expected<void, esp_err_t> WifiService::stop()
	{
		if (!started_)
			return std::unexpected{ESP_ERR_INVALID_STATE};

		cancel_reconnect();

		const auto result = esp_wifi_stop();

		if (result != ESP_OK)
			return std::unexpected{result};

		started_ = false;
		station_enabled_ = false;
		access_point_enabled_ = false;
		reconnect_suppressed_ = true;

		station_state_ = StationState::Stopped;
		access_point_state_ = AccessPointState::Stopped;

		return {};
	}

	std::expected<void, esp_err_t> WifiService::start_station()
	{
		if (!started_)
			return std::unexpected{ESP_ERR_INVALID_STATE};

		if (!valid_station_config(config_.station))
			return std::unexpected{ESP_ERR_INVALID_ARG};

		if (station_enabled_)
			return {};

		station_enabled_ = true;
		reconnect_suppressed_ = false;

		const auto result = update_mode();

		if (result != ESP_OK)
		{
			station_enabled_ = false;
			return std::unexpected{result};
		}

		station_state_ = StationState::Connecting;

		return connect();
	}

	std::expected<void, esp_err_t> WifiService::stop_station()
	{
		if (!started_)
			return std::unexpected{ESP_ERR_INVALID_STATE};

		if (!station_enabled_)
			return {};

		reconnect_suppressed_ = true;
		cancel_reconnect();

		const auto disconnect_result = esp_wifi_disconnect();

		if (disconnect_result != ESP_OK &&
			disconnect_result != ESP_ERR_WIFI_NOT_CONNECT)
		{
			reconnect_suppressed_ = false;
			return std::unexpected{disconnect_result};
		}

		station_enabled_ = false;

		const auto result = update_mode();

		if (result != ESP_OK)
		{
			station_enabled_ = true;
			reconnect_suppressed_ = false;
			return std::unexpected{result};
		}

		station_state_ = StationState::Stopped;
		publish_station_stopped();

		return {};
	}

	std::expected<void, esp_err_t> WifiService::connect()
	{
		if (!started_ || !station_enabled_)
			return std::unexpected{ESP_ERR_INVALID_STATE};

		if (!valid_station_config(config_.station))
			return std::unexpected{ESP_ERR_INVALID_ARG};

		if (station_state_ == StationState::Connected ||
			station_state_ == StationState::Connecting)
		{
			return {};
		}

		reconnect_suppressed_ = false;
		cancel_reconnect();

		station_state_ = StationState::Connecting;

		const auto result = esp_wifi_connect();

		if (result != ESP_OK)
		{
			station_state_ = StationState::Disconnected;
			schedule_reconnect();
			return std::unexpected{result};
		}

		return {};
	}

	std::expected<void, esp_err_t> WifiService::disconnect()
	{
		if (!started_ || !station_enabled_)
			return std::unexpected{ESP_ERR_INVALID_STATE};

		reconnect_suppressed_ = true;
		cancel_reconnect();

		const auto result = esp_wifi_disconnect();

		if (result != ESP_OK &&
			result != ESP_ERR_WIFI_NOT_CONNECT)
		{
			reconnect_suppressed_ = false;
			return std::unexpected{result};
		}

		station_state_ = StationState::Disconnected;

		return {};
	}

	std::expected<void, esp_err_t> WifiService::start_access_point()
	{
		if (!started_)
			return std::unexpected{ESP_ERR_INVALID_STATE};

		if (access_point_enabled_)
			return {};

		access_point_enabled_ = true;

		const auto result = update_mode();

		if (result != ESP_OK)
		{
			access_point_enabled_ = false;
			return std::unexpected{result};
		}

		return {};
	}

	std::expected<void, esp_err_t> WifiService::stop_access_point()
	{
		if (!started_)
			return std::unexpected{ESP_ERR_INVALID_STATE};

		if (!access_point_enabled_)
			return {};

		access_point_enabled_ = false;

		const auto result = update_mode();

		if (result != ESP_OK)
		{
			access_point_enabled_ = true;
			return std::unexpected{result};
		}
		return {};
	}

	std::expected<void, esp_err_t> WifiService::configure_wifi_service(WifiConfig config)
	{
		const bool station_configured = !config.station.ssid.empty();

		if (station_configured && !valid_station_config(config.station))
			return std::unexpected{ESP_ERR_INVALID_ARG};

		if (!valid_access_point_config(config.access_point))
			return std::unexpected{ESP_ERR_INVALID_ARG};

		if (config.reconnect.initial_delay_ms == 0 ||
			config.reconnect.max_delay_ms <
				config.reconnect.initial_delay_ms)
		{
			return std::unexpected{ESP_ERR_INVALID_ARG};
		}

		if (!started_)
		{
			config_ = std::move(config);
			return {};
		}

		const auto previous_config = config_;

		auto result = configure_station(config.station);

		if (result == ESP_OK)
			result = configure_access_point(config.access_point);

		if (result != ESP_OK)
		{
			const auto restore_station =
				configure_station(previous_config.station);

			const auto restore_ap =
				configure_access_point(previous_config.access_point);

			if (restore_station != ESP_OK || restore_ap != ESP_OK)
			{
				ESP_LOGE(
					TAG,
					"Failed to restore previous Wi-Fi driver configuration");
			}

			return std::unexpected{result};
		}

		config_ = std::move(config);

		// Reconfiguration invalidates any pending retry using old settings.
		cancel_reconnect();
		reconnect_suppressed_ = false;

		if (station_enabled_ && station_configured)
		{
			station_state_ = StationState::Disconnected;
			return connect();
		}

		return {};
	}

	void WifiService::set_reconnect_enabled(bool enabled)
	{
		config_.reconnect.enabled = enabled;

		if (!enabled)
		{
			cancel_reconnect();
			return;
		}

		if (started_ &&
			station_enabled_ &&
			station_state_ == StationState::Disconnected)
		{
			schedule_reconnect();
		}
	}

	esp_err_t WifiService::configure_station(
		const StationConfig& station)
	{
		wifi_config_t config{};

		if (!copy_string(station.ssid, config.sta.ssid) ||
			!copy_string(station.password, config.sta.password))
		{
			return ESP_ERR_INVALID_ARG;
		}

		return esp_wifi_set_config(WIFI_IF_STA, &config);
	}

	esp_err_t WifiService::configure_access_point(
		const AccessPointConfig& access_point)
	{
		wifi_config_t config{};

		if (!copy_string(access_point.ssid, config.ap.ssid) ||
			!copy_string(access_point.password, config.ap.password))
		{
			return ESP_ERR_INVALID_ARG;
		}

		config.ap.channel = access_point.channel;
		config.ap.max_connection = access_point.max_connections;
		config.ap.ssid_hidden = access_point.hidden;

		config.ap.authmode = access_point.password.empty()
			? WIFI_AUTH_OPEN
			: WIFI_AUTH_WPA2_PSK;

		return esp_wifi_set_config(WIFI_IF_AP, &config);
	}


	void WifiService::reconnect_timer_callback(TimerHandle_t timer)
	{
		auto *self = static_cast<WifiService *>(
			pvTimerGetTimerID(timer));

		if (self != nullptr)
			self->handle_reconnect_timer();
	}

	void WifiService::schedule_reconnect()
	{
		if (!started_ ||
			!station_enabled_ ||
			!config_.reconnect.enabled ||
			reconnect_suppressed_ ||
			reconnect_timer_ == nullptr)
		{
			return;
		}

		if (reconnect_delay_ms_ == 0)
		{
			reconnect_delay_ms_ =
				config_.reconnect.initial_delay_ms;
		}
		else
		{
			const auto maximum = config_.reconnect.max_delay_ms;

			reconnect_delay_ms_ =
				reconnect_delay_ms_ > maximum / 2
					? maximum
					: std::min(reconnect_delay_ms_ * 2, maximum);
		}

		const auto ticks = std::max<TickType_t>(
			pdMS_TO_TICKS(reconnect_delay_ms_),
			1);

		if (xTimerChangePeriod(reconnect_timer_, ticks, 0) != pdPASS)
		{
			ESP_LOGW(TAG, "Failed to schedule reconnect");
		}
	}

	void WifiService::cancel_reconnect()
	{
		reconnect_delay_ms_ = 0;

		if (reconnect_timer_ != nullptr)
			xTimerStop(reconnect_timer_, 0);
	}

	void WifiService::handle_reconnect_timer()
	{
		if (!started_ ||
			!station_enabled_ ||
			!config_.reconnect.enabled ||
			reconnect_suppressed_)
		{
			return;
		}

		if (station_state_ == StationState::Connected ||
			station_state_ == StationState::Connecting)
		{
			return;
		}

		station_state_ = StationState::Connecting;

		const auto result = esp_wifi_connect();

		if (result != ESP_OK)
		{
			station_state_ = StationState::Disconnected;

			ESP_LOGW(
				TAG,
				"Reconnect request failed: %s",
				esp_err_to_name(result));

			schedule_reconnect();
		}
	}

	void WifiService::esp_event_handler(
		void *arg,
		esp_event_base_t event_base,
		int32_t event_id,
		void *event_data)
	{
		auto *self = static_cast<WifiService *>(arg);

		if (self == nullptr)
			return;

		if (event_base == WIFI_EVENT)
		{
			self->handle_wifi_event(event_id, event_data);
			return;
		}

		if (event_base == IP_EVENT)
			self->handle_ip_event(event_id, event_data);
	}

	void WifiService::handle_wifi_event(
		std::int32_t event_id,
		void *event_data)
	{
		switch (event_id)
		{
		case WIFI_EVENT_STA_START:
			if (station_enabled_)
			{
				station_state_ = StationState::Connecting;
				publish_station_started();

				const auto result = esp_wifi_connect();

				if (result != ESP_OK)
				{
					station_state_ = StationState::Disconnected;
					schedule_reconnect();
				}
			}
			break;

		case WIFI_EVENT_STA_STOP:
			station_state_ = StationState::Stopped;
			publish_station_stopped();
			break;

		case WIFI_EVENT_STA_CONNECTED: {
			const auto *info =
				static_cast<const wifi_event_sta_connected_t *>(event_data);

			station_state_ = StationState::Connected;
			publish_station_connected(*info);
			break;
		}

		case WIFI_EVENT_STA_DISCONNECTED: {
			const auto *info =
				static_cast<const wifi_event_sta_disconnected_t *>(event_data);

			station_state_ = station_enabled_
								 ? StationState::Disconnected
								 : StationState::Stopped;

			publish_station_disconnected(*info);
			schedule_reconnect();
			break;
		}

		case WIFI_EVENT_AP_START:
			access_point_state_ = AccessPointState::Started;
			publish_ap_started();
			break;

		case WIFI_EVENT_AP_STOP:
			access_point_state_ = AccessPointState::Stopped;
			publish_ap_stopped();
			break;

		case WIFI_EVENT_AP_STACONNECTED: {
			const auto *info =
				static_cast<const wifi_event_ap_staconnected_t *>(event_data);

			publish_ap_client_connected(*info);
			break;
		}

		case WIFI_EVENT_AP_STADISCONNECTED: {
			const auto *info =
				static_cast<const wifi_event_ap_stadisconnected_t *>(event_data);

			publish_ap_client_disconnected(*info);
			break;
		}

		default:
			break;
		}
	}

	void WifiService::handle_ip_event(
		std::int32_t event_id,
		void *event_data)
	{
		switch (event_id)
		{
		case IP_EVENT_STA_GOT_IP: {
			const auto *info =
				static_cast<const ip_event_got_ip_t *>(event_data);

			station_state_ = StationState::Connected;

			cancel_reconnect();

			ESP_LOGI(
				TAG,
				"Got IP: " IPSTR,
				IP2STR(&info->ip_info.ip));

			publish_station_got_ip(*info);
			break;
		}

		case IP_EVENT_STA_LOST_IP:
			if (station_enabled_)
				station_state_ = StationState::Disconnected;

			publish_station_lost_ip();
			schedule_reconnect();
			break;

		default:
			break;
		}
	}

	void WifiService::publish_station_started()
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiStationStarted{});
	}

	void WifiService::publish_station_stopped()
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiStationStopped{});
	}

	void WifiService::publish_station_connected(
		const wifi_event_sta_connected_t &info)
	{
		const auto bssid = make_mac(info.bssid);

		wifi_ap_record_t ap_info{};
		std::int8_t rssi = 0;

		if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
			rssi = ap_info.rssi;

		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiStationConnected{
				.bssid = bssid,
				.rssi = rssi,
				.channel = info.channel});
	}

	void WifiService::publish_station_disconnected(
		const wifi_event_sta_disconnected_t &info)
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiStationDisconnected{
				.reason = info.reason});
	}

	void WifiService::publish_station_got_ip(
		const ip_event_got_ip_t &info)
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiStationGotIp{
				.address = info.ip_info.ip.addr,
				.netmask = info.ip_info.netmask.addr,
				.gateway = info.ip_info.gw.addr});
	}

	void WifiService::publish_station_lost_ip()
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiStationLostIp{});
	}

	void WifiService::publish_ap_started()
	{
		event::MacAddress mac{};

		if (esp_wifi_get_mac(WIFI_IF_AP, mac.data()) != ESP_OK)
			mac.fill(0);

		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiApStarted{
				.mac = mac});
	}

	void WifiService::publish_ap_stopped()
	{
		event::MacAddress mac{};

		if (esp_wifi_get_mac(WIFI_IF_AP, mac.data()) != ESP_OK)
			mac.fill(0);

		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiApStopped{
				.mac = mac});
	}

	void WifiService::publish_ap_client_connected(
		const wifi_event_ap_staconnected_t &info)
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiApClientConnected{
				.mac = make_mac(info.mac)});
	}

	void WifiService::publish_ap_client_disconnected(
		const wifi_event_ap_stadisconnected_t &info)
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiApClientDisconnected{
				.mac = make_mac(info.mac)});
	}
}