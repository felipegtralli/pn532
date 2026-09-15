cmake_minimum_required(VERSION 3.16)

# Include component directory so example finds pn532 component
set(EXTRA_COMPONENT_DIRS "../../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pn532_spi_example)
