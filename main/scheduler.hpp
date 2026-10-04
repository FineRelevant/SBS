#pragma once

#include "events.hpp"
#include "cJSON.h"

#include <string>
#include <array>
#include <cstdint>
#include <utility>
#include <ctime>

namespace scheduler
{
	struct TimeOfDay
	{
		uint8_t hour;
		uint8_t minute;
	};
	struct Period
	{
		uint8_t id;
		
		TimeOfDay start;
		TimeOfDay end;
	};

	struct Schedule
	{
		static constexpr size_t max_periods = 20;
		std::string name;
		std::string description;
	
		std::array<Period, max_periods> periods {};
		std::size_t period_count = 0;
	};

	class SchedulerConfig
	{
	public:
		static constexpr size_t max_schedules = 10;
	
		std::string timezone;
		uint32_t duration_ms;
		uint32_t warning_ms;
	
		std::array<Schedule, max_schedules> schedules;
		uint32_t schedule_count = 0;
		
		std::string active_schedule;
		
		const Schedule* get_active_schedule() const;
	};
	
	class SchedulerConfigParser
	{
	public:
		SchedulerConfig parse_from_json(const char* json);
	};

	class TimeService
	{
	public:
		bool synchronize();
		bool is_synchronized() const;
	
		bool set_timezone(const char* timezone);
	
		std::time_t absolute_time() const;
		std::tm local_time() const;
		
		uint32_t ms_since_midnight() const;
	private:
		// TODO: implement sntp internals later, when internet service will be ready
	};

	class Scheduler
	{
	public:
		Scheduler(SchedulerConfig& config, TimeService& time_service, event::EventDispatcher& event_dispatcher)
			: _config(config), _time_service(time_service), _event_dispatcher(event_dispatcher)
		{}
		void run();
	private:
		const SchedulerConfig& _config;
		TimeService& _time_service;
		event::EventDispatcher& _event_dispatcher;
	};
}