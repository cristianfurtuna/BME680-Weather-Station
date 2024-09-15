# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c  wifi.c  timesetup.c led_builtin.c  # list the source files of this component
    INCLUDE_DIRS       "." # optional, add here public include directories
    
)

spiffs_create_partition_image(storage ../spiffs_image FLASH_IN_PROJECT)
