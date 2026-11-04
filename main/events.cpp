#include "events.hpp"

namespace event
{

	// ============================================================
	// EventQueue
	// ============================================================

	EventQueue::EventQueue()
	{
		queue_ = xQueueCreateStatic(queue_length, sizeof(Event), reinterpret_cast<uint8_t*>(storage_.data()), &control_block_);
	}


	bool EventQueue::publish(const Event& event)
	{
		if (queue_ == nullptr)
			return false;

		return xQueueSend(queue_, &event, portMAX_DELAY) == pdTRUE;
	}


	bool EventQueue::publish_from_isr(const Event& event, BaseType_t* higher_task_priority_woken)
	{
		if (queue_ == nullptr)
			return false;

		BaseType_t local_woken = pdFALSE;

		if (higher_task_priority_woken == nullptr)
		{
			higher_task_priority_woken = &local_woken;
		}

		return xQueueSendFromISR(queue_, &event, higher_task_priority_woken) == pdTRUE;
	}


	bool EventQueue::receive(Event& event, TickType_t timeout)
	{
		if (queue_ == nullptr)
		{
			return false;
		}

		return xQueueReceive(queue_, &event, timeout) == pdTRUE;
	}


	// ============================================================
	// EventDispatcher
	// ============================================================

	void EventDispatcher::run()
	{
		Event event;

		while (true)
		{
			if (queue_.receive(event))
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

				auto& handlers = std::get<HandlerList<DataType>>(handler_lists_);

				handlers.invoke(event, data);
			}, event.data);
	}

}