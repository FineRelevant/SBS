#include "application.hpp"

#include "esp_log.h"

Application::Application()
	: _event_queue(), _event_dispatcher(_event_queue), _event_dispatcher_task(_event_dispatcher, "event_dispatcher", 5)
// etc
{
	
}

void Application::initialize()
{

}

void Application::start()
{

}

void Application::stop()
{

}