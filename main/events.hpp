#pragma once

#include <cstdint>
#include <variant>
#include <array>
#include <type_traits>
#include <utility>
#include <tuple>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace event
{	
	enum class Source : uint8_t
	{
		Scheduler,
		Test
		// Etc.
	};
	
	// EVENT DATA TYPES BEGIN
	struct BellDue
	{
		uint8_t duration_s;
	};
	struct Test
	{
		uint8_t test_value;
	};
	struct ScheduleChanged
	{
		// Placeholder for actual schedule object pointer
		void* schedule_ptr;
	};
	
	// EVENT DATA TYPES END
	
	using Data = std::variant<
		BellDue,
		Test,
		ScheduleChanged
	>;
	
	struct Metadata
	{
		Source source;
		uint32_t sequence_id;
		TickType_t timestamp;
	};
	
	struct Event
	{
		Metadata metadata;
		Data data;
	};
	
	template<typename T, typename Variant>
		struct IsVariantAlternative;

	template<typename T, typename... Ts>
		struct IsVariantAlternative<T, std::variant<Ts...>> 
		{
			static constexpr bool value = (std::is_same_v<T, Ts> || ...);
		};

	template<typename T>
		concept DataType =
		    IsVariantAlternative<T, event::Data>::value;
	
	class EventQueue
	{
	public:
		static constexpr std::size_t queue_length = 32;

	public:
		EventQueue();

		EventQueue(const EventQueue&) = delete;
		EventQueue& operator=(const EventQueue&) = delete;

		bool publish(const Event& event);

		bool publish_from_isr(const Event& event, BaseType_t* higher_task_priority_woken = nullptr);

		bool receive(Event& event, TickType_t timeout = portMAX_DELAY);

	private:
		QueueHandle_t _queue = nullptr;

		StaticQueue_t _control_block {};

		alignas(Event)
		std::array<Event, queue_length> _storage {};
	};
	
	class EventDispatcher
	{
	public:
		static constexpr std::size_t max_handlers_per_type = 8;
			
	private:
		
		template<DataType T>
		class HandlerList
		{
		public:
			using Callback = void(*)(const Event&, const T&, void* context);
			
			struct Handler
			{
				Callback callback;
				void* context;
			};
			
			bool add_handler(Callback callback, void* context)
			{
				if (callback == nullptr || _count >= max_handlers_per_type)
					return false;
				
				_handlers[_count++] = {callback, context};
				return true;
			}
			
			void invoke_handlers(const Event& event, const T& data) const
			{
				for (std::size_t i = 0; i < _count; ++i)
				{
					const auto& handler = _handlers[i];

					handler.callback(event, data, handler.context);
				}
			}
		private:
			std::array<Handler, max_handlers_per_type> _handlers {};
			std::size_t _count = 0;
		};
		
		template<typename Variant>
		struct HandlerLists;
		
		template<typename... Ts>
		struct HandlerLists<std::variant<Ts...>>
		{
			using type = std::tuple<HandlerList<Ts>...>;
		};
		
		using HandlerLists_t = HandlerLists<Data>::type;
	public:
		explicit EventDispatcher(EventQueue& queue)
			: _queue(queue)
		{}
		~EventDispatcher() = default;
		
		template<DataType T>
		// A subscription is permanent
		bool subscribe(typename HandlerList<T>::Callback callback, void* context = nullptr)
		{
			return std::get<HandlerList<T>>(_handler_lists).add_handler(callback, context);
		}
		
		void run();
		
	private:
		void dispatch(const Event& event);
		
	private:
		EventQueue& _queue;
		HandlerLists_t _handler_lists {};
	};
}