
#include "peripherals.hpp"

#include "esp_check.h"
#include "esp_log.h"

#include <algorithm>

#include "freertos/task.h"

namespace
{
    constexpr char k_tag[] = "hardware";

    TickType_t milliseconds_to_ticks(uint32_t milliseconds)
    {
        const uint64_t ticks =
            (static_cast<uint64_t>(milliseconds) *
             configTICK_RATE_HZ + 999ULL) / 1000ULL;

        return static_cast<TickType_t>(
            std::clamp<uint64_t>(
                ticks,
                1,
                portMAX_DELAY
            )
        );
    }
}

HardwareController::HardwareController(
    event::EventDispatcher& dispatcher)
    : event_dispatcher_(dispatcher)
{
}

esp_err_t HardwareController::start()
{
    if (started_)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // ---------------------------------------------------------------------
    // External LED
    // ---------------------------------------------------------------------

    gpio_config_t bell_config{};

    bell_config.pin_bit_mask = 1ULL << k_gpio_bell_;
    bell_config.mode = GPIO_MODE_OUTPUT;
    bell_config.pull_up_en = GPIO_PULLUP_DISABLE;
    bell_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    bell_config.intr_type = GPIO_INTR_DISABLE;

    ESP_RETURN_ON_ERROR(
        gpio_config(&bell_config),
        k_tag,
        "Bell GPIO configuration failed");

    set_external_led(false);

    // ---------------------------------------------------------------------
    // Button
    // ---------------------------------------------------------------------

    gpio_config_t button_config{};

    button_config.pin_bit_mask = 1ULL << k_gpio_button_;
    button_config.mode = GPIO_MODE_INPUT;
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    button_config.intr_type = GPIO_INTR_DISABLE;

    ESP_RETURN_ON_ERROR(
        gpio_config(&button_config),
        k_tag,
        "Button GPIO configuration failed");

    // ---------------------------------------------------------------------
    // Onboard RGB LED (WS2812)
    // ---------------------------------------------------------------------

    led_strip_config_t strip_config{};

    strip_config.strip_gpio_num = k_gpio_led_onboard_;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format =
        LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config{};

    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10'000'000;
    rmt_config.mem_block_symbols = 64;
    rmt_config.flags.with_dma = false;

    ESP_RETURN_ON_ERROR(
        led_strip_new_rmt_device(
            &strip_config,
            &rmt_config,
            &rgb_led_),
        k_tag,
        "RGB LED initialization failed");

    ESP_RETURN_ON_ERROR(
        led_strip_clear(rgb_led_),
        k_tag,
        "RGB LED initialization failed");

    // ---------------------------------------------------------------------
    // Bell software timer
    // ---------------------------------------------------------------------

    bell_timer_ = xTimerCreate(
        "bell_led",
        1,
        pdFALSE,
        this,
        &HardwareController::external_led_timeout);

    if (bell_timer_ == nullptr)
    {
        return ESP_ERR_NO_MEM;
    }

    // ---------------------------------------------------------------------
    // Event subscriptions
    // ---------------------------------------------------------------------

    if (!event_dispatcher_.subscribe<event::BellDue>(
            &HardwareController::on_bell_due, this))
    {
        return ESP_ERR_NO_MEM;
    }

    if (!event_dispatcher_.subscribe<event::ApplicationStateChanged>(
            &HardwareController::on_application_state_changed, this))
    {
        return ESP_ERR_NO_MEM;
    }

    // ---------------------------------------------------------------------
    // Button task
    // ---------------------------------------------------------------------

    if (xTaskCreate(
            &HardwareController::button_task,
            "button",
            3072,
            this,
            5,
            nullptr) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    started_ = true;

    // Initial application state.
    set_onboard_rgb(0, 0, 0);

    ESP_LOGI(k_tag, "Hardware controller started");

    return ESP_OK;
}

// ==========================================================================
// Event callbacks
// ==========================================================================

void HardwareController::on_bell_due(
    const event::Event&,
    const event::BellDue& event,
    void* context)
{
    auto* self = static_cast<HardwareController*>(context);
    self->handle_bell_due(event);
}

void HardwareController::on_application_state_changed(
    const event::Event&,
    const event::ApplicationStateChanged& event,
    void* context)
{
    auto* self = static_cast<HardwareController*>(context);
    self->handle_application_state_changed(event);
}

// ==========================================================================
// Bell
// ==========================================================================

void HardwareController::handle_bell_due(
    const event::BellDue& event)
{
    if (event.duration_ms == 0)
    {
        return;
    }

    const TickType_t duration =
        milliseconds_to_ticks(event.duration_ms);

    // ChangePeriod also starts/restarts a one-shot timer.
    if (xTimerChangePeriod(
            bell_timer_,
            duration,
            0) != pdPASS)
    {
        ESP_LOGE(k_tag, "Failed to start bell timer");
        return;
    }

    set_external_led(true);
}

void HardwareController::external_led_timeout(
    TimerHandle_t timer)
{
    auto* self = static_cast<HardwareController*>(
        pvTimerGetTimerID(timer));

    self->set_external_led(false);
}

void HardwareController::set_external_led(bool enabled)
{
    gpio_set_level(k_gpio_bell_, enabled ? 1 : 0);
}

// ==========================================================================
// Application state
// ==========================================================================

void HardwareController::handle_application_state_changed(
    const event::ApplicationStateChanged& event)
{
    application_state_.store(
        event.state,
        std::memory_order_relaxed);

    switch (event.state)
    {
        case event::ApplicationState::Configuration:
            set_onboard_rgb(0, 5, 0);
            break;

        case event::ApplicationState::Running:
        case event::ApplicationState::Synchronization:
            set_onboard_rgb(0, 0, 0);
            break;
    }
}

// ==========================================================================
// Button
// ==========================================================================

void HardwareController::button_task(void* context)
{
    auto* self = static_cast<HardwareController*>(context);

    constexpr TickType_t poll_interval =
        pdMS_TO_TICKS(k_button_poll_interval_ms_);

    constexpr TickType_t debounce_interval =
        pdMS_TO_TICKS(k_button_debounce_ms_);

    // Button uses internal pull-up:
    // HIGH = released (UP)
    // LOW  = pressed  (DOWN)

    int stable_state = 1;
    int candidate_state = 1;

    TickType_t candidate_since = xTaskGetTickCount();

    while (true)
    {
        const int current_state =
            gpio_get_level(k_gpio_button_);

        if (current_state != candidate_state)
        {
            candidate_state = current_state;
            candidate_since = xTaskGetTickCount();
        }

        const TickType_t now = xTaskGetTickCount();

        if (candidate_state != stable_state &&
            (now - candidate_since) >= debounce_interval)
        {
            stable_state = candidate_state;

            const auto next_state =
                stable_state == 0
                    ? event::ApplicationState::Configuration
                    : event::ApplicationState::Running;

            if (!self->event_dispatcher_.publish(
                    event::Source::Application,
                    event::ApplicationStateChanged{next_state}))
            {
                ESP_LOGW(
                    k_tag,
                    "Failed to publish application state change");
            }
        }

        vTaskDelay(poll_interval);
    }
}

// ==========================================================================
// Onboard RGB LED
// ==========================================================================

void HardwareController::set_onboard_rgb(
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    if (rgb_led_ == nullptr)
    {
        return;
    }

    const esp_err_t err =
        led_strip_set_pixel(rgb_led_, 0, r, g, b);

    if (err != ESP_OK)
    {
        ESP_LOGE(k_tag, "Failed to set RGB pixel: %s",
                 esp_err_to_name(err));
        return;
    }

    const esp_err_t refresh_err =
        led_strip_refresh(rgb_led_);

    if (refresh_err != ESP_OK)
    {
        ESP_LOGE(k_tag, "Failed to refresh RGB LED: %s",
                 esp_err_to_name(refresh_err));
    }
}
