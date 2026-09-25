cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ai_code_pager)

option(LCD_DIAGNOSTIC_BUILD "Build the standalone ST7789 wiring diagnostic" OFF)

if(LCD_DIAGNOSTIC_BUILD)
  target_sources(app PRIVATE src/lcd_diagnostic.c)
else()
  target_include_directories(app PRIVATE
    src
    src/assets
  )

  target_sources(app PRIVATE
    src/main.c
    src/app_protocol.c
    src/ble.c
    src/input.c
    src/storage.c
    src/ui.c
    src/assets/pet_animations.c
  )
endif()
