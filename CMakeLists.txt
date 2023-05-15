# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG "sdkconfig")
set(SDKCONFIG_DEFAULT "sdkconfig.defaults;sdkconfig.defaults.esp32")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_prog_vscode_debug)
