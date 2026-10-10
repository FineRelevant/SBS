#include "application.hpp"

#include "esp_log.h"

Application::Application()
	: _event_queue(), _event_dispatcher(_event_queue), _event_dispatcher_task(_event_dispatcher, "event_dispatcher", 5),
	  _internet_config {
		  .station{
			  .ssid = "HomeWiFi",
			  .password = "your-wifi-password",
		  },
		  .access_point{
			  .ssid = "Device-Setup",
			  .password = "setup-password",
			  .channel = 1,
			  .max_connections = 4,
			  .hidden = false,
		  },
		  .reconnect{
			  .enabled = true,
			  .initial_delay_ms = 1000,
			  .max_delay_ms = 30000,
		  },
		  .provisioning{
			  .enabled = true,
			  .start_if_unconfigured = true,
		  },
	  },
	  _wifi_service(_internet_config, _event_dispatcher)
// etc
{}

void Application::initialize()
{
	ESP_ERROR_CHECK(initialize_nvs());
	ESP_ERROR_CHECK(initialize_networking());
}

void Application::start()
{
	ESP_LOGI("Application", "Starting event dispatcher");
	_event_dispatcher_task.start();

	const esp_err_t wifi_result = _wifi_service.start();
	ESP_LOGI("Application", "Wi-Fi start result: %s",
			 esp_err_to_name(wifi_result));
	ESP_ERROR_CHECK(wifi_result);

	const esp_err_t ap_result = _wifi_service.start_access_point();
	ESP_LOGI("Application", "AP start result: %s",
			 esp_err_to_name(ap_result));
	ESP_ERROR_CHECK(ap_result);

	ESP_LOGI("Application", "Application startup complete");
}