#pragma once

#include <array>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace event
{
	enum class Source
	{
		Scheduler = 0,
		Test,
		Network
	};

	using IPv4Address = std::uint32_t;
	using MacAddress = std::array<std::uint8_t, 6>;

	// -------------------------------------------------------------------------
	// Event data
	// -------------------------------------------------------------------------

	struct BellDue
	{
		std::uint32_t duration_ms;
	};

	struct Test
	{
		std::uint8_t test_value;
	};

	struct ScheduleChanged
	{
	};

	// Wi-Fi station

	struct WifiStationStarted
	{
	};

	struct WifiStationStopped
	{
	};

	struct WifiStationConnected
	{
		MacAddress bssid;
		std::int8_t rssi;
		std::uint8_t channel;
	};

	struct WifiStationDisconnected
	{
		std::uint8_t reason;
	};

	struct WifiStationGotIp
	{
		IPv4Address address;
		IPv4Address netmask;
		IPv4Address gateway;
	};

	struct WifiStationLostIp
	{
	};

	// Wi-Fi access point

	struct WifiApStarted
	{
		MacAddress mac;
	};

	struct WifiApStopped
	{
		MacAddress mac;
	};

	struct WifiApClientConnected
	{
		MacAddress mac;
	};

	struct WifiApClientDisconnected
	{
		MacAddress mac;
	};

	struct WifiProvisioningStarted
	{
	};

	struct WifiProvisioningStopped
	{
	};

	struct WifiProvisioningCompleted
	{
	};

	struct WifiProvisioningFailed
	{
		std::int32_t error;
	};

	// -------------------------------------------------------------------------
	// Event infrastructure
	// -------------------------------------------------------------------------

	using Data = std::variant<
		BellDue,
		Test,
		ScheduleChanged,

		WifiStationStarted,
		WifiStationStopped,
		WifiStationConnected,
		WifiStationDisconnected,
		WifiStationGotIp,
		WifiStationLostIp,

		WifiApStarted,
		WifiApStopped,
		WifiApClientConnected,
		WifiApClientDisconnected,

		WifiProvisioningStarted,
		WifiProvisioningStopped,
		WifiProvisioningCompleted,
		WifiProvisioningFailed>;

	struct Metadata
	{
		Source source{};
		std::uint32_t sequence_id{};
		TickType_t timestamp{};
	};

	struct Event
	{
		Metadata metadata;
		Data data;
	};

	template <typename T, typename Variant>
	struct IsVariantAlternative;

	template <typename T, typename... Ts>
	struct IsVariantAlternative<T, std::variant<Ts...>>
	{
		static constexpr bool value =
			(std::is_same_v<T, Ts> || ...);
	};

	template <typename T>
	concept DataType =
		IsVariantAlternative<T, Data>::value;

	class EventQueue
	{
	  public:
		static constexpr std::size_t queue_length = 32;

		EventQueue();

		EventQueue(const EventQueue &) = delete;
		EventQueue &operator=(const EventQueue &) = delete;

		bool publish(const Event &event);

		bool publish_from_isr(
			const Event &event,
			BaseType_t *higher_task_priority_woken = nullptr);

		bool receive(
			Event &event,
			TickType_t timeout = portMAX_DELAY);

	  private:
		QueueHandle_t _queue{nullptr};

		StaticQueue_t _control_block{};

		alignas(Event)
			std::array<Event, queue_length> _storage{};
	};

	class EventDispatcher
	{
	  public:
		static constexpr std::size_t max_handlers_per_type = 8;

	  private:
		template <DataType T>
		class HandlerList
		{
		  public:
			using Callback =
				void (*)(const Event &, const T &, void *context);

			struct Handler
			{
				Callback callback{};
				void *context{};
			};

			bool add_handler(
				Callback callback,
				void *context)
			{
				if (callback == nullptr ||
					_count >= max_handlers_per_type)
				{
					return false;
				}

				_handlers[_count++] = {
					.callback = callback,
					.context = context};

				return true;
			}

			void invoke(
				const Event &event,
				const T &data) const
			{
				for (std::size_t i = 0; i < _count; ++i)
				{
					const auto &handler = _handlers[i];

					handler.callback(
						event,
						data,
						handler.context);
				}
			}

		  private:
			std::array<Handler, max_handlers_per_type> _handlers{};
			std::size_t _count{0};
		};

		template <typename Variant>
		struct HandlerLists;

		template <typename... Ts>
		struct HandlerLists<std::variant<Ts...>>
		{
			using type = std::tuple<HandlerList<Ts>...>;
		};

		using HandlerLists_t = HandlerLists<Data>::type;

	  public:
		explicit EventDispatcher(EventQueue &queue)
			: _queue(queue)
		{
		}

		EventDispatcher(const EventDispatcher &) = delete;
		EventDispatcher &operator=(const EventDispatcher &) = delete;

		template <DataType T>
		bool subscribe(
			typename HandlerList<T>::Callback callback,
			void *context = nullptr)
		{
			return std::get<HandlerList<T>>(
					   _handler_lists)
				.add_handler(callback, context);
		}

		template <DataType T>
		bool publish(
			Source source,
			T data)
		{
			Event event{
				.metadata = {
					.source = source,
					.sequence_id = _next_sequence_id++,
					.timestamp = xTaskGetTickCount()},
				.data = std::move(data)};

			return _queue.publish(event);
		}

		template <DataType T>
		bool publish_from_isr(
			Source source,
			T data,
			BaseType_t *higher_task_priority_woken = nullptr)
		{
			Event event{
				.metadata = {
					.source = source,
					.sequence_id = _next_sequence_id++,
					.timestamp = xTaskGetTickCountFromISR()},
				.data = std::move(data)};

			return _queue.publish_from_isr(
				event,
				higher_task_priority_woken);
		}

		void run();

	  private:
		void dispatch(const Event &event);

	  private:
		EventQueue &_queue;
		HandlerLists_t _handler_lists{};
		std::uint32_t _next_sequence_id{0};
	};
} // namespace event