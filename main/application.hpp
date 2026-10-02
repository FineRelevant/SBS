#pragma once

#include "events.hpp"
#include "task.hpp"

class Application
{
public:
	Application();
	
	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;
	
	void start();
private:
	// Event System
	event::EventQueue _event_queue;
	event::EventDispatcher _event_dispatcher;
	StaticTask<event::EventDispatcher> _event_dispatcher_task;
	
	// Other systems
	
};