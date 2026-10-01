cmake_minimum_required(VERSION 3.13)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT DEFINED RTIME_TARGET_NAME AND DEFINED ENV{RTIME_TARGET_NAME})
    set(RTIME_TARGET_NAME $ENV{RTIME_TARGET_NAME})
endif()

if(RTIME_TARGET_NAME MATCHES "CERT-")
    add_compile_definitions(RTI_CERT=1)
    set(BUILD_SHARED_LIBS OFF)
endif()

find_package(RTIConnextMicroDDS 4.3.0 REQUIRED 
    COMPONENTS 
        c
        dpde
        appgen
        filter
)

include(ConnextDdsCodegen)

connextdds_rtiddsgen_run(
    IDL_FILE "${CMAKE_SOURCE_DIR}/idl/HpcSafety/HpcSafetyTypes.idl"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen/HpcSafety"
    MICRO
    LANG C
    EXTRA_ARGS -interpreted 0
)
add_library(
    HpcSafetyTypes_typesupport_micro_c
        "${HpcSafetyTypes_C_SOURCES}"
)
target_include_directories(
    HpcSafetyTypes_typesupport_micro_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen/HpcSafety>
)
target_link_libraries(
    HpcSafetyTypes_typesupport_micro_c
    PUBLIC
        RTIConnextMicroDDS::c
        ${RTIME_EXTERNAL_LIBS}
)

connextdds_rtiddsmag_run(
    XML_FILE "${CMAKE_SOURCE_DIR}/config/system/HpcSafetySystem.xml"
    IDL_FILE "HpcSafety/HpcSafetyTypes.idl"
    REFERENCED_FILES
        "${CMAKE_SOURCE_DIR}/config/qos/HpcSafetyQos.xml"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen/HpcSafety"
    LANG C
)
add_library(
    HpcSafetySystem_appsupport_micro_c
        "${HpcSafetySystem_C_APPGEN_SOURCES}"
)
target_include_directories(
    HpcSafetySystem_appsupport_micro_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen/HpcSafety>
)
target_link_libraries(
    HpcSafetySystem_appsupport_micro_c
    PUBLIC
        RTIConnextMicroDDS::appgen
        RTIConnextMicroDDS::filter
        RTIConnextMicroDDS::dpde
        RTIConnextMicroDDS::netio
        RTIConnextMicroDDS::c_api
        RTIConnextMicroDDS::osapi
        HpcSafetyTypes_typesupport_micro_c
)


add_executable(
    safety_controller
    "${CMAKE_CURRENT_SOURCE_DIR}/safety_controller_service.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/safety_controller.c"
)

target_link_libraries(
    safety_controller
    PRIVATE
        HpcSafetySystem_appsupport_micro_c
)