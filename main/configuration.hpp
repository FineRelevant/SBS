#pragma once

#include <array>
#include <cstdint>

#include "events.hpp"
#include "filesystem.hpp"
#include "wifi.hpp"
#include "internet_config.hpp"
#include "http_server.hpp"
#include "http_client.hpp"
#include "filesystem.hpp"
#include "mdns.hpp"
#include "scheduler.hpp"


#include "esp_err.h"

namespace internet
{
    enum class ConnectivityStatus : uint8_t
    {
        Unknown = 0,
        Connected,
        NoConnectivity,
    };

    class ConnectivityChecker final
    {
    public:
        struct ConnectivityProbe
        {
            std::string_view url;
            int expected_status{};
            std::string_view expected_body;
        };

        static constexpr std::size_t k_max_probe_body_size = 1024;

        static constexpr std::array k_connectivity_probes{
        ConnectivityProbe{
            .url = "https://connectivitycheck.gstatic.com/generate_204",
            .expected_status = 204,
            .expected_body = {},
        },
        ConnectivityProbe{
            .url = "https://www.gstatic.com/generate_204",
            .expected_status = 204,
            .expected_body = {},
        },
        ConnectivityProbe{
            .url = "http://cp.cloudflare.com/generate_204",
            .expected_status = 204,
            .expected_body = {},
        },
        ConnectivityProbe{
            .url = "http://edge-http.microsoft.com/captiveportal/generate_204",
            .expected_status = 204,
            .expected_body = {},
        },
        ConnectivityProbe{
            .url = "http://detectportal.firefox.com/success.txt",
            .expected_status = 200,
            .expected_body = "success",
        },
        ConnectivityProbe{
            .url = "http://captive.apple.com/hotspot-detect.html",
            .expected_status = 200,
            .expected_body = "Success",
        },
    };

    explicit ConnectivityChecker(int timeout_ms);
    ~ConnectivityChecker() = default;

    ConnectivityChecker(ConnectivityChecker&) = delete;
    ConnectivityChecker& operator=(ConnectivityChecker&) = delete;

    ConnectivityChecker(ConnectivityChecker&&) = delete;
    ConnectivityChecker& operator=(ConnectivityChecker&&) = delete;

    ConnectivityStatus check_connectivity();

    private:
        HttpClient client_;
        esp_err_t init_error_{ESP_OK};
    };

    class ConfigurationService final
    {
    public:
        enum class OperationMode : std::uint8_t
        {
            Normal = 0,
            Recovery,
        };

        explicit ConfigurationService(
            event::EventDispatcher& event_dispatcher,
            WifiService& wifi_service,
            storage::FilesystemService& filesystem_service,
            scheduler::SntpClient& sntp_client,
            scheduler::BellScheduler& scheduler
        );

        [[nodiscard]] esp_err_t start();

    private:
        event::EventDispatcher& event_dispatcher_;
        WifiService& wifi_service_;
        storage::FilesystemService& filesystem_service_;
        scheduler::SntpClient& sntp_client_;
        scheduler::BellScheduler& scheduler_;
        HttpServer server_{};
        Mdns mdns_;

        InternetConfig config_;

        OperationMode mode_{OperationMode::Normal};
        ConnectivityStatus connectivity_status_{ConnectivityStatus::Unknown};
        event::ApplicationState application_state_{event::ApplicationState::Running};

        esp_err_t stop();

        void start_configuration();
        void start_synchronization();

        esp_err_t start_mdns();
        esp_err_t stop_mdns();

        esp_err_t start_website();
        esp_err_t stop_website();
        esp_err_t register_website_handlers();
        esp_err_t unregister_website_handlers();

        void start_recovery();

        void register_event_handlers();
        static void on_application_state_changed(const event::Event& event, const event::ApplicationStateChanged& data, void* context);
        static void on_station_got_ip(const event::Event& event, const event::WifiStationGotIp& data, void* context);
        static void on_ap_started(const event::Event& event, const event::WifiApStarted& data, void* context);
        static void on_wifi_reconnect_threshold_reached(const event::Event& event, const event::WifiReconnectThresholdReached& data, void* context);
        static void on_sync(const event::Event& event, const event::SntpTimeSynced& data, void* context);

        void publish_wifi_configuration_started();
        void publish_wifi_configuration_stopped();
        void publish_wifi_configuration_completed();
        void publish_wifi_configuration_failed();
    };
} // namespace internet