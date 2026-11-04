#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "application.hpp"

static Application g_app;

extern "C" void app_main(void)
{
	g_app.start();

	while (true)
    {
        vTaskDelay(portMAX_DELAY);   // or a long delay
    }
}