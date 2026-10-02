#pragma once

#include "led_strip.h"
#include "sdkconfig.h"

struct Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

class LEDOnboardController
{
public:
	LEDOnboardController(uint8_t led_gpio);
	void set_color(const Color& color, uint8_t brightness = 255);
	Color get_color();
	uint8_t get_brightness();
private:
	led_strip_handle_t _led_strip = {};
	Color _color = {0, 0, 0};
	uint8_t _brightness = 255;
};