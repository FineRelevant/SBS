#pragma once

#include "wifi.hpp"
#include "nvs.hpp"
#include "http_server.hpp"
#include "http_client.hpp"

#include "esp_err.h"

namespace internet
{
    class Provisioning final
    {
        public:
        explicit Provisioning(
            WifiService &wifi_service
        );

    };
} // namespace internet