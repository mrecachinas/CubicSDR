cmake_minimum_required(VERSION 3.10)
project(SoapyTestSource LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

find_package(SoapySDR REQUIRED)

add_library(SoapyTestSource MODULE SoapyTestSource.cpp)
target_link_libraries(SoapyTestSource PRIVATE SoapySDR)
set_target_properties(SoapyTestSource PROPERTIES PREFIX "")

# Install into SoapySDR module directory
# SoapySDR 0.8.x looks in modules0.8/
execute_process(
    COMMAND ${CMAKE_COMMAND} -E echo "SoapySDR ABI: ${SoapySDR_ABI_VERSION}"
)
if(NOT SoapySDR_ABI_VERSION)
    set(SoapySDR_ABI_VERSION "0.8")
endif()
set(SOAPY_MODULE_DIR "${CMAKE_INSTALL_PREFIX}/lib/SoapySDR/modules${SoapySDR_ABI_VERSION}"
    CACHE PATH "SoapySDR module install directory")

install(TARGETS SoapyTestSource
    LIBRARY DESTINATION ${SOAPY_MODULE_DIR}
)
