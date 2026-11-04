
#pragma once

#include <cstdint>
#include <string>

#include "events.hpp"
#include "internet_config.hpp"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

namespace internet
{
    class WifiService final
    {
    public:
        enum class StationState : std::uint8_t
        {
            Stopped = 0,
            Disconnected,
            Connecting,
            Connected,
        };

        enum class AccessPointState : std::uint8_t
        {
            Stopped = 0,
            Started,
        };

        WifiService(
            event::EventDispatcher& event_dispatcher);

        ~WifiService() noexcept;

        WifiService(const WifiService&) = delete;
        WifiService& operator=(const WifiService&) = delete;
        WifiService(WifiService&&) = delete;
        WifiService& operator=(WifiService&&) = delete;

        // Initialize/release resources owned by this service.
        // Does not enable either Wi-Fi interface.
        [[nodiscard]] esp_err_t start();
        [[nodiscard]] esp_err_t stop();

        // Applies configuration without implicitly enabling interfaces.
        // May be called before start().
        [[nodiscard]] esp_err_t configure_wifi_service(WifiConfig config);

        // Explicit interface lifecycle operations.
        [[nodiscard]] esp_err_t start_station();
        [[nodiscard]] esp_err_t stop_station();

        [[nodiscard]] esp_err_t start_access_point();
        [[nodiscard]] esp_err_t stop_access_point();

        void set_reconnect_enabled(bool enabled);

		[[nodiscard]]
		bool is_started()
		{
			return service_started_;
		}

        [[nodiscard]]
        StationState station_state() const noexcept
        {
            return station_state_;
        }

        [[nodiscard]]
        AccessPointState access_point_state() const noexcept
        {
            return access_point_state_;
        }

        [[nodiscard]]
        bool reconnect_enabled() const noexcept
        {
            return config_.reconnect.enabled;
        }

        [[nodiscard]]
        esp_netif_ip_info_t station_ip_info() const noexcept;

    private:
        [[nodiscard]] esp_err_t configure_station(
            const StationConfig& config);

        [[nodiscard]] esp_err_t configure_access_point(
            const AccessPointConfig& config);

        [[nodiscard]] esp_err_t update_mode();

        [[nodiscard]] esp_err_t ensure_driver_started();
        [[nodiscard]] esp_err_t stop_driver();

        void handle_wifi_event(int32_t event_id, void* event_data);
        void handle_ip_event(int32_t event_id, void* event_data);

        static void esp_event_handler(
            void* arg,
            esp_event_base_t event_base,
            int32_t event_id,
            void* event_data);

        static void reconnect_timer_callback(TimerHandle_t timer);

        void handle_reconnect_timer();
        void schedule_reconnect();
        void cancel_reconnect();
        bool record_reconnect_failure();

        void cleanup() noexcept;

        void publish_station_started();
        void publish_station_stopped();

        void publish_station_connected(
            const wifi_event_sta_connected_t& info);

        void publish_station_disconnected(
            const wifi_event_sta_disconnected_t& info);

        void publish_station_got_ip(
            const ip_event_got_ip_t& info);

        void publish_station_lost_ip();

        void publish_ap_started();
        void publish_ap_stopped();

        void publish_ap_client_connected(
            const wifi_event_ap_staconnected_t& info);

        void publish_ap_client_disconnected(
            const wifi_event_ap_stadisconnected_t& info);

        void publish_wifi_reconnection_threshold_reached(
            std::uint32_t consecutive_failures);

    private:
        WifiConfig config_;
        event::EventDispatcher& event_dispatcher_;

        // Service resources are initialized independently of the driver.
        bool service_started_{false};
        bool driver_started_{false};

        // These represent requested interface states.
        bool station_enabled_{false};
        bool access_point_enabled_{false};

        bool reconnect_suppressed_{false};
        bool reconnect_threshold_reached_{false};

        std::uint32_t consecutive_retries_{0};
        std::uint32_t reconnect_delay_ms_{0};

        StationState station_state_{StationState::Stopped};
        AccessPointState access_point_state_{AccessPointState::Stopped};

        esp_netif_t* station_netif_{nullptr};
        esp_netif_t* access_point_netif_{nullptr};

        esp_event_handler_instance_t wifi_event_handler_{nullptr};
        esp_event_handler_instance_t ip_event_handler_{nullptr};

        TimerHandle_t reconnect_timer_{nullptr};
    };
}
