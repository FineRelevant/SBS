
#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "led_strip.h"

#include "events.hpp"

class HardwareController
{
public:
    explicit HardwareController(event::EventDispatcher& dispatcher);

    esp_err_t start();

private:
    static void on_bell_due(
        const event::Event&,
        const event::BellDue&,
        void* context);

    static void on_application_state_changed(
        const event::Event&,
        const event::ApplicationStateChanged&,
        void* context);

    static void button_task(void* context);

    static void external_led_timeout(TimerHandle_t timer);

    void handle_bell_due(const event::BellDue& event);

    void handle_application_state_changed(
        const event::ApplicationStateChanged& event);

    void set_external_led(bool enabled);
    void set_onboard_rgb(uint8_t r, uint8_t g, uint8_t b);

private:
    static constexpr gpio_num_t k_gpio_button_ = GPIO_NUM_15;
    static constexpr gpio_num_t k_gpio_bell_ = GPIO_NUM_20;
    static constexpr gpio_num_t k_gpio_led_onboard_ = GPIO_NUM_8;

    static constexpr uint32_t k_button_poll_interval_ms_ = 10;
    static constexpr uint32_t k_button_debounce_ms_ = 30;

    event::EventDispatcher& event_dispatcher_;

    TimerHandle_t bell_timer_{nullptr};
    led_strip_handle_t rgb_led_{nullptr};

    std::atomic<event::ApplicationState> application_state_{
        event::ApplicationState::Running
    };

    bool started_{false};
};
