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
	
	uint32_t TimeService::ms_since_midnight()
	{
		const std::tm time = local_time();
		
		return
			static_cast<uint32_t>(time.tm_hour) * ms_per_hour +
			static_cast<uint32_t>(time.tm_min) * ms_per_minute +
			static_cast<uint32_t>(time.tm_sec) * ms_per_second;
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
}