cmake_minimum_required(VERSION 3.20.0)
    target_sources(app PRIVATE
         ble_connection.c
         ble_advertising.c
         ble_service.c
         ble_char_command.c
	)

