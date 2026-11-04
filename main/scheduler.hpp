#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "events.hpp"
#include "filesystem.hpp"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif_sntp.h"

namespace storage
{
class FilesystemService;
} // namespace storage

namespace scheduler
{
    struct BellSchedule
    {
        using TimeOfDay = std::chrono::minutes;

        struct Period
        {
            std::uint32_t id{};
            TimeOfDay start{};
            TimeOfDay end{};
        };

        static constexpr std::size_t max_periods{20};
        std::string id;
        std::string description{};

        std::array<Period, max_periods> periods{};
        std::size_t period_count{0};
    };

    struct BellRuntimeSettings
    {
        std::string timezone{"Europe/London"};
        std::chrono::milliseconds bell_duration{};
        std::chrono::milliseconds warning_lead_time{};
        std::string active_schedule_id{};
    };

    // Helper class with no state
    struct BellScheduleParser final
    {
        BellScheduleParser() = delete;

        [[nodiscard]]
        static std::expected<BellRuntimeSettings, esp_err_t>
        parse_settings(std::string_view json) noexcept;

        [[nodiscard]]
        static std::expected<BellSchedule, esp_err_t>
        parse_schedule(std::string_view json, std::string_view schedule) noexcept;
    };

// ---------------------------------------------------------------------------
// SntpClient
// ---------------------------------------------------------------------------
    class SntpClient
    {
    public:
        struct Config
        {
            bool smooth_sync = true;
            bool server_from_dhcp = true;
            bool wait_for_sync = true;
            bool start = false;
            bool renew_servers_after_new_ip = false;
            ip_event_t  ip_event_to_renew = IP_EVENT_STA_GOT_IP;
            std::size_t index_of_first_server = 0;

            std::array<const char*, CONFIG_LWIP_SNTP_MAX_SERVERS> servers{"pool.ntp.org"};
            std::size_t num_servers = 1;

            // Factories
            [[nodiscard]] static Config single(std::string_view server,
                bool start_immediately = true) noexcept;

            [[nodiscard]] static Config from_dhcp(bool start_immediately = false) noexcept;

            // Fluent setters
            Config& with_smooth_sync(bool v = true) noexcept;
            Config& with_wait_for_sync(bool v = true) noexcept;
            Config& with_start(bool v = true) noexcept;
            Config& renew_on(ip_event_t evt, std::size_t first_idx = 0) noexcept;

            template <std::convertible_to<std::string_view>... Servers>
                requires (sizeof...(Servers) <= CONFIG_LWIP_SNTP_MAX_SERVERS)
            Config& with_servers(Servers&&... svs) noexcept
            {
                num_servers = 0;
                ((servers[num_servers++] = std::string_view{svs}.data()), ...);
                return *this;
            }

            [[nodiscard]] esp_sntp_config_t to_c() const noexcept;
        };


        explicit SntpClient(event::EventDispatcher& dispatcher) noexcept;

        SntpClient(const SntpClient&) = delete;
        SntpClient& operator=(const SntpClient&) = delete;

        ~SntpClient();

        // Initialise (and optionally start). Returns ESP_OK on success.
        [[nodiscard]] esp_err_t init(const Config& cfg) noexcept;

        // Explicit start / restart
        [[nodiscard]] esp_err_t start() noexcept;

        void deinit() noexcept;

        // Blocking wait for the first successful synchronisation
        [[nodiscard]] esp_err_t
        wait_for_sync(std::chrono::milliseconds timeout) noexcept;

        // Convenience: wait forever
        [[nodiscard]] esp_err_t wait_for_sync() noexcept;

        // RFC 5905 reachability shift register
        [[nodiscard]] std::expected<unsigned, esp_err_t>
        reachability(unsigned index) const noexcept;

        [[nodiscard]] bool is_initialised() const noexcept { return initialised_; }
        [[nodiscard]] bool is_started() const noexcept { return started_; }

    private:
        static void sntp_event_handler(void* arg,
            esp_event_base_t base,
            std::int32_t id,
            void* event_data);

        event::EventDispatcher& dispatcher_;
        esp_event_handler_instance_t handler_instance_{nullptr};
        bool initialised_{false};
        bool started_{false};
    };

    // ---------------------------------------------------------------------------
    // BellScheduler
    // ---------------------------------------------------------------------------
    class BellScheduler final
    {
    public:
        static constexpr std::size_t max_events = 128;
        static constexpr std::string_view schedules_path = "schedules.json";

        struct Config
        {
            std::chrono::hours planning_horizon = std::chrono::hours(36);
            std::uint32_t task_stack_size{4096};
            UBaseType_t task_priority{5};
        };

        explicit BellScheduler(storage::FilesystemService& fs,
                            event::EventDispatcher& dispatcher,
                            Config cfg) noexcept;

        BellScheduler(const BellScheduler&) = delete;
        BellScheduler& operator=(const BellScheduler&) = delete;

        ~BellScheduler();

        // Rebuild the event plan from schedules.json (non-blocking).
        [[nodiscard]] esp_err_t recalculate() noexcept;

        // Start the background worker. Call after SNTP has synchronised.
        [[nodiscard]] esp_err_t start() noexcept;

        void stop() noexcept;

        [[nodiscard]] bool is_running() const noexcept
        {
            return running_.load(std::memory_order_acquire);
        }

    private:
        // Internal representation of a future ringing instant
        struct PlannedEvent
        {
            std::time_t when{};       // absolute UTC seconds
            std::uint32_t period_id{};  // for logging / diagnostics only
        };

        // FreeRTOS worker
        static void worker_entry(void* arg);
        void worker_loop();

        // Core algorithm
        [[nodiscard]] esp_err_t load_and_parse() noexcept;
        void apply_timezone(std::string_view tz) noexcept;

        [[nodiscard]] std::vector<PlannedEvent>
        build_events(const BellSchedule& schedule,
            const BellRuntimeSettings& settings,
            std::time_t now) const noexcept;

        void dispatch(const PlannedEvent& ev) noexcept;

        // Helpers
        static std::time_t local_minutes_to_time_t(std::chrono::minutes tod,
            const std::tm& base_local,
            int day_offset) noexcept;

        static std::chrono::minutes clamp_tod(std::chrono::minutes m) noexcept;

        // Members
        storage::FilesystemService& fs_;
        event::EventDispatcher& dispatcher_;
        Config cfg_;

        std::atomic<bool> running_{false};
        std::atomic<bool> recalculate_requested_{false};

        TaskHandle_t task_{nullptr};
        SemaphoreHandle_t wake_sem_{nullptr};

        std::array<PlannedEvent, max_events> plan_{};
        std::size_t plan_count_{0};
        std::size_t next_index_{0};

        BellRuntimeSettings settings_{};
        BellSchedule schedule_{};
    };
} // namespace scheduler