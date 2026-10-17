#include "events.hpp"

namespace event
{

	// ============================================================
	// EventQueue
	// ============================================================

	EventQueue::EventQueue()
	{
		_queue = xQueueCreateStatic(queue_length, sizeof(Event), reinterpret_cast<uint8_t*>(_storage.data()), &_control_block);
	}


	bool EventQueue::publish(const Event& event)
	{
		if (_queue == nullptr)
			return false;

		return xQueueSend(_queue, &event, portMAX_DELAY) == pdTRUE;
	}


	bool EventQueue::publish_from_isr(const Event& event, BaseType_t* higher_task_priority_woken)
	{
		if (_queue == nullptr)
			return false;

		BaseType_t local_woken = pdFALSE;

		if (higher_task_priority_woken == nullptr)
		{
			higher_task_priority_woken = &local_woken;
		}

		return xQueueSendFromISR(_queue, &event, higher_task_priority_woken) == pdTRUE;
	}


	bool EventQueue::receive(Event& event, TickType_t timeout)
	{
		if (_queue == nullptr)
			return false;

		return xQueueReceive(_queue, &event, timeout) == pdTRUE;
	}


	// ============================================================
	// EventDispatcher
	// ============================================================

	void EventDispatcher::run()
	{
		Event event;

		while (true)
		{
			if (_queue.receive(event))
			{
				dispatch(event);
			}
		}
	}


	void EventDispatcher::dispatch(const Event& event)
	{
		std::visit(
		    [this, &event](const auto& data)
			{
				using DataType = std::decay_t<decltype(data)>;

				auto& handlers = std::get<HandlerList<DataType>>(_handler_lists);

				handlers.invoke(event, data);
			}, event.data);
	}

}