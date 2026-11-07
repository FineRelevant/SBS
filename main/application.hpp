#pragma once

#include "events.hpp"
#include "filesystem.hpp"
#include "scheduler.hpp"
#include "task.hpp"
#include "wifi.hpp"
#include "configuration.hpp"
#include "peripherals.hpp"

class Application
{
public:
	Application();

	Application(const Application &) = delete;
	Application& operator=(const Application &) = delete;
	Application(const Application&&) = delete;
	Application& operator=(const Application&&) = delete;

	~Application() = default; // Application is static

	void start();
private:
	
	// Event System
	event::EventQueue event_queue_;
	event::EventDispatcher event_dispatcher_;
	StaticTask<event::EventDispatcher> event_dispatcher_task_;

	storage::FilesystemService filesystem_service_;

	scheduler::SntpClient sntp_client_;
	scheduler::BellScheduler scheduler_;

	internet::WifiService wifi_service_;
	internet::ConfigurationService configuration_service_;

	HardwareController hardware_controller_;

	void initialize_global();

	void publish_application_state_changed(event::ApplicationState state);
};