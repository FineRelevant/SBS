idf_component_register(
    SRCS
    	"main.cpp"
    	"led.cpp"
    	"events.cpp"
    	"task.cpp"
    	"scheduler.cpp"
    	"wifi.cpp"
    	"httpserver.cpp"
    	"application.cpp"
    	"httpclient.cpp"
    	"provisioning.cpp"
    	filesystem.cpp
    	nvs.cpp
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash)