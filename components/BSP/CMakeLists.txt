set(src_dirs
	I2C/i2c_driver.c
	MQTT/mqtt.c
	RTC/rtc_driver.c
	Buzzer/Buzzer.c)

set(include_dirs
	I2C
	MQTT
	RTC
	Buzzer)

set(requires
	Middlewares
    driver)

idf_component_register(SRCS "${src_dirs}"
					   INCLUDE_DIRS "${include_dirs}"
					   REQUIRES "${requires}"
					   REQUIRES driver esp_wifi esp_netif esp_event nvs_flash mqtt wifi_provisioning bt protocomm json)
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)