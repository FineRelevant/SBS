#include "internet.hpp"

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

		ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));

		ESP_ERROR_CHECK(esp_event_handler_instance_register(
			WIFI_EVENT,	
			ESP_EVENT_ANY_ID, 
			&InternetService::esp_event_handler, 
			this, 
			&_wifi_event_handler));

		ESP_ERROR_CHECK(esp_event_handler_instance_register(
			IP_EVENT, 
			IP_EVENT_STA_GOT_IP, 
			&InternetService::esp_event_handler, 
			this, 
			&_ip_event_handler));
	}

	InternetService::~InternetService()
	{
		esp_event_handler_instance_unregister(IP_EVENT,	IP_EVENT_STA_GOT_IP, _ip_event_handler);

		esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,	_wifi_event_handler);

		esp_wifi_stop();
		esp_wifi_deinit();

		if (_netif != nullptr)
			esp_netif_destroy(_netif);
	}

	void InternetService::start()
	{
		configure_wifi();

		ESP_ERROR_CHECK(esp_wifi_start());

		connect();
	}

	void InternetService::stop()
	{
		disconnect();

		ESP_ERROR_CHECK(esp_wifi_stop());

		_state = State::Disconnected;
	}

	void InternetService::connect()
	{
		_state = State::Connecting;

		ESP_ERROR_CHECK(esp_wifi_connect());
	}

	void InternetService::disconnect()
	{
		esp_err_t result = esp_wifi_disconnect();

		if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_CONNECT)
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

	void InternetService::esp_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
	{
		auto *self = static_cast<InternetService *>(arg);

		self->handle_event(event_base, event_id, event_data);
	}

	void InternetService::handle_event(esp_event_base_t event_base, std::int32_t event_id, void *event_data)
	{
		if (event_base == WIFI_EVENT)
		{
			switch (event_id)
			{
			case WIFI_EVENT_STA_START:
				// a
				break;

			case WIFI_EVENT_STA_DISCONNECTED:
				_state = State::Disconnected;
				// a
				break;

			default:
				// a
				break;
			}

			return;
		}

		if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
		{
			auto *event = static_cast<ip_event_got_ip_t *>(event_data);

			_state = InternetService::State::Connected;

			ESP_LOGI(
				TAG,
				"Got IP: " IPSTR,
				IP2STR(&event->ip_info.ip));
		}
	}
}