#include "application.hpp"

#include "nvs_flash.h"

Application::Application()
	: event_queue_(), event_dispatcher_(event_queue_), event_dispatcher_task_(event_dispatcher_, "dispatcher", 5),
	filesystem_service_({
          .base_path = "/littlefs",
          .partition_label = "storage",
          .format_if_mount_failed = true,
      }),
	sntp_client_(event_dispatcher_), scheduler_(filesystem_service_, event_dispatcher_, scheduler::BellScheduler::Config{}),
	wifi_service_(event_dispatcher_),
	configuration_service_(event_dispatcher_, wifi_service_, filesystem_service_, sntp_client_, scheduler_),
	hardware_controller_(event_dispatcher_)
{}

void Application::start()
{
	initialize_global();
	ESP_ERROR_CHECK(filesystem_service_.mount());
	ESP_ERROR_CHECK(hardware_controller_.start());

	ESP_ERROR_CHECK(configuration_service_.start());


	event_dispatcher_task_.start();
}

void Application::initialize_global()
{
	ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    auto sntp_cfg = scheduler::SntpClient::Config{};

	ESP_ERROR_CHECK(sntp_client_.init(sntp_cfg));
}



void Application::publish_application_state_changed(event::ApplicationState state)
{
	event_dispatcher_.publish(event::Source::Application, 
	event::ApplicationStateChanged{
		.state = state,
	});
}