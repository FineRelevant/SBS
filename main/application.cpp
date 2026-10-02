#include "application.hpp"

Application::Application()
	: _event_queue(), _event_dispatcher(_event_queue), _event_dispatcher_task(_event_dispatcher, "event_dispatcher", 5)
	// etc
{
	// Initialization code if necessary
}

void Application::start()
{
	_event_dispatcher_task.start();
}