#pragma once

#include <array>
#include <cstddef>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

template <typename T, std::size_t stack_words = 1024>
class StaticTask final
{
  public:
	StaticTask(T &object, const char *name, UBaseType_t priority)
		: _object(object), _name(name), _priority(priority)
	{
	}
	StaticTask &operator=(const StaticTask &) = delete;
	StaticTask(const StaticTask &) = delete;
	~StaticTask()
	{
		stop();
	}

	bool start()
	{
		if (_handle != nullptr)
			return false;

		_handle = xTaskCreateStatic(&StaticTask::entry, _name, stack_words, this, _priority, _stack.data(), &_control_block);

		return _handle != nullptr;
	}

	void stop()
	{
		if (_handle == nullptr)
			return;
		vTaskDelete(_handle);

		_handle = nullptr;
	}

	TaskHandle_t handle()
	{
		return _handle;
	}

  private:
	static void entry(void *argument)
	{
		auto *task = reinterpret_cast<StaticTask *>(argument);

		task->_object.run();

		// If run() ever returns, terminate the task
		vTaskDelete(nullptr);
	}

  private:
	T &_object;

	const char *_name;
	UBaseType_t _priority;

	TaskHandle_t _handle = nullptr;

	StaticTask_t _control_block{};

	std::array<StackType_t, stack_words> _stack{};
};