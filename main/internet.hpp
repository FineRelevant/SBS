#pragma once

#include <cstdint>
#include <string_view>

#include "events.hpp"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace internet
{
	struct InternetConfig
	{
		std::string_view ssid;
		std::string_view password;
	};
	
	class InternetConfigLoader
	{
	public:
		// Draft
		// TODO: implement later
		InternetConfig load_from_flash();
		InternetConfig load_from_website();
	};
	
	class InternetService
	{
	public:
		enum class State
		{
			Connected    = 0,
			Connecting,
			Disconnected,
			Failed
		};

		InternetService(InternetConfig &config, event::EventDispatcher &event_dispatcher)
			: _config(config), _event_dispatcher(event_dispatcher)
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
		
		~InternetService();
		InternetService(const InternetService&) = delete;
		InternetService& operator=(const InternetService&) = delete;
	
		void start();
		void stop();
	
		void connect();
		void disconnect();
	
		State state() const noexcept;
		esp_netif_ip_info_t ip_info() const noexcept;
	private:
		InternetConfig& _config;
		event::EventDispatcher& _event_dispatcher;
		
		State _state{State::Disconnected};
		esp_netif_t* _netif{nullptr};
		esp_event_handler_instance_t _wifi_event_handler{nullptr};
		esp_event_handler_instance_t _ip_event_handler{nullptr};
		
		static void esp_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
		void handle_event(esp_event_base_t event_base, int32_t event_id, void* event_data);

		void configure_wifi();
		void configure_station();
	};
}