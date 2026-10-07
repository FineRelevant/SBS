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
	
		InternetService(InternetConfig& config, event::EventDispatcher& event_dispatcher)
			: _config(config), _event_dispatcher(event_dispatcher)
		{}
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