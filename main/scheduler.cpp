#include "scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_sntp.h"
#include "events.hpp"
#include "filesystem.hpp"

#include "cJSON.h"

#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "sys/time.h"

using namespace std::chrono_literals;

namespace scheduler
{
    namespace
    {
        using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

        [[nodiscard]]
        std::expected<JsonPtr, esp_err_t>
        parse_json(std::string_view input)
        {
            if (input.empty() || input.find('\0') != std::string_view::npos)
            {
                return std::unexpected(ESP_ERR_INVALID_ARG);
            }

            const char *parse_end = nullptr;

            JsonPtr root{
                cJSON_ParseWithLengthOpts(
                    input.data(),
                    input.size(),
                    &parse_end,
                    false),
                &cJSON_Delete
            };

            if (!root)
            {
                return std::unexpected(ESP_ERR_INVALID_RESPONSE);
            }

            if (!cJSON_IsObject(root.get()))
            {
                return std::unexpected(ESP_ERR_INVALID_RESPONSE);
            }

            const char *const end = input.data() + input.size();

            if (parse_end == nullptr || parse_end > end)
            {
                return std::unexpected(ESP_ERR_INVALID_RESPONSE);
            }

            for (const char *cursor = parse_end; cursor < end; ++cursor)
            {
                if (std::isspace(static_cast<unsigned char>(*cursor)) == 0)
                {
                    return std::unexpected(ESP_ERR_INVALID_RESPONSE);
                }
            }

            return root;
        }

        [[nodiscard]]
        const cJSON *member(const cJSON *object, const char *key)
        {
            if (!cJSON_IsObject(object))
            {
                return nullptr;
            }

            return cJSON_GetObjectItemCaseSensitive(object, key);
        }

        [[nodiscard]]
        std::expected<std::string, esp_err_t>
        read_string(const cJSON *object, const char *key)
        {
            const cJSON *value = member(object, key);

            if (!cJSON_IsString(value) || value->valuestring == nullptr)
            {
                return std::unexpected(ESP_ERR_INVALID_RESPONSE);
            }

            return std::string{value->valuestring};
        }

        [[nodiscard]]
        std::expected<std::uint32_t, esp_err_t>
        read_uint32(const cJSON *object, const char *key)
        {
            const cJSON *value = member(object, key);

            if (!cJSON_IsNumber(value))
            {
                return std::unexpected(ESP_ERR_INVALID_RESPONSE);
            }

            const double number = value->valuedouble;

            if (!std::isfinite(number) ||
                number < 0.0 ||
                number > static_cast<double>(
                    std::numeric_limits<std::uint32_t>::max()) ||
                std::floor(number) != number)
            {
                return std::unexpected(ESP_ERR_INVALID_SIZE);
            }

            return static_cast<std::uint32_t>(number);
        }

        [[nodiscard]]
        std::expected<BellSchedule::TimeOfDay, esp_err_t>
        parse_time_of_day(std::string_view value)
        {
            if (value.size() != 5 ||
                value[2] != ':' ||
                value[0] < '0' || value[0] > '9' ||
                value[1] < '0' || value[1] > '9' ||
                value[3] < '0' || value[3] > '9' ||
                value[4] < '0' || value[4] > '9')
            {
                return std::unexpected(ESP_ERR_INVALID_ARG);
            }

            const auto hour =
                (value[0] - '0') * 10 + (value[1] - '0');

            const auto minute =
                (value[3] - '0') * 10 + (value[4] - '0');

            if (hour > 23 || minute > 59)
            {
                return std::unexpected(ESP_ERR_INVALID_ARG);
            }

            return std::chrono::minutes{hour * 60 + minute};
        }

        constexpr std::chrono::minutes minutes_per_day{24 * 60};

        bool is_valid_start_time(std::chrono::minutes value) noexcept
        {
            return value >= std::chrono::minutes{0}
                && value < minutes_per_day;
        }

        bool is_valid_end_time(std::chrono::minutes value) noexcept
        {
            // Allow 24:00 as end-of-day
            return value >= std::chrono::minutes{0}
                && value <= minutes_per_day;
        }

        [[nodiscard]]
        std::expected<BellSchedule::Period, esp_err_t>
        parse_period(const cJSON *json)
        {
            if (!cJSON_IsObject(json))
            {
                return std::unexpected(ESP_ERR_INVALID_RESPONSE);
            }

            const auto id = read_uint32(json, "id");
            if (!id)
            {
                return std::unexpected(id.error());
            }

            const auto start_string = read_string(json, "start");
            if (!start_string)
            {
                return std::unexpected(start_string.error());
            }

            const auto end_string = read_string(json, "end");
            if (!end_string)
            {
                return std::unexpected(end_string.error());
            }

            const auto start = parse_time_of_day(*start_string);
            if (!start)
            {
                return std::unexpected(start.error());
            }

            const auto end = parse_time_of_day(*end_string);
            if (!end)
            {
                return std::unexpected(end.error());
            }

            if (!is_valid_start_time(*start) || !is_valid_end_time(*end))
            {
                return std::unexpected(ESP_ERR_INVALID_ARG);
            }

            // We allow overnight periods.

            return BellSchedule::Period{
                .id = *id,
                .start = *start,
                .end = *end
            };
        }

        [[nodiscard]]
        const cJSON *find_schedule(
            const cJSON *schedules,
            std::string_view schedule_id)
        {
            if (!cJSON_IsObject(schedules))
            {
                return nullptr;
            }

            for (const cJSON *item = schedules->child;
                item != nullptr;
                item = item->next)
            {
                if (item->string != nullptr &&
                    std::string_view{item->string} == schedule_id)
                {
                    return item;
                }
            }

            return nullptr;
        }

        [[nodiscard]]
        std::expected<BellSchedule, esp_err_t>
        parse_schedule_object(
            const cJSON *json,
            std::string_view schedule_id)
        {
            if (!cJSON_IsObject(json))
            {
                return std::unexpected(ESP_ERR_INVALID_RESPONSE);
            }

            const cJSON *periods = member(json, "periods");
            if (!cJSON_IsArray(periods))
            {
                return std::unexpected(ESP_ERR_INVALID_RESPONSE);
            }

            const int period_count = cJSON_GetArraySize(periods);
            if (period_count < 0 ||
                static_cast<std::size_t>(period_count) >
                    BellSchedule::max_periods)
            {
                return std::unexpected(ESP_ERR_INVALID_SIZE);
            }

            BellSchedule schedule{
                .id = std::string{schedule_id},
            };

            const cJSON *description = member(json, "description");
            if (description != nullptr)
            {
                if (!cJSON_IsString(description) ||
                    description->valuestring == nullptr)
                {
                    return std::unexpected(ESP_ERR_INVALID_RESPONSE);
                }

                schedule.description = description->valuestring;
            }

            for (const cJSON *item = periods->child;
                item != nullptr;
                item = item->next)
            {
                const auto period = parse_period(item);
                if (!period)
                {
                    return std::unexpected(period.error());
                }

                schedule.periods[schedule.period_count] = *period;
                ++schedule.period_count;
            }

            return schedule;
        }

        constexpr char k_tag[] = "BellScheduler";
    } // namespace

    std::expected<BellRuntimeSettings, esp_err_t>
    BellScheduleParser::parse_settings(std::string_view json) noexcept
    {
        auto root_result = parse_json(json);
        if (!root_result)
        {
            return std::unexpected(root_result.error());
        }

        const cJSON *root = root_result->get();

        const auto timezone = read_string(root, "timezone");
        if (!timezone)
        {
            return std::unexpected(timezone.error());
        }

        const auto bell_duration_ms =
            read_uint32(root, "bell_duration_ms");
        if (!bell_duration_ms)
        {
            return std::unexpected(bell_duration_ms.error());
        }

        const auto warning_lead_time_ms =
            read_uint32(root, "warning_lead_time_ms");
        if (!warning_lead_time_ms)
        {
            return std::unexpected(warning_lead_time_ms.error());
        }

        const auto active_schedule_id =
            read_string(root, "active_schedule_id");
        if (!active_schedule_id)
        {
            return std::unexpected(active_schedule_id.error());
        }

        if (active_schedule_id->empty())
        {
            return std::unexpected(ESP_ERR_INVALID_ARG);
        }

        const cJSON *schedules = member(root, "schedules");
        if (!cJSON_IsObject(schedules))
        {
            return std::unexpected(ESP_ERR_INVALID_RESPONSE);
        }

        const auto bell_duration =
            std::chrono::milliseconds{*bell_duration_ms};
        const auto warning_lead =
            std::chrono::milliseconds{*warning_lead_time_ms};

        return BellRuntimeSettings{
            .timezone = *timezone,
            .bell_duration = bell_duration,
            .warning_lead_time = warning_lead,
            .active_schedule_id = *active_schedule_id
        };
    }

    std::expected<BellSchedule, esp_err_t>
    BellScheduleParser::parse_schedule(
        std::string_view json,
        std::string_view schedule_id) noexcept
    {
        if (schedule_id.empty() ||
            schedule_id.find('\0') != std::string_view::npos)
        {
            return std::unexpected(ESP_ERR_INVALID_ARG);
        }

        auto root_result = parse_json(json);
        if (!root_result)
        {
            return std::unexpected(root_result.error());
        }

        const cJSON *root = root_result->get();
        const cJSON *schedules = member(root, "schedules");
        if (!cJSON_IsObject(schedules))
        {
            return std::unexpected(ESP_ERR_INVALID_RESPONSE);
        }

        const cJSON *schedule_json =
            find_schedule(schedules, schedule_id);
        if (schedule_json == nullptr)
        {
            return std::unexpected(ESP_ERR_NOT_FOUND);
        }

        return parse_schedule_object(schedule_json, schedule_id);
    }

    SntpClient::Config SntpClient::Config::single(std::string_view server, bool start_immediately) noexcept
    {
        SntpClient::Config c;
        c.servers[0] = server.data();
        c.num_servers = 1;
        c.start = start_immediately;
        return c;
    }

    SntpClient::Config SntpClient::Config::from_dhcp(bool start_immediately) noexcept
    {
        SntpClient::Config c;
        c.server_from_dhcp = true;
        c.start = start_immediately;
        return c;
    }

    SntpClient::Config& SntpClient::Config::with_smooth_sync(bool v) noexcept
    {
        smooth_sync = v;
        return *this;
    }

    SntpClient::Config& SntpClient::Config::with_wait_for_sync(bool v) noexcept
    {
        wait_for_sync = v;
        return *this;
    }

    SntpClient::Config& SntpClient::Config::with_start(bool v) noexcept
    {
        start = v;
        return *this;
    }

    SntpClient::Config& SntpClient::Config::renew_on(ip_event_t evt, std::size_t first_idx) noexcept
    {
        renew_servers_after_new_ip = true;
        ip_event_to_renew = evt;
        index_of_first_server = first_idx;
        return *this;
    }

    esp_sntp_config_t SntpClient::Config::to_c() const noexcept
    {
        esp_sntp_config_t c{};
        c.smooth_sync = smooth_sync;
        c.server_from_dhcp = server_from_dhcp;
        c.wait_for_sync = wait_for_sync;
        c.start = start;
        c.sync_cb = nullptr;          // we use the event bridge
        c.renew_servers_after_new_IP = renew_servers_after_new_ip;
        c.ip_event_to_renew = ip_event_to_renew;
        c.index_of_first_server = index_of_first_server;
        c.num_of_servers = num_servers;

        for (std::size_t i = 0; i < num_servers; ++i)
            c.servers[i] = servers[i];

        return c;
    }

    SntpClient::SntpClient(event::EventDispatcher& dispatcher) noexcept
        : dispatcher_(dispatcher)
    {
    }

    SntpClient::~SntpClient()
    {
        deinit();
    }

    esp_err_t SntpClient::init(const SntpClient::Config& cfg) noexcept
    {
        if (initialised_)
            return ESP_ERR_INVALID_STATE;

        const auto c   = cfg.to_c();
        const auto err = esp_netif_sntp_init(&c);
        if (err != ESP_OK)
            return err;

        const esp_err_t reg = esp_event_handler_instance_register(
            NETIF_SNTP_EVENT,
            NETIF_SNTP_TIME_SYNC,
            &SntpClient::sntp_event_handler,
            this,
            &handler_instance_);

        if (reg != ESP_OK)
        {
            esp_netif_sntp_deinit();
            return reg;
        }

        initialised_ = true;
        started_     = cfg.start;
        return ESP_OK;
    }

    esp_err_t SntpClient::start() noexcept
    {
        if (!initialised_)
            return ESP_ERR_INVALID_STATE;

        const auto err = esp_netif_sntp_start();
        if (err == ESP_OK)
            started_ = true;
        return err;
    }

    void SntpClient::deinit() noexcept
    {
        if (!initialised_)
            return;

        if (handler_instance_)
        {
            esp_event_handler_instance_unregister(
                NETIF_SNTP_EVENT,
                NETIF_SNTP_TIME_SYNC,
                handler_instance_);
            handler_instance_ = nullptr;
        }

        esp_netif_sntp_deinit();
        initialised_ = false;
        started_     = false;
    }

    esp_err_t SntpClient::wait_for_sync(std::chrono::milliseconds timeout) noexcept
    {
        if (!initialised_)
            return ESP_ERR_INVALID_STATE;

        const TickType_t ticks =
            pdMS_TO_TICKS(static_cast<uint32_t>(timeout.count()));

        return esp_netif_sntp_sync_wait(ticks);
    }

    esp_err_t SntpClient::wait_for_sync() noexcept
    {
        return wait_for_sync(std::chrono::milliseconds{portMAX_DELAY});
    }

    std::expected<unsigned, esp_err_t>
    SntpClient::reachability(unsigned index) const noexcept
    {
        if (!initialised_)
            return std::unexpected{ESP_ERR_INVALID_STATE};

        unsigned value = 0;
        const auto err = esp_netif_sntp_reachability(index, &value);
        if (err != ESP_OK)
            return std::unexpected{err};

        return value;
    }

    void SntpClient::sntp_event_handler(void* arg,
        esp_event_base_t /*base*/,
        std::int32_t /*id*/,
        void* event_data)
    {
        auto* self = static_cast<SntpClient*>(arg);
        if (!self)
            return;

        event::SntpTimeSynced payload{};

        if (event_data)
        {
            const auto* sync =
                static_cast<const esp_netif_sntp_time_sync_t*>(event_data);

            payload.tv_sec  = sync->tv.tv_sec;
            payload.tv_usec = static_cast<std::int32_t>(sync->tv.tv_usec);

            // Explicitly apply the time.  Some ESP-IDF / config combinations
            // post the event without updating the system clock.
            settimeofday(&sync->tv, nullptr);
        }

        self->dispatcher_.publish(event::Source::Network, std::move(payload));
    }

    BellScheduler::BellScheduler(storage::FilesystemService& fs,
        event::EventDispatcher& dispatcher,
        Config cfg) noexcept
        : fs_(fs),
        dispatcher_(dispatcher),
        cfg_(std::move(cfg))
    {
        wake_sem_ = xSemaphoreCreateBinary();
        if (!wake_sem_)
        {
            ESP_LOGE(k_tag, "Failed to create wake semaphore");
        }
    }

    BellScheduler::~BellScheduler()
    {
        stop();
        if (wake_sem_)
        {
            vSemaphoreDelete(wake_sem_);
            wake_sem_ = nullptr;
        }
    }

    esp_err_t BellScheduler::start() noexcept
    {
        if (running_.load(std::memory_order_acquire))
        {
            return ESP_OK;
        }

        if (!wake_sem_)
        {
            return ESP_ERR_NO_MEM;
        }

        recalculate_requested_.store(true, std::memory_order_release);

        // Must be set before xTaskCreate().
        running_.store(true, std::memory_order_release);

        const BaseType_t rc = xTaskCreate(worker_entry,
            "bell_sched",
            cfg_.task_stack_size,
            this,
            cfg_.task_priority,
            &task_);

        if (rc != pdPASS)
        {
            running_.store(false, std::memory_order_release);
            task_ = nullptr;

            ESP_LOGE(k_tag, "Failed to create worker task");
            return ESP_ERR_NO_MEM;
        }

        xSemaphoreGive(wake_sem_);

        ESP_LOGI(k_tag, "Scheduler started");

        return ESP_OK;
    }

    void BellScheduler::stop() noexcept
    {
        if (!running_.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        if (wake_sem_)
        {
            xSemaphoreGive(wake_sem_);
        }

        if (task_)
        {
            for (int i = 0; i < 50 && eTaskGetState(task_) != eDeleted; ++i)
            {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            task_ = nullptr;
        }
    }

    esp_err_t BellScheduler::recalculate() noexcept
    {
        if (!running_.load(std::memory_order_acquire))
        {
            return ESP_ERR_INVALID_STATE;
        }
        recalculate_requested_.store(true, std::memory_order_release);
        xSemaphoreGive(wake_sem_);
        return ESP_OK;
    }

    void BellScheduler::worker_entry(void* arg)
    {
        static_cast<BellScheduler*>(arg)->worker_loop();
        vTaskDelete(nullptr);
    }

    void BellScheduler::worker_loop()
    {
        ESP_LOGI(k_tag, "Worker started");

        while (running_.load(std::memory_order_acquire))
        {
            if (recalculate_requested_.exchange(false, std::memory_order_acq_rel))
            {
                ESP_LOGI(k_tag, "Executing schedule calculation");

                if (const esp_err_t err = load_and_parse(); err != ESP_OK)
                {
                    ESP_LOGE(k_tag, "Recalculate failed: %s", esp_err_to_name(err));
                    continue;
                }
            }

            // Dispatch all events whose scheduled time has arrived.
            const TimePoint now = Clock::now();

            while (next_index_ < plan_count_ && plan_[next_index_].when <= now)
            {
                dispatch(plan_[next_index_]);
                ++next_index_;
            }

            TickType_t timeout = portMAX_DELAY;

            if (next_index_ < plan_count_)
            {
                const auto remaining = plan_[next_index_].when - Clock::now();

                if (remaining <= std::chrono::seconds{0})
                {
                    timeout = 0;
                }
                else
                {
                    // Convert to FreeRTOS ticks, rounding up so we never wake early.
                    const auto remaining_us =
                        std::chrono::duration_cast<std::chrono::microseconds>(remaining).count();

                    const std::uint64_t ticks =
                        (static_cast<std::uint64_t>(remaining_us) * configTICK_RATE_HZ +
                        999'999ULL) /
                        1'000'000ULL;

                    timeout = static_cast<TickType_t>(
                        std::clamp<std::uint64_t>(ticks, 1, portMAX_DELAY - 1));
                }
            }

            xSemaphoreTake(wake_sem_, timeout);
        }

        ESP_LOGI(k_tag, "Worker stopped");
    }

    esp_err_t BellScheduler::load_and_parse() noexcept
    {
        auto text = fs_.read_text(schedules_path);
        if (!text)
        {
            ESP_LOGE(k_tag, "Cannot read %s: %s",
                schedules_path.data(),
                esp_err_to_name(text.error()));
            return text.error();
        }

        auto settings = BellScheduleParser::parse_settings(*text);
        if (!settings)
        {
            ESP_LOGE(k_tag, "parse_settings failed: %s",
                esp_err_to_name(settings.error()));
            return settings.error();
        }

        auto schedule = BellScheduleParser::parse_schedule(
            *text, settings->active_schedule_id);
        if (!schedule)
        {
            ESP_LOGE(k_tag, "parse_schedule(\"%s\") failed: %s",
                settings->active_schedule_id.c_str(),
                esp_err_to_name(schedule.error()));
            return schedule.error();
        }

        settings_ = std::move(*settings);
        schedule_ = std::move(*schedule);

        apply_timezone(settings_.timezone);

        const TimePoint now = Clock::now();
        auto events = build_events(schedule_, settings_, now);

        // Sort and coalesce identical timestamps
        std::ranges::sort(events, {}, &PlannedEvent::when);

        plan_count_ = 0;
        next_index_ = 0;

        for (const auto& e : events)
        {
            if (plan_count_ > 0 && plan_[plan_count_ - 1].when == e.when)
            {
                continue; // already have a BellDue at this exact second
            }

            if (plan_count_ >= max_events)
            {
                ESP_LOGW(k_tag, "Event plan truncated at %zu entries", max_events);
                break;
            }

            plan_[plan_count_++] = e;
        }

        dispatcher_.publish(event::Source::Scheduler, event::ScheduleChanged{});

        ESP_LOGI(k_tag,
            "Loaded schedule \"%s\" (%zu periods) → %zu unique BellDue events",
            schedule_.id.c_str(),
            schedule_.period_count,
            plan_count_);

        {
            const auto now_sec = Clock::to_time_t(now);
            ESP_LOGI(k_tag, "now=%lld  plan_count=%zu",
                    static_cast<long long>(now_sec), plan_count_);

            for (std::size_t i = 0; i < plan_count_; ++i)
            {
                const auto when_sec = Clock::to_time_t(plan_[i].when);
                const auto delta = plan_[i].when - now;

                ESP_LOGI(k_tag, "  [%zu] when=%lld  period=%u  (in %lld s)",
                    i,
                    static_cast<long long>(when_sec),
                    plan_[i].period_id,
                    static_cast<long long>(
                        std::chrono::duration_cast<std::chrono::seconds>(delta).count()));
            }
        }

        return ESP_OK;
    }

    void BellScheduler::apply_timezone(std::string_view tz) noexcept
    {
        if (tz.find('/') != std::string_view::npos)
        {
            ESP_LOGW(k_tag,
                "Timezone \"%.*s\" looks like an IANA name; "
                "ESP-IDF prefers POSIX form (e.g. GMT0BST,M3.5.0/1,M10.5.0)",
                static_cast<int>(tz.size()), tz.data());
        }

        setenv("TZ", std::string(tz).c_str(), 1);
        tzset();
        ESP_LOGI(k_tag, "Timezone set to \"%.*s\"",
            static_cast<int>(tz.size()), tz.data());
    }

    // ---------------------------------------------------------------------------
    // Core planning algorithm
    // ---------------------------------------------------------------------------
    std::vector<BellScheduler::PlannedEvent>
    BellScheduler::build_events(const BellSchedule& schedule,
        const BellRuntimeSettings& settings,
        TimePoint now) const noexcept
    {
        std::vector<PlannedEvent> result;
        result.reserve(schedule.period_count * 3 * 2); // worst-case today+tomorrow

        const std::time_t now_tt = Clock::to_time_t(now);
        std::tm local{};
        localtime_r(&now_tt, &local);

        const auto lead_ms = settings.warning_lead_time;
        const bool has_warning = lead_ms.count() > 0;
        const auto lead_sec = std::chrono::duration_cast<std::chrono::seconds>(lead_ms);

        // Generate for “today” and “tomorrow” relative to the local calendar day.
        // This covers any overnight period and the configured planning horizon.
        for (int day_offset = 0; day_offset <= 1; ++day_offset)
        {
            for (std::size_t i = 0; i < schedule.period_count; ++i)
            {
                const auto& p = schedule.periods[i];
                const auto start_tod = clamp_tod(p.start);
                const auto end_tod = clamp_tod(p.end);

                // Start
                {
                    const TimePoint t =
                        local_minutes_to_time_point(start_tod, local, day_offset);
                    if (t > now)
                    {
                        result.push_back({t, p.id});
                    }
                }

                // Finish
                {
                    const int finish_day =
                        day_offset + (end_tod < start_tod ? 1 : 0);
                    const TimePoint t =
                        local_minutes_to_time_point(end_tod, local, finish_day);
                    if (t > now)
                    {
                        result.push_back({t, p.id});
                    }
                }

                // Warning
                if (has_warning)
                {
                    // Always compute from the start of this day_offset instance
                    const TimePoint start_t =
                        local_minutes_to_time_point(start_tod, local, day_offset);

                    // May cross midnight; that is intentional and correct
                    const TimePoint warn_t = start_t - lead_sec;

                    if (warn_t > now)
                    {
                        result.push_back({warn_t, p.id});
                    }
                }
            }
        }

        // Discard anything beyond the configured horizon
        const TimePoint horizon = now + cfg_.planning_horizon;

        std::erase_if(result,
                    [horizon](const PlannedEvent& e) { return e.when > horizon; });

        return result;
    }

    BellScheduler::TimePoint
    BellScheduler::local_minutes_to_time_point(std::chrono::minutes tod,
        const std::tm& base_local,
        int day_offset) noexcept
    {
        std::tm tm = base_local;

        // Advance by local calendar days, not by 86400 seconds.
        tm.tm_mday += day_offset;

        tm.tm_hour = static_cast<int>(tod.count() / 60);
        tm.tm_min = static_cast<int>(tod.count() % 60);
        tm.tm_sec = 0;
        tm.tm_isdst = -1;

        const std::time_t t = std::mktime(&tm);
        return Clock::from_time_t(t);
    }

    std::chrono::minutes BellScheduler::clamp_tod(std::chrono::minutes m) noexcept
    {
        constexpr auto day = 24h;
        m %= day;
        if (m.count() < 0)
        {
            m += day;
        }
        return m;
    }

    void BellScheduler::dispatch(const PlannedEvent& ev) noexcept
    {
        const TimePoint actual = Clock::now();

        const event::BellDue data{
            .duration_ms =
                static_cast<std::uint32_t>(settings_.bell_duration.count())
        };

        const bool ok = dispatcher_.publish(event::Source::Scheduler, data);

        const auto lateness =
            std::chrono::duration_cast<std::chrono::microseconds>(actual - ev.when);

        if (!ok)
        {
            ESP_LOGE(k_tag, "Failed to publish BellDue (period %u)",
                ev.period_id);
        }
        else
        {
            ESP_LOGI(k_tag,
                "BellDue period=%u scheduled=%lld actual=%lld lateness=%lld us",
                ev.period_id,
                static_cast<long long>(Clock::to_time_t(ev.when)),
                static_cast<long long>(Clock::to_time_t(actual)),
                static_cast<long long>(lateness.count()));
        }
    }
} // namespace scheduler