#include "scheduler.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstring>

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
}