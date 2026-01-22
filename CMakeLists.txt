# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Add local components explicitly to the build search path
set(EXTRA_COMPONENT_DIRS
	"components/ac_meter"
	"components/ac_relay"
	"components/ac_storage"
	"components/ds18b20"
	"components/power_control"
)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
# Use custom partition table
set(PARTITION_CSV_PATH ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv)
project(control_aire_acondicinado)
