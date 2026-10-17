#include "led.hpp"
#include "esp_err.h"

LEDOnboardController::LEDOnboardController(uint8_t led_gpio)
{
	// LED strip initialization with the GPIO and pixels number
	led_strip_config_t strip_config = {};
	strip_config.strip_gpio_num = led_gpio;
	strip_config.max_leds = 1;

	led_strip_rmt_config_t rmt_config = {};
	rmt_config.resolution_hz = 10 * 1000 * 1000;
	rmt_config.flags.with_dma = false;

	// Create a new RMT device for the LED strip
	ESP_ERROR_CHECK(
		led_strip_new_rmt_device(
			&strip_config,
		&rmt_config,
		&_led_strip)
);
	// Set all LED off
	led_strip_clear(_led_strip);
}
void LEDOnboardController::set_color(const Color& color, uint8_t brightness)
{
	led_strip_set_pixel(
		_led_strip,
		0,
		color.r * brightness / 255,
		color.g * brightness / 255,
		color.b * brightness / 255);

	led_strip_refresh(_led_strip);
	_color = color;
	_brightness = brightness;
}
Color LEDOnboardController::get_color()
{
	return _color;
}
uint8_t LEDOnboardController::get_brightness()
{
	return _brightness;
}