#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led.hpp"
#include "application.hpp"

namespace
{
	constexpr const char* TAG = "Testing grounds";
	uint8_t s_led_powered = 0;
}

void blink_led(LEDOnboardController& led_controller, const uint8_t led_state)
{
	if (led_state) {
		led_controller.set_color({255, 255, 50}, 1);
	}
	else {
		led_controller.set_color({0, 0, 0}, 0);
	}
}

extern "C" void app_main(void)
{
	static Application application;
	application.start();
	// that's it, the loop is managed by application
	
	//test
}