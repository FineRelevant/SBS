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
		InternetConfig config,
		event::EventDispatcher &event_dispatcher)
		: _config(std::move(config)),
		  _event_dispatcher(event_dispatcher)
	{
	}

	WifiService::~WifiService() noexcept
	{
		// The application must destroy the service in task context,
		// before deinitializing the ESP-IDF Wi-Fi driver.

		cancel_reconnect();

		if (_reconnect_timer != nullptr)
		{
			xTimerDelete(_reconnect_timer, portMAX_DELAY);
			_reconnect_timer = nullptr;
		}

		if (_started)
		{
			esp_wifi_stop();
			_started = false;
		}

		if (_ip_event_handler != nullptr)
		{
			esp_event_handler_instance_unregister(
				IP_EVENT,
				ESP_EVENT_ANY_ID,
				_ip_event_handler);

			_ip_event_handler = nullptr;
		}

		if (_wifi_event_handler != nullptr)
		{
			esp_event_handler_instance_unregister(
				WIFI_EVENT,
				ESP_EVENT_ANY_ID,
				_wifi_event_handler);

			_wifi_event_handler = nullptr;
		}

		if (_access_point_netif != nullptr)
		{
			esp_netif_destroy(_access_point_netif);
			_access_point_netif = nullptr;
		}

		if (_station_netif != nullptr)
		{
			esp_netif_destroy(_station_netif);
			_station_netif = nullptr;
		}
	}

	esp_err_t WifiService::start()
	{
		if (_started)
			return ESP_ERR_INVALID_STATE;

		if (!valid_access_point_config(_config.access_point))
			return ESP_ERR_INVALID_ARG;

		if (_config.reconnect.initial_delay_ms == 0 ||
			_config.reconnect.max_delay_ms <
				_config.reconnect.initial_delay_ms)
		{
			return ESP_ERR_INVALID_ARG;
		}

		_station_netif = esp_netif_create_default_wifi_sta();

		if (_station_netif == nullptr)
			return ESP_ERR_NO_MEM;

		_access_point_netif = esp_netif_create_default_wifi_ap();

		if (_access_point_netif == nullptr)
		{
			esp_netif_destroy(_station_netif);
			_station_netif = nullptr;
			return ESP_ERR_NO_MEM;
		}

		// Both interfaces are configured below.
		auto result = esp_wifi_set_mode(WIFI_MODE_APSTA);

		if (result != ESP_OK)
			goto cleanup;

		result = configure_station();

		if (result != ESP_OK)
			goto cleanup;

		result = configure_access_point();

		if (result != ESP_OK)
			goto cleanup;

		_station_enabled = valid_station_config(_config.station);

		_provisioning_active =
			!_station_enabled &&
			_config.provisioning.enabled &&
			_config.provisioning.start_if_unconfigured;

		_access_point_enabled = _provisioning_active;

		result = update_mode();

		if (result != ESP_OK)
			goto cleanup;

		_reconnect_timer = xTimerCreate(
			"wifi_retry",
			pdMS_TO_TICKS(_config.reconnect.initial_delay_ms),
			pdFALSE,
			this,
			&WifiService::reconnect_timer_callback);

		if (_reconnect_timer == nullptr)
		{
			result = ESP_ERR_NO_MEM;
			goto cleanup;
		}

		result = esp_event_handler_instance_register(
			WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			&WifiService::esp_event_handler,
			this,
			&_wifi_event_handler);

		if (result != ESP_OK)
			goto cleanup;

		result = esp_event_handler_instance_register(
			IP_EVENT,
			ESP_EVENT_ANY_ID,
			&WifiService::esp_event_handler,
			this,
			&_ip_event_handler);

		if (result != ESP_OK)
			goto cleanup;

		result = esp_wifi_start();

		if (result != ESP_OK)
			goto cleanup;

		_started = true;

		if (_station_enabled)
			_station_state = StationState::Connecting;
		else
			_station_state = StationState::Stopped;

		if (_provisioning_active)
		{
			_event_dispatcher.publish(
				event::Source::Network,
				event::WifiProvisioningStarted{});
		}

		return ESP_OK;

	cleanup:
		if (_started)
		{
			esp_wifi_stop();
			_started = false;
		}

		if (_ip_event_handler != nullptr)
		{
			esp_event_handler_instance_unregister(
				IP_EVENT,
				ESP_EVENT_ANY_ID,
				_ip_event_handler);

			_ip_event_handler = nullptr;
		}

		if (_wifi_event_handler != nullptr)
		{
			esp_event_handler_instance_unregister(
				WIFI_EVENT,
				ESP_EVENT_ANY_ID,
				_wifi_event_handler);

			_wifi_event_handler = nullptr;
		}

		if (_reconnect_timer != nullptr)
		{
			xTimerDelete(_reconnect_timer, portMAX_DELAY);
			_reconnect_timer = nullptr;
		}

		if (_access_point_netif != nullptr)
		{
			esp_netif_destroy(_access_point_netif);
			_access_point_netif = nullptr;
		}

		if (_station_netif != nullptr)
		{
			esp_netif_destroy(_station_netif);
			_station_netif = nullptr;
		}

		_station_enabled = false;
		_access_point_enabled = false;
		_provisioning_active = false;
		_station_state = StationState::Stopped;
		_access_point_state = AccessPointState::Stopped;

		return result;
	}

	esp_err_t WifiService::stop()
	{
		if (!_started)
			return ESP_ERR_INVALID_STATE;

		cancel_reconnect();

		const auto result = esp_wifi_stop();

		if (result != ESP_OK)
			return result;

		_started = false;
		_station_enabled = false;
		_access_point_enabled = false;
		_provisioning_active = false;
		_reconnect_suppressed = true;

		_station_state = StationState::Stopped;
		_access_point_state = AccessPointState::Stopped;

		return ESP_OK;
	}

	esp_err_t WifiService::start_station()
	{
		if (!_started)
			return ESP_ERR_INVALID_STATE;

		if (!valid_station_config(_config.station))
			return ESP_ERR_INVALID_ARG;

		if (_station_enabled)
			return ESP_OK;

		_station_enabled = true;
		_reconnect_suppressed = false;

		const auto result = update_mode();

		if (result != ESP_OK)
		{
			_station_enabled = false;
			return result;
		}

		_station_state = StationState::Connecting;

		return connect();
	}

	esp_err_t WifiService::stop_station()
	{
		if (!_started)
			return ESP_ERR_INVALID_STATE;

		if (!_station_enabled)
			return ESP_OK;

		_reconnect_suppressed = true;
		cancel_reconnect();

		const auto disconnect_result = esp_wifi_disconnect();

		if (disconnect_result != ESP_OK &&
			disconnect_result != ESP_ERR_WIFI_NOT_CONNECT)
		{
			_reconnect_suppressed = false;
			return disconnect_result;
		}

		_station_enabled = false;

		const auto result = update_mode();

		if (result != ESP_OK)
		{
			_station_enabled = true;
			_reconnect_suppressed = false;
			return result;
		}

		_station_state = StationState::Stopped;
		publish_station_stopped();

		return ESP_OK;
	}

	esp_err_t WifiService::connect()
	{
		if (!_started || !_station_enabled)
			return ESP_ERR_INVALID_STATE;

		if (!valid_station_config(_config.station))
			return ESP_ERR_INVALID_ARG;

		if (_station_state == StationState::Connected ||
			_station_state == StationState::Connecting)
		{
			return ESP_OK;
		}

		_reconnect_suppressed = false;
		cancel_reconnect();

		_station_state = StationState::Connecting;

		const auto result = esp_wifi_connect();

		if (result != ESP_OK)
		{
			_station_state = StationState::Disconnected;
			schedule_reconnect();
		}

		return result;
	}

	esp_err_t WifiService::disconnect()
	{
		if (!_started || !_station_enabled)
			return ESP_ERR_INVALID_STATE;

		_reconnect_suppressed = true;
		cancel_reconnect();

		const auto result = esp_wifi_disconnect();

		if (result != ESP_OK &&
			result != ESP_ERR_WIFI_NOT_CONNECT)
		{
			_reconnect_suppressed = false;
			return result;
		}

		_station_state = StationState::Disconnected;

		return ESP_OK;
	}

	esp_err_t WifiService::start_access_point()
	{
		if (!_started)
			return ESP_ERR_INVALID_STATE;

		if (_access_point_enabled)
			return ESP_OK;

		_access_point_enabled = true;

		const auto result = update_mode();

		if (result != ESP_OK)
			_access_point_enabled = false;

		return result;
	}

	esp_err_t WifiService::stop_access_point()
	{
		if (!_started)
			return ESP_ERR_INVALID_STATE;

		if (!_access_point_enabled)
			return ESP_OK;

		if (_provisioning_active)
			return ESP_ERR_INVALID_STATE;

		_access_point_enabled = false;

		const auto result = update_mode();

		if (result != ESP_OK)
			_access_point_enabled = true;

		return result;
	}

	esp_err_t WifiService::configure_station(StationConfig config)
	{
		if (!valid_station_config(config))
			return ESP_ERR_INVALID_ARG;

		const auto previous_config = _config.station;
		const bool was_enabled = _station_enabled;

		// Apply the new configuration before committing it.
		wifi_config_t driver_config{};

		if (!copy_string(config.ssid, driver_config.sta.ssid) ||
			!copy_string(config.password, driver_config.sta.password))
		{
			return ESP_ERR_INVALID_ARG;
		}

		if (_started && was_enabled)
		{
			const auto disconnect_result = esp_wifi_disconnect();

			if (disconnect_result != ESP_OK &&
				disconnect_result != ESP_ERR_WIFI_NOT_CONNECT)
			{
				return disconnect_result;
			}
		}

		_config.station = std::move(config);

		if (_started)
		{
			const auto result = esp_wifi_set_config(
				WIFI_IF_STA,
				&driver_config);

			if (result != ESP_OK)
			{
				_config.station = previous_config;
				return result;
			}
		}

		cancel_reconnect();
		_reconnect_suppressed = false;
		_station_enabled = true;
		_station_state = StationState::Connecting;

		if (_provisioning_active)
		{
			const auto result = end_provisioning();

			if (result != ESP_OK)
				return result;
		}
		else if (_started)
		{
			const auto result = update_mode();

			if (result != ESP_OK)
				return result;
		}

		if (!_started)
			return ESP_OK;

		return connect();
	}

	esp_err_t WifiService::begin_provisioning()
	{
		if (!_started)
			return ESP_ERR_INVALID_STATE;

		if (!_config.provisioning.enabled)
			return ESP_ERR_NOT_SUPPORTED;

		if (_provisioning_active)
			return ESP_OK;

		cancel_reconnect();

		_provisioning_active = true;
		_access_point_enabled = true;

		const auto result = update_mode();

		if (result != ESP_OK)
		{
			_provisioning_active = false;
			_access_point_enabled = false;
			return result;
		}

		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiProvisioningStarted{});

		return ESP_OK;
	}

	esp_err_t WifiService::end_provisioning()
	{
		if (!_started)
			return ESP_ERR_INVALID_STATE;

		if (!_provisioning_active)
			return ESP_OK;

		_provisioning_active = false;

		// Keep the AP enabled if it was explicitly enabled for another
		// application purpose. A production application may track that
		// requirement separately from provisioning.
		_access_point_enabled = false;

		const auto result = update_mode();

		if (result != ESP_OK)
		{
			_provisioning_active = true;
			_access_point_enabled = true;
			return result;
		}

		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiProvisioningStopped{});

		return ESP_OK;
	}

	void WifiService::set_reconnect_enabled(bool enabled)
	{
		_config.reconnect.enabled = enabled;

		if (!enabled)
		{
			cancel_reconnect();
			return;
		}

		if (_started &&
			_station_enabled &&
			_station_state == StationState::Disconnected)
		{
			schedule_reconnect();
		}
	}

	esp_netif_ip_info_t WifiService::station_ip_info() const noexcept
	{
		esp_netif_ip_info_t info{};

		if (_station_netif != nullptr)
			esp_netif_get_ip_info(_station_netif, &info);

		return info;
	}

	esp_err_t WifiService::configure_station()
	{
		wifi_config_t config{};

		if (!copy_string(_config.station.ssid, config.sta.ssid) ||
			!copy_string(_config.station.password, config.sta.password))
		{
			return ESP_ERR_INVALID_ARG;
		}

		return esp_wifi_set_config(WIFI_IF_STA, &config);
	}

	esp_err_t WifiService::configure_access_point()
	{
		wifi_config_t config{};

		if (!copy_string(_config.access_point.ssid, config.ap.ssid) ||
			!copy_string(_config.access_point.password, config.ap.password))
		{
			return ESP_ERR_INVALID_ARG;
		}

		config.ap.channel = _config.access_point.channel;
		config.ap.max_connection = _config.access_point.max_connections;
		config.ap.ssid_hidden = _config.access_point.hidden;

		config.ap.authmode = _config.access_point.password.empty()
								 ? WIFI_AUTH_OPEN
								 : WIFI_AUTH_WPA2_PSK;

		return esp_wifi_set_config(WIFI_IF_AP, &config);
	}

	esp_err_t WifiService::update_mode()
	{
		wifi_mode_t mode = WIFI_MODE_NULL;

		if (_station_enabled && _access_point_enabled)
			mode = WIFI_MODE_APSTA;
		else if (_station_enabled)
			mode = WIFI_MODE_STA;
		else if (_access_point_enabled)
			mode = WIFI_MODE_AP;

		return esp_wifi_set_mode(mode);
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
		if (!_started ||
			!_station_enabled ||
			!_config.reconnect.enabled ||
			_reconnect_suppressed ||
			_provisioning_active ||
			_reconnect_timer == nullptr)
		{
			return;
		}

		if (_reconnect_delay_ms == 0)
		{
			_reconnect_delay_ms =
				_config.reconnect.initial_delay_ms;
		}
		else
		{
			const auto maximum = _config.reconnect.max_delay_ms;

			_reconnect_delay_ms =
				_reconnect_delay_ms > maximum / 2
					? maximum
					: std::min(_reconnect_delay_ms * 2, maximum);
		}

		const auto ticks = std::max<TickType_t>(
			pdMS_TO_TICKS(_reconnect_delay_ms),
			1);

		if (xTimerChangePeriod(_reconnect_timer, ticks, 0) != pdPASS)
		{
			ESP_LOGW(TAG, "Failed to schedule reconnect");
		}
	}

	void WifiService::cancel_reconnect()
	{
		_reconnect_delay_ms = 0;

		if (_reconnect_timer != nullptr)
			xTimerStop(_reconnect_timer, 0);
	}

	void WifiService::handle_reconnect_timer()
	{
		if (!_started ||
			!_station_enabled ||
			!_config.reconnect.enabled ||
			_reconnect_suppressed ||
			_provisioning_active)
		{
			return;
		}

		if (_station_state == StationState::Connected ||
			_station_state == StationState::Connecting)
		{
			return;
		}

		_station_state = StationState::Connecting;

		const auto result = esp_wifi_connect();

		if (result != ESP_OK)
		{
			_station_state = StationState::Disconnected;

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
			if (_station_enabled)
			{
				_station_state = StationState::Connecting;
				publish_station_started();

				const auto result = esp_wifi_connect();

				if (result != ESP_OK)
				{
					_station_state = StationState::Disconnected;
					schedule_reconnect();
				}
			}
			break;

		case WIFI_EVENT_STA_STOP:
			_station_state = StationState::Stopped;
			publish_station_stopped();
			break;

		case WIFI_EVENT_STA_CONNECTED: {
			const auto *info =
				static_cast<const wifi_event_sta_connected_t *>(event_data);

			_station_state = StationState::Connected;
			publish_station_connected(*info);
			break;
		}

		case WIFI_EVENT_STA_DISCONNECTED: {
			const auto *info =
				static_cast<const wifi_event_sta_disconnected_t *>(event_data);

			_station_state = _station_enabled
								 ? StationState::Disconnected
								 : StationState::Stopped;

			publish_station_disconnected(*info);
			schedule_reconnect();
			break;
		}

		case WIFI_EVENT_AP_START:
			_access_point_state = AccessPointState::Started;
			publish_ap_started();
			break;

		case WIFI_EVENT_AP_STOP:
			_access_point_state = AccessPointState::Stopped;
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

			_station_state = StationState::Connected;

			cancel_reconnect();

			ESP_LOGI(
				TAG,
				"Got IP: " IPSTR,
				IP2STR(&info->ip_info.ip));

			publish_station_got_ip(*info);
			break;
		}

		case IP_EVENT_STA_LOST_IP:
			if (_station_enabled)
				_station_state = StationState::Disconnected;

			publish_station_lost_ip();
			schedule_reconnect();
			break;

		default:
			break;
		}
	}

	void WifiService::publish_station_started()
	{
		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiStationStarted{});
	}

	void WifiService::publish_station_stopped()
	{
		_event_dispatcher.publish(
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

		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiStationConnected{
				.bssid = bssid,
				.rssi = rssi,
				.channel = info.channel});
	}

	void WifiService::publish_station_disconnected(
		const wifi_event_sta_disconnected_t &info)
	{
		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiStationDisconnected{
				.reason = info.reason});
	}

	void WifiService::publish_station_got_ip(
		const ip_event_got_ip_t &info)
	{
		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiStationGotIp{
				.address = info.ip_info.ip.addr,
				.netmask = info.ip_info.netmask.addr,
				.gateway = info.ip_info.gw.addr});
	}

	void WifiService::publish_station_lost_ip()
	{
		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiStationLostIp{});
	}

	void WifiService::publish_ap_started()
	{
		event::MacAddress mac{};

		if (esp_wifi_get_mac(WIFI_IF_AP, mac.data()) != ESP_OK)
			mac.fill(0);

		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiApStarted{
				.mac = mac});
	}

	void WifiService::publish_ap_stopped()
	{
		event::MacAddress mac{};

		if (esp_wifi_get_mac(WIFI_IF_AP, mac.data()) != ESP_OK)
			mac.fill(0);

		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiApStopped{
				.mac = mac});
	}

	void WifiService::publish_ap_client_connected(
		const wifi_event_ap_staconnected_t &info)
	{
		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiApClientConnected{
				.mac = make_mac(info.mac)});
	}

	void WifiService::publish_ap_client_disconnected(
		const wifi_event_ap_stadisconnected_t &info)
	{
		_event_dispatcher.publish(
			event::Source::Network,
			event::WifiApClientDisconnected{
				.mac = make_mac(info.mac)});
	}
}