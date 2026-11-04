
#include "wifi.hpp"

#include <algorithm>
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

		bool valid_station_config(const StationConfig& config)
		{
			return !config.ssid.empty() &&
				config.ssid.size() <= 32 &&
				config.password.size() <= 64;
		}

		bool valid_access_point_config(const AccessPointConfig& config)
		{
			if (config.ssid.empty() ||
				config.ssid.size() > 32 ||
				config.channel < 1 ||
				config.channel > 14 ||
				config.max_connections == 0)
			{
				return false;
			}

			return config.password.empty() ||
				(config.password.size() >= 8 &&
					config.password.size() <= 63);
		}

		bool valid_reconnect_config(const WifiConfig& config)
		{
			return config.reconnect.initial_delay_ms != 0 &&
				config.reconnect.max_delay_ms >=
					config.reconnect.initial_delay_ms;
		}
	} // namespace

	WifiService::WifiService(
		event::EventDispatcher& event_dispatcher)
		: event_dispatcher_(event_dispatcher)
	{
	}

	WifiService::~WifiService() noexcept
	{
		// The owner must ensure that no public method is executing
		// concurrently with destruction.
		cleanup();
	}

	esp_err_t WifiService::start()
	{
		if (service_started_)
			return ESP_ERR_INVALID_STATE;

		if (!valid_reconnect_config(config_))
			return ESP_ERR_INVALID_ARG;

		if (!config_.station.ssid.empty() &&
			!valid_station_config(config_.station))
		{
			return ESP_ERR_INVALID_ARG;
		}

		if (!valid_access_point_config(config_.access_point))
			return ESP_ERR_INVALID_ARG;

		// ESP-NETIF initialization and the default event loop are owned
		// by the application and must already exist.
		station_netif_ = esp_netif_create_default_wifi_sta();

		if (station_netif_ == nullptr)
			return ESP_ERR_NO_MEM;

		access_point_netif_ = esp_netif_create_default_wifi_ap();

		if (access_point_netif_ == nullptr)
		{
			cleanup();
			return ESP_ERR_NO_MEM;
		}

		wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();

		auto result = esp_wifi_init(&init_config);

		if (result != ESP_OK)
		{
			cleanup();
			return result;
		}

		// Register handlers before any operation can start the driver.
		result = esp_event_handler_instance_register(
			WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			&WifiService::esp_event_handler,
			this,
			&wifi_event_handler_);

		if (result != ESP_OK)
		{
			cleanup();
			return result;
		}

		result = esp_event_handler_instance_register(
			IP_EVENT,
			ESP_EVENT_ANY_ID,
			&WifiService::esp_event_handler,
			this,
			&ip_event_handler_);

		if (result != ESP_OK)
		{
			cleanup();
			return result;
		}

		reconnect_timer_ = xTimerCreate(
			"wifi_retry",
			std::max<TickType_t>(
				pdMS_TO_TICKS(config_.reconnect.initial_delay_ms),
				1),
			pdFALSE,
			this,
			&WifiService::reconnect_timer_callback);

		if (reconnect_timer_ == nullptr)
		{
			cleanup();
			return ESP_ERR_NO_MEM;
		}

		result = esp_wifi_set_mode(WIFI_MODE_APSTA);

		if (result == ESP_OK)
			result = configure_station(config_.station);

		if (result == ESP_OK)
			result = configure_access_point(config_.access_point);

		if (result == ESP_OK)
			result = esp_wifi_set_mode(WIFI_MODE_NULL);

		if (result != ESP_OK)
		{
			cleanup();
			return result;
		}
		
		station_enabled_ = false;
		access_point_enabled_ = false;
		station_state_ = StationState::Stopped;
		access_point_state_ = AccessPointState::Stopped;

		reconnect_suppressed_ = false;
		reconnect_threshold_reached_ = false;
		consecutive_retries_ = 0;
		reconnect_delay_ms_ = 0;

		service_started_ = true;
		return ESP_OK;
	}

	esp_err_t WifiService::stop()
	{
		if (!service_started_)
			return ESP_ERR_INVALID_STATE;

		cancel_reconnect();

		// If this fails, retain resources and state so the caller can
		// retry shutdown rather than destroying live driver resources.
		const auto result = stop_driver();

		if (result != ESP_OK)
			return result;

		station_enabled_ = false;
		access_point_enabled_ = false;

		station_state_ = StationState::Stopped;
		access_point_state_ = AccessPointState::Stopped;

		reconnect_suppressed_ = true;
		reconnect_threshold_reached_ = false;
		consecutive_retries_ = 0;
		reconnect_delay_ms_ = 0;

		cleanup();
		return ESP_OK;
	}

	esp_err_t WifiService::configure_wifi_service(WifiConfig config)
	{
		if (!valid_reconnect_config(config))
			return ESP_ERR_INVALID_ARG;

		if (!config.station.ssid.empty() &&
			!valid_station_config(config.station))
		{
			return ESP_ERR_INVALID_ARG;
		}

		if (!valid_access_point_config(config.access_point))
			return ESP_ERR_INVALID_ARG;

		// Validate first, then commit locally if the service is not
		// initialized. No driver calls are made in this state.
		if (!service_started_)
		{
			config_ = std::move(config);
			return ESP_OK;
		}

		// Configuration changes while either interface is active are
		// deliberately rejected. Stop the interfaces, configure, then
		// start the desired interfaces explicitly.
		if (station_enabled_ || access_point_enabled_)
			return ESP_ERR_INVALID_STATE;

		const auto old_config = config_;

		auto result = configure_station(config.station);

		if (result == ESP_OK)
			result = configure_access_point(config.access_point);

		if (result != ESP_OK)
		{
			const auto restore_sta = configure_station(old_config.station);
			const auto restore_ap = configure_access_point(old_config.access_point);

			if (restore_sta != ESP_OK || restore_ap != ESP_OK)
			{
				ESP_LOGE(
					TAG,
					"Failed to restore previous Wi-Fi configuration");
			}

			return result;
		}

		config_ = std::move(config);

		cancel_reconnect();
		reconnect_suppressed_ = false;
		reconnect_threshold_reached_ = false;
		consecutive_retries_ = 0;
		reconnect_delay_ms_ = 0;

		return ESP_OK;
	}

	esp_err_t WifiService::start_station()
	{
		if (!service_started_)
			return ESP_ERR_INVALID_STATE;

		if (!valid_station_config(config_.station))
			return ESP_ERR_INVALID_ARG;

		if (station_enabled_)
			return ESP_OK;

		station_enabled_ = true;
		reconnect_suppressed_ = false;

		auto result = update_mode();

		if (result == ESP_OK)
			result = ensure_driver_started();

		if (result != ESP_OK)
		{
			station_enabled_ = false;

			// Restore the mode corresponding to the previous desired state.
			const auto rollback = update_mode();

			if (rollback != ESP_OK)
			{
				ESP_LOGE(TAG, "Failed to restore Wi-Fi mode after STA start failure");
			}

			return result;
		}

		station_state_ = StationState::Connecting;
		return ESP_OK;
	}

	esp_err_t WifiService::stop_station()
	{
		if (!service_started_)
			return ESP_ERR_INVALID_STATE;

		if (!station_enabled_)
			return ESP_OK;

		reconnect_suppressed_ = true;
		cancel_reconnect();

		const auto disconnect_result = esp_wifi_disconnect();

		if (disconnect_result != ESP_OK &&
			disconnect_result != ESP_ERR_WIFI_NOT_CONNECT &&
			disconnect_result != ESP_ERR_WIFI_NOT_STARTED)
		{
			reconnect_suppressed_ = false;
			return disconnect_result;
		}

		station_enabled_ = false;

		auto result = update_mode();

		if (result != ESP_OK)
		{
			station_enabled_ = true;
			reconnect_suppressed_ = false;
			return result;
		}

		if (!access_point_enabled_)
		{
			result = stop_driver();

			if (result != ESP_OK)
			{
				// Best-effort restoration of the station mode.
				station_enabled_ = true;
				const auto rollback = update_mode();

				if (rollback == ESP_OK)
					(void)ensure_driver_started();

				reconnect_suppressed_ = false;
				return result;
			}
		}

		station_state_ = StationState::Stopped;
		return ESP_OK;
	}

	esp_err_t WifiService::start_access_point()
	{
		if (!service_started_)
			return ESP_ERR_INVALID_STATE;

		if (!valid_access_point_config(config_.access_point))
			return ESP_ERR_INVALID_ARG;

		if (access_point_enabled_)
			return ESP_OK;

		access_point_enabled_ = true;

		auto result = update_mode();

		if (result == ESP_OK)
			result = ensure_driver_started();

		if (result != ESP_OK)
		{
			access_point_enabled_ = false;
			const auto rollback = update_mode();

			if (rollback != ESP_OK)
			{
				ESP_LOGE(TAG, "Failed to restore Wi-Fi mode after AP start failure");
			}

			return result;
		}

		return ESP_OK;
	}

	esp_err_t WifiService::stop_access_point()
	{
		if (!service_started_)
			return ESP_ERR_INVALID_STATE;

		if (!access_point_enabled_)
			return ESP_OK;

		access_point_enabled_ = false;

		auto result = update_mode();

		if (result != ESP_OK)
		{
			access_point_enabled_ = true;
			return result;
		}

		if (!station_enabled_)
		{
			result = stop_driver();

			if (result != ESP_OK)
			{
				access_point_enabled_ = true;
				const auto rollback = update_mode();

				if (rollback == ESP_OK)
					(void)ensure_driver_started();

				return result;
			}
		}

		return ESP_OK;
	}

	void WifiService::set_reconnect_enabled(bool enabled)
	{
		config_.reconnect.enabled = enabled;

		if (!enabled)
		{
			cancel_reconnect();
			return;
		}

		if (station_enabled_ &&
			station_state_ == StationState::Disconnected)
		{
			reconnect_suppressed_ = false;
			reconnect_threshold_reached_ = false;
			consecutive_retries_ = 0;
			schedule_reconnect();
		}
	}

	esp_err_t WifiService::configure_station(const StationConfig& station)
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

	esp_err_t WifiService::update_mode()
	{
		if (!service_started_)
			return ESP_ERR_INVALID_STATE;

		wifi_mode_t mode = WIFI_MODE_NULL;

		if (station_enabled_ && access_point_enabled_)
			mode = WIFI_MODE_APSTA;
		else if (station_enabled_)
			mode = WIFI_MODE_STA;
		else if (access_point_enabled_)
			mode = WIFI_MODE_AP;

		const auto result = esp_wifi_set_mode(mode);

		if (result != ESP_OK)
		{
			ESP_LOGE(
				TAG,
				"Failed to set Wi-Fi mode: %s",
				esp_err_to_name(result));
		}

		return result;
	}

	esp_err_t WifiService::ensure_driver_started()
	{
		if (driver_started_)
			return ESP_OK;

		const auto result = esp_wifi_start();

		if (result == ESP_OK)
			driver_started_ = true;

		return result;
	}

	esp_err_t WifiService::stop_driver()
	{
		if (!driver_started_)
			return ESP_OK;

		const auto result = esp_wifi_stop();

		if (result == ESP_OK)
			driver_started_ = false;

		return result;
	}

	void WifiService::reconnect_timer_callback(TimerHandle_t timer)
	{
		auto* self = static_cast<WifiService*>(pvTimerGetTimerID(timer));

		if (self != nullptr)
			self->handle_reconnect_timer();
	}

	void WifiService::schedule_reconnect()
	{
		if (!service_started_ ||
			!driver_started_ ||
			!station_enabled_ ||
			!config_.reconnect.enabled ||
			reconnect_suppressed_ ||
			reconnect_threshold_reached_ ||
			reconnect_timer_ == nullptr)
		{
			return;
		}

		if (reconnect_delay_ms_ == 0)
		{
			reconnect_delay_ms_ = config_.reconnect.initial_delay_ms;
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
			ESP_LOGW(TAG, "Failed to schedule reconnect");
	}

	void WifiService::cancel_reconnect()
	{
		if (reconnect_timer_ != nullptr)
		{
			if (xTimerStop(reconnect_timer_, 0) != pdPASS)
				ESP_LOGW(TAG, "Failed to stop reconnect timer");
		}

		reconnect_delay_ms_ = 0;
	}

	bool WifiService::record_reconnect_failure()
	{
		if (!service_started_ ||
			!station_enabled_ ||
			!config_.reconnect.enabled ||
			reconnect_suppressed_)
		{
			return false;
		}

		if (consecutive_retries_ < std::numeric_limits<std::uint32_t>::max())
			++consecutive_retries_;

		const auto threshold =
			config_.reconnect.consecutive_failures_threshold;

		if (threshold == 0)
			return true;

		if (consecutive_retries_ % threshold == 0)
		{
			publish_wifi_reconnection_threshold_reached(consecutive_retries_);
		}

		if (config_.reconnect.stop_if_reached_threshold &&
			consecutive_retries_ >= threshold)
		{
			reconnect_threshold_reached_ = true;
			cancel_reconnect();

			ESP_LOGW(
				TAG,
				"Reconnect threshold reached (%lu failures)",
				static_cast<unsigned long>(consecutive_retries_));

			return false;
		}

		return true;
	}

	void WifiService::handle_reconnect_timer()
	{
		if (!service_started_ ||
			!driver_started_ ||
			!station_enabled_ ||
			!config_.reconnect.enabled ||
			reconnect_suppressed_ ||
			reconnect_threshold_reached_)
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

			if (record_reconnect_failure())
				schedule_reconnect();
		}
	}

	void WifiService::esp_event_handler(
		void* arg,
		esp_event_base_t event_base,
		int32_t event_id,
		void* event_data)
	{
		auto* self = static_cast<WifiService*>(arg);

		if (self == nullptr)
			return;

		if (event_base == WIFI_EVENT)
		{
			self->handle_wifi_event(event_id, event_data);
		}
		else if (event_base == IP_EVENT)
		{
			self->handle_ip_event(event_id, event_data);
		}
	}

	void WifiService::handle_wifi_event(
		std::int32_t event_id,
		void* event_data)
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

					if (record_reconnect_failure())
						schedule_reconnect();
				}
			}
			break;

		case WIFI_EVENT_STA_STOP:
			station_state_ = StationState::Stopped;
			publish_station_stopped();
			break;

		case WIFI_EVENT_STA_CONNECTED:
			if (station_enabled_ && event_data != nullptr)
			{
				const auto* info =
					static_cast<const wifi_event_sta_connected_t*>(event_data);

				station_state_ = StationState::Connected;
				publish_station_connected(*info);
			}
			break;

		case WIFI_EVENT_STA_DISCONNECTED:
			if (event_data != nullptr)
			{
				const auto* info =
					static_cast<const wifi_event_sta_disconnected_t*>(event_data);

				station_state_ = station_enabled_
					? StationState::Disconnected
					: StationState::Stopped;

				publish_station_disconnected(*info);

				if (record_reconnect_failure())
					schedule_reconnect();
			}
			break;

		case WIFI_EVENT_AP_START:
			if (access_point_enabled_)
			{
				access_point_state_ = AccessPointState::Started;
				publish_ap_started();
			}
			break;

		case WIFI_EVENT_AP_STOP:
			access_point_state_ = AccessPointState::Stopped;
			publish_ap_stopped();
			break;

		case WIFI_EVENT_AP_STACONNECTED:
			if (event_data != nullptr)
			{
				publish_ap_client_connected(
					*static_cast<const wifi_event_ap_staconnected_t*>(event_data));
			}
			break;

		case WIFI_EVENT_AP_STADISCONNECTED:
			if (event_data != nullptr)
			{
				publish_ap_client_disconnected(
					*static_cast<const wifi_event_ap_stadisconnected_t*>(event_data));
			}
			break;

		default:
			break;
		}
	}

	void WifiService::handle_ip_event(
		std::int32_t event_id,
		void* event_data)
	{
		switch (event_id)
		{
		case IP_EVENT_STA_GOT_IP:
			if (station_enabled_ && event_data != nullptr)
			{
				const auto* info =
					static_cast<const ip_event_got_ip_t*>(event_data);

				station_state_ = StationState::Connected;

				consecutive_retries_ = 0;
				reconnect_threshold_reached_ = false;

				cancel_reconnect();

				ESP_LOGI(
					TAG,
					"Got IP: " IPSTR,
					IP2STR(&info->ip_info.ip));

				publish_station_got_ip(*info);
			}
			break;

		case IP_EVENT_STA_LOST_IP:
			if (station_enabled_)
			{
				station_state_ = StationState::Disconnected;
				publish_station_lost_ip();
				schedule_reconnect();
			}
			break;

		default:
			break;
		}
	}

	void WifiService::cleanup() noexcept
	{
		cancel_reconnect();

		if (driver_started_)
		{
			const auto result = esp_wifi_stop();

			if (result != ESP_OK)
			{
				ESP_LOGE(
					TAG,
					"Failed to stop Wi-Fi driver during cleanup: %s",
					esp_err_to_name(result));

				// Do not destroy netifs or deinitialize the driver while
				// its shutdown has failed.
				return;
			}

			driver_started_ = false;
		}

		if (reconnect_timer_ != nullptr)
		{
			if (xTimerDelete(reconnect_timer_, portMAX_DELAY) != pdPASS)
			{
				ESP_LOGE(TAG, "Failed to delete reconnect timer");
				return;
			}

			reconnect_timer_ = nullptr;
		}

		if (ip_event_handler_ != nullptr)
		{
			const auto result = esp_event_handler_instance_unregister(
				IP_EVENT,
				ESP_EVENT_ANY_ID,
				ip_event_handler_);

			if (result != ESP_OK)
				ESP_LOGE(TAG, "Failed to unregister IP event handler");

			ip_event_handler_ = nullptr;
		}

		if (wifi_event_handler_ != nullptr)
		{
			const auto result = esp_event_handler_instance_unregister(
				WIFI_EVENT,
				ESP_EVENT_ANY_ID,
				wifi_event_handler_);

			if (result != ESP_OK)
				ESP_LOGE(TAG, "Failed to unregister Wi-Fi event handler");

			wifi_event_handler_ = nullptr;
		}

		if (station_netif_ != nullptr)
		{
			esp_netif_destroy(station_netif_);
			station_netif_ = nullptr;
		}

		if (access_point_netif_ != nullptr)
		{
			esp_netif_destroy(access_point_netif_);
			access_point_netif_ = nullptr;
		}

		if (service_started_ || driver_started_)
		{
			const auto result = esp_wifi_deinit();

			if (result != ESP_OK)
			{
				ESP_LOGE(
					TAG,
					"Failed to deinitialize Wi-Fi driver: %s",
					esp_err_to_name(result));
			}
		}

		service_started_ = false;
		driver_started_ = false;
		station_enabled_ = false;
		access_point_enabled_ = false;

		station_state_ = StationState::Stopped;
		access_point_state_ = AccessPointState::Stopped;

		reconnect_suppressed_ = false;
		reconnect_threshold_reached_ = false;
		consecutive_retries_ = 0;
		reconnect_delay_ms_ = 0;
	}

	esp_netif_ip_info_t WifiService::station_ip_info() const noexcept
	{
		esp_netif_ip_info_t info{};

		if (station_netif_ != nullptr)
			(void)esp_netif_get_ip_info(station_netif_, &info);

		return info;
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
		const wifi_event_sta_connected_t& info)
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
		const wifi_event_sta_disconnected_t& info)
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiStationDisconnected{
				.reason = info.reason});
	}

	void WifiService::publish_station_got_ip(
		const ip_event_got_ip_t& info)
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
		const wifi_event_ap_staconnected_t& info)
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiApClientConnected{
				.mac = make_mac(info.mac)});
	}

	void WifiService::publish_ap_client_disconnected(
		const wifi_event_ap_stadisconnected_t& info)
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiApClientDisconnected{
				.mac = make_mac(info.mac)});
	}

	void WifiService::publish_wifi_reconnection_threshold_reached(
		std::uint32_t consecutive_failures)
	{
		event_dispatcher_.publish(
			event::Source::Network,
			event::WifiReconnectThresholdReached{
				.consecutive_failures = consecutive_failures});
	}

} // namespace internet