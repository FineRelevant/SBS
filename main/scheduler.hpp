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
		uint32_t id;
		
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
	private:
		// TODO: implement sntp internals later, when internet service will be ready
	};

	class SchedulerTask
	{
	private:
		static constexpr std::size_t stack_words = 1024;
		static constexpr std::size_t max_scheduled_events = 512;
	public:
		
		SchedulerTask(SchedulerConfig& config, TimeService& time_service, event::EventDispatcher& event_dispatcher, UBaseType_t priority)
			: _config(config), _time_service(time_service), _event_dispatcher(event_dispatcher), _priority(priority)
		{}
		SchedulerTask(const SchedulerTask&) = delete;
		SchedulerTask& operator=(const SchedulerTask&) = delete;
		~SchedulerTask()
		{
			stop();
		}
		
		bool start();
		void stop();
	private:
		struct ScheduledEvent
		{
			std::time_t timestamp;
			event::Data event_data;
		};
		SchedulerConfig& _config;
		TimeService& _time_service;
		event::EventDispatcher& _event_dispatcher;
		std::array<ScheduledEvent, max_scheduled_events> _scheduled_events;
		std::size_t _scheduled_events_count = 0;
		
		bool _schedule_built = false;
		uint32_t _schedule_day = -1;
		
		UBaseType_t _priority;
		TaskHandle_t _handle = nullptr;
		StaticTask_t _control_block {};
		std::array<StackType_t, stack_words> _stack {};
		
		void run();
		static void task_entry(void* argument);
		bool wait_until(std::time_t timestamp);
		void notify();
		
		// Maintains scheduled events order, removes expired events, i.e. those that can not be logically executed
		void schedule_event(std::time_t timestamp, event::Data event_data);
		const ScheduledEvent* next_event();
		void remove_next_event();
		void rebuild_schedule();
	};
}