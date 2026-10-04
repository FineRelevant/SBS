#include "scheduler.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstring>
#include <cstdio>

#include "esp_log.h"
#include "esp_sntp.h"

#include <sys/time.h>

namespace scheduler
{
	namespace
	{
		constexpr uint32_t ms_per_second = 1000;
		constexpr uint32_t ms_per_minute = 60 * ms_per_second;
		constexpr uint32_t ms_per_hour = 60 * ms_per_minute;
		
		TimeOfDay parse_time_of_day(const cJSON* json)
		{
			if (!cJSON_IsString(json) || json->valuestring == nullptr)
				return {};
			

			unsigned hour = 0;
			unsigned minute = 0;

			char extra = '\0';

			const int parsed = std::sscanf(
				json->valuestring,
				"%u:%u%c",
				&hour,
				&minute,
				&extra);

			if (parsed != 2)
				return {};

			if (hour > 23 || minute > 59)
				return {};

			return TimeOfDay{static_cast<uint8_t>(hour),
							 static_cast<uint8_t>(minute)};
		}
	}
	
	bool TimeService::synchronize()
	{
		if (esp_sntp_enabled())
		{
			return false;
		}
		
		esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
		esp_sntp_setservername(0, "pool.ntp.org");
		
		esp_sntp_init();
		
		return true;
	}
	
	bool TimeService::is_synchronized() const
	{
		return esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
	}
	
	bool TimeService::set_timezone(const char* timezone)
	{
		if (timezone == nullptr)
			return false;
		
		setenv("TZ", timezone, 1);
		tzset();
		
		return true;
	}
	
	std::time_t TimeService::absolute_time() const
	{
		return std::time(nullptr);
	}
	
	std::tm TimeService::local_time() const
	{
		const std::time_t current = absolute_time();
		
		std::tm result {};
		
		localtime_r(&current, &result);
		
		return result;
	}
	
	SchedulerConfig SchedulerConfigParser::parse_from_json(const char* json)
	{
		if (json == nullptr)
			return {};
		
		cJSON* root = cJSON_Parse(json);
		
		if (root == nullptr)
			return {};
		
		SchedulerConfig config {};

		do
		{	
			const cJSON* timezone =
				cJSON_GetObjectItemCaseSensitive(root, "timezone");

			const cJSON* duration =
				cJSON_GetObjectItemCaseSensitive(root, "duration_seconds");

			const cJSON* warning =
				cJSON_GetObjectItemCaseSensitive(root, "warning_seconds");

			const cJSON* active =
				cJSON_GetObjectItemCaseSensitive(root, "active_schedule");

			const cJSON* schedules =
				cJSON_GetObjectItemCaseSensitive(root, "schedules");

			if (!cJSON_IsString(timezone) ||
				!cJSON_IsNumber(duration) ||
				!cJSON_IsNumber(warning) ||
				!cJSON_IsString(active) ||
				!cJSON_IsObject(schedules))
			{
				break;
			}

			config.timezone = timezone->valuestring;

			config.duration_ms = static_cast<uint32_t>(duration->valuedouble * 1000.0);

			config.warning_ms = static_cast<uint32_t>(warning->valuedouble * 1000.0);

			config.active_schedule = active->valuestring;

			config.schedule_count = 0;

			const cJSON* schedule_json = nullptr;

			cJSON_ArrayForEach(schedule_json, schedules)
			{
				if (config.schedule_count >= SchedulerConfig::max_schedules)
					break;

				if (!cJSON_IsObject(schedule_json))
					break;

				Schedule& schedule = config.schedules[config.schedule_count];

				schedule.name = schedule_json->string;

				const cJSON* name =
					cJSON_GetObjectItemCaseSensitive(
						schedule_json,
					"name");

				const cJSON* periods =
					cJSON_GetObjectItemCaseSensitive(
						schedule_json,
					"periods");

				if (!cJSON_IsString(name) ||
					!cJSON_IsArray(periods))
				{
					break;
				}

				schedule.name = name->valuestring;
				schedule.period_count = 0;

				const cJSON* period_json = nullptr;

				cJSON_ArrayForEach(period_json, periods)
				{
					if (schedule.period_count >= Schedule::max_periods)
						break;

					if (!cJSON_IsObject(period_json))
						break;

					Period& period =
						schedule.periods[schedule.period_count];

					const cJSON* id =
						cJSON_GetObjectItemCaseSensitive(
							period_json,
						"id");

					const cJSON* start =
						cJSON_GetObjectItemCaseSensitive(
							period_json,
						"start");

					const cJSON* end =
						cJSON_GetObjectItemCaseSensitive(
							period_json,
						"end");

					if (!cJSON_IsNumber(id) ||
						!cJSON_IsString(start) ||
						!cJSON_IsString(end))
					{
						break;
					}
					
					period.start = parse_time_of_day(start);
					period.end = parse_time_of_day(end);

					period.id = static_cast<uint32_t>(id->valuedouble);

					++schedule.period_count;
				}

				++config.schedule_count;
			}

		} while (false);

		cJSON_Delete(root);
		
		return config;
	}
	
	const Schedule* SchedulerConfig::get_active_schedule() const
	{
		for (std::size_t i = 0; i < schedule_count; i++)
		{
			if (schedules.name == active_schedule)
				return &schedules[i]i;
		}
		
		return nullptr;
	}
	
	bool SchedulerTask::start()
	{
		if (_handle == nullptr)
			return false;
		
		_handle = xTaskCreateStatic(
		&SchedulerTask::task_entry,
			"scheduler",
			stack_words,
			this,
			_priority,
			_stack.data(),
			&_control_block);
		
		return _handle != nullptr;
	}
	
	void SchedulerTask::stop()
	{
		if (_handle == nullptr)
			return;
		
		vTaskDelete(_handle);
		_handle = nullptr;
	}
	
	void SchedulerTask::task_entry(void* argument)
	{
		auto* task = static_cast<SchedulerTask*>(argument);

		task->run();

		// run() is expected to never return.
		vTaskDelete(nullptr);
	}
	
	void SchedulerTask::schedule_event(std::time_t timestamp, event::Data event_data)
	{
		if (_scheduled_events_count >= max_scheduled_events)
			return;

		std::size_t position = _scheduled_events_count;

		while (position > 0 &&
		       _scheduled_events[position - 1].timestamp > timestamp)
		{
			_scheduled_events[position] =
			    std::move(_scheduled_events[position - 1]);

			--position;
		}

		_scheduled_events[position] = ScheduledEvent {
			.timestamp = timestamp,
			.event_data = std::move(event_data)
		};

		++_scheduled_events_count;
	}
	
	const SchedulerTask::ScheduledEvent* SchedulerTask::next_event()
	{
		if (_scheduled_events_count == 0)
			return nullptr;

		return &_scheduled_events[0];
	}
	
	void SchedulerTask::remove_next_event()
	{
		if (_scheduled_events_count == 0)
			return;

		for (std::size_t i = 1; i < _scheduled_events_count; ++i)
			_scheduled_events[i - 1] =
			    std::move(_scheduled_events[i]);

		--_scheduled_events_count;
	}
	
	void SchedulerTask::rebuild_schedule()
	{
		_scheduled_events_count = 0;

		const std::tm local = _time_service.local_time();

		const Schedule* schedule = _config.get_active_schedule();

		if (schedule == nullptr)
			return;

		for (std::size_t i = 0; i < schedule->period_count; ++i)
		{
			const Period& period = schedule->periods[i];

			std::tm start = local;
			start.tm_hour = period.start.hour;
			start.tm_min  = period.start.minute;
			start.tm_sec  = 0;

			const std::time_t start_time = std::mktime(&start);

			if (start_time == static_cast<std::time_t>(-1))
				continue;

			schedule_event(
			    start_time,
				event::BellEvent {
					.duration_ms = _config.duration_ms
				});
		}
	}
	
	bool SchedulerTask::wait_until(std::time_t timestamp)
	{
		while (true)
		{
			const std::time_t now = _time_service.absolute_time();

			if (now >= timestamp)
				return true;

			const std::time_t seconds = timestamp - now;

			TickType_t ticks;

			if (seconds >=
			    static_cast<std::time_t>(portMAX_DELAY / configTICK_RATE_HZ))
			{
				ticks = portMAX_DELAY;
			}
			else
			{
				ticks = pdMS_TO_TICKS(
				    static_cast<uint32_t>(seconds * 1000)
				);

				if (ticks == 0)
					ticks = 1;
			}

			const uint32_t notification =
			    ulTaskNotifyTake(pdTRUE, ticks);

			if (notification != 0)
			{
				/*
				 * Something explicitly woke us.
				 *
				 * Recalculate the schedule/time rather than assuming
				 * the original deadline is still valid.
				 */
				return false;
			}
		}
	}
	
	void SchedulerTask::notify()
	{
		if (_handle != nullptr)
			xTaskNotifyGive(_handle);
	}
	
	void SchedulerTask::run()
	{
		while (true)
		{
			if (!_time_service.is_synchronized())
			{
				/*
				 * Don't construct a schedule using an untrusted clock.
				 * Wait until something tells us synchronization has occurred.
				 */
				ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
				continue;
			}

			const std::tm local = _time_service.local_time();

			if (!_schedule_built ||
			    local.tm_yday != _schedule_day)
			{
				rebuild_schedule();

				_schedule_day = local.tm_yday;
				_schedule_built = true;
			}

			const ScheduledEvent* event = next_event();

			if (event == nullptr)
			{
				/*
				 * Nothing remains for today.
				 *
				 * Wake periodically so that we notice the next day.
				 * This can later be replaced with a proper next-day
				 * deadline / power-management integration.
				 */
				ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60 * 1000));
				continue;
			}

			if (!wait_until(event->timestamp))
				continue;

			/*
			 * Re-read the time after waking. The clock may have changed
			 * while we were blocked.
			 */
			if (_time_service.absolute_time() < event->timestamp)
				continue;

			event::Event output {
				event::Metadata {
					.source = event::Source::Scheduler,
					.sequence_id = 0, // replace with member counter
					.timestamp = xTaskGetTickCount()
				},
				event->event_data
			}
			;

			_event_dispatcher.publish(output);

			remove_next_event();
		}
	}
}