#include "internet.hpp"

#include <algorithm>

#include "esp_log.h"

namespace internet
{
	namespace 
	{
		constexpr char TAG[] = "InternetService";
	}
	InternetService::InternetService()
	{
		ESP_ERROR_CHECK(esp_netif_init());

		ESP_ERROR_CHECK(
			esp_event_loop_create_default());

		_netif = esp_netif_create_default_wifi_sta();

		wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();

		ESP_ERROR_CHECK(
			esp_wifi_init(&wifi_config));

		ESP_ERROR_CHECK(
			esp_event_handler_instance_register(
				WIFI_EVENT,
				ESP_EVENT_ANY_ID,
				&InternetService::esp_event_handler,
				this,
				&_wifi_event_handler));

		ESP_ERROR_CHECK(
			esp_event_handler_instance_register(
				IP_EVENT,
				ESP_EVENT_ANY_ID,
				&InternetService::esp_event_handler,
				this,
				&_ip_event_handler));
	}

	InternetService::~InternetService()
	{
		esp_event_handler_instance_unregister(
			IP_EVENT,
			ESP_EVENT_ANY_ID,
			_ip_event_handler);

		esp_event_handler_instance_unregister(
			WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			_wifi_event_handler);

		esp_wifi_stop();
		esp_wifi_deinit();

		if (_netif != nullptr)
			esp_netif_destroy(_netif);
	}

	void InternetService::start()
	{
		configure_wifi();

		ESP_ERROR_CHECK(
			esp_wifi_start());

		connect();
	}

	void InternetService::stop()
	{
		disconnect();

		ESP_ERROR_CHECK(
			esp_wifi_stop());

		_state = State::Disconnected;
	}

	void InternetService::connect()
	{
		_state = State::Connecting;

		ESP_ERROR_CHECK(
			esp_wifi_connect());
	}

	void InternetService::disconnect()
	{
		const esp_err_t result =
			esp_wifi_disconnect();

		if (result != ESP_OK &&
			result != ESP_ERR_WIFI_NOT_CONNECT)
		{
			ESP_ERROR_CHECK(result);
		}

		_state = State::Disconnected;
	}

	InternetService::State InternetService::state() const noexcept
	{
		return _state;
	}

	esp_netif_ip_info_t InternetService::ip_info() const noexcept
	{
		esp_netif_ip_info_t info{};

		if (_netif != nullptr)
			esp_netif_get_ip_info(_netif, &info);

		return info;
	}

	void InternetService::configure_wifi()
	{
		wifi_config_t config{};

		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
	}

	void InternetService::esp_event_handler(
		void *arg,
		esp_event_base_t event_base,
		int32_t event_id,
		void *event_data)
	{
		auto *self =
			static_cast<InternetService *>(arg);

		self->handle_event(
			event_base,
			event_id,
			event_data);
	}

	void InternetService::handle_event(
		esp_event_base_t event_base,
		std::int32_t event_id,
		void *event_data)
	{
		if (event_base == WIFI_EVENT)
		{
			switch (event_id)
			{
			case WIFI_EVENT_STA_START: {
				_state = State::Connecting;

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = event::WifiClientStarted{};

				_event_dispatcher.publish(e);
				break;
			}

			case WIFI_EVENT_STA_CONNECTED: {
				const auto *info =
					static_cast<const wifi_event_sta_connected_t *>(event_data);

				event::WifiClientConnected data{
					.bssid = {},
					.rssi = 0,
					.channel = info->channel};

				std::copy_n(
					std::begin(info->bssid),
					data.bssid.size(),
					data.bssid.begin());

				wifi_ap_record_t ap_info{};

				if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
				{
					data.rssi = ap_info.rssi;
				}

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = data;

				_event_dispatcher.publish(e);
				break;
			}

			case WIFI_EVENT_STA_DISCONNECTED: {
				const auto *info =
					static_cast<const wifi_event_sta_disconnected_t *>(event_data);

				_state = State::Disconnected;

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = event::WifiClientDisconnected{
					.reason = info->reason};

				_event_dispatcher.publish(e);
				break;
			}

			case WIFI_EVENT_AP_START: {
				std::array<uint8_t, 6> mac{};

				if (esp_wifi_get_mac(WIFI_IF_AP, mac.data()) != ESP_OK)
					mac.fill(0);

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = event::WifiApStarted{
					.mac = mac};

				_event_dispatcher.publish(e);
				break;
			}

			case WIFI_EVENT_AP_STOP: {
				std::array<uint8_t, 6> mac{};

				if (esp_wifi_get_mac(WIFI_IF_AP, mac.data()) != ESP_OK)
					mac.fill(0);

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = event::WifiApStopped{
					.mac = mac};

				_event_dispatcher.publish(e);
				break;
			}

			case WIFI_EVENT_AP_STACONNECTED: {
				const auto *info =
					static_cast<const wifi_event_ap_staconnected_t *>(event_data);

				event::WifiApClientConnected data{
					.mac = {}};

				std::copy_n(
					std::begin(info->mac),
					data.mac.size(),
					data.mac.begin());

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = data;

				_event_dispatcher.publish(e);
				break;
			}

			case WIFI_EVENT_AP_STADISCONNECTED: {
				const auto *info =
					static_cast<const wifi_event_ap_stadisconnected_t *>(event_data);

				event::WifiApClientDisconnected data{
					.mac = {}};

				std::copy_n(
					std::begin(info->mac),
					data.mac.size(),
					data.mac.begin());

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = data;

				_event_dispatcher.publish(e);
				break;
			}

			default:
				break;
			}

			return;
		}

		if (event_base == IP_EVENT)
		{
			switch (event_id)
			{
			case IP_EVENT_STA_GOT_IP: {
				const auto *info =
					static_cast<const ip_event_got_ip_t *>(event_data);

				_state = State::Connected;

				ESP_LOGI(
					TAG,
					"Got IP: " IPSTR,
					IP2STR(&info->ip_info.ip));

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = event::WifiGotIp{
					.address = info->ip_info.ip.addr,
					.netmask = info->ip_info.netmask.addr,
					.gateway = info->ip_info.gw.addr};

				_event_dispatcher.publish(e);
				break;
			}

			case IP_EVENT_STA_LOST_IP: {
				_state = State::Disconnected;

				event::Event e{};
				e.metadata.source = event::Source::Internet;
				e.data = event::WifiLostIp{};

				_event_dispatcher.publish(e);
				break;
			}

			default:
				break;
			}

			return;
		}
	}
}