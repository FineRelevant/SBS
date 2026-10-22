idf_component_register(
    SRCS
    	"main.cpp"
    	"led.cpp"
    	"events.cpp"
    	"task.cpp"
    	"scheduler.cpp"
    	"wifi.cpp"
    	"application.cpp"
		"http_server.cpp"
    	"http_client.cpp"
    	"provisioning.cpp"
    	"filesystem.cpp"
    	"nvs.cpp"
		"internet_config.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash esp_http_server esp_http_client)