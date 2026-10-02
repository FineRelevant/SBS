#pragma once

#include "events.hpp"
#include "cJSON.h"

#include <string>
#include <array>
#include <cstdint>
#include <utility>
#include <ctime>

struct Period
{
	uint8_t start_hour;
	uint8_t start_minute;
	uint8_t end_hour;
	uint8_t end_minute;
};

struct Schedule
{
	static constexpr size_t max_periods = 13;
	std::string name;
	std::array<Period, max_periods> periods;
};

enum class Schedules
{
	Regular = 0,
	ShortenedDay,
	ShortenedFirstHalf,
	ShortenedSecondHalf,
	Custom1,
	Custom2,
	Custom3,
	//etc
	
	Count
};

struct SchedulerConfig
{
	static constexpr size_t schedules_count = std::to_underlying(Schedules::Count);
	
	std::string timezone;
	uint8_t duration_seconds;
	uint32_t warning_seconds;
	Schedules active_schedule;
	
	std::array<Schedule, schedules_count> schedules;
};

class TimeService
{
	bool syncronize();
	bool is_synchrinized();
	
	bool set_timezone(const char* timezone);
	
	std::time_t now() const;
	bool local_time(std::tm& result) const;
};

class Scheduler
{
public:
	
private:
	
};