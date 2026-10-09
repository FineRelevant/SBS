#pragma once

#include "events.hpp"
#include "task.hpp"
#include "internet.hpp"

#include "esp_wifi.h"

class Application
{
public:
	Application();
	
	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;
	
	void start();
private:
	// Event System
	event::EventQueue _event_queue;
	event::EventDispatcher _event_dispatcher;
	StaticTask<event::EventDispatcher> _event_dispatcher_task;
	
	// WiFi
	
	
	[[nodiscard]]
	esp_err_t initialize_networking()
	{
		esp_err_t result = esp_netif_init();

		if (result != ESP_OK &&
			result != ESP_ERR_INVALID_STATE)
		{
			return result;
		}

		result = esp_event_loop_create_default();

		if (result != ESP_OK &&
			result != ESP_ERR_INVALID_STATE)
		{
			return result;
		}

		const wifi_init_config_t wifi_config =
			WIFI_INIT_CONFIG_DEFAULT();

		return esp_wifi_init(&wifi_config);
	}
	
	void shutdown_networking()
	{
		esp_wifi_deinit();
		esp_event_loop_delete_default();
		esp_netif_deinit();
	}
};