file(GLOB_RECURSE COMPONENT_SRCS "main.cpp" "*.cpp")

# Ensure embedded assets are always compiled into the main component.
set(MAIN_SRCS assets_arrow.cpp assets_arrow_outline.cpp assets_arrow_mtl.cpp ${COMPONENT_SRCS})

idf_component_register(SRCS ${MAIN_SRCS}
                       INCLUDE_DIRS 
                       "."
                       
                       REQUIRES 
                        arduino-esp32
                        esp_driver_ppa
                        esp_lcd
                        esp_io_expander 
                        esp_lcd_touch          # Base touch support
                        esp_lcd_touch_gt911    # The specific managed component
                        driver                 # For I2C Master and GPIO
                        nvs_flash              # NVS (non-volatile storage)
                        CANBus_Driver          # For CAN bus support
                        esp-nimble-cpp         # For Bluetooth support
                       )