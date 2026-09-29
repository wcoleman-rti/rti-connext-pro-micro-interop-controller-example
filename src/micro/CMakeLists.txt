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
)

include(ConnextDdsCodegen)

connextdds_rtiddsgen_run(
    IDL_FILE "${CMAKE_SOURCE_DIR}/idl/Mining/MiningMsgTypes.idl"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen/Mining"
    MICRO
    LANG C
    EXTRA_ARGS -interpreted 0
)
add_library(
    MiningMsgTypes_typesupport_micro_c
        "${MiningMsgTypes_C_SOURCES}"
)
target_include_directories(
    MiningMsgTypes_typesupport_micro_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen/Mining>
)
target_link_libraries(
    MiningMsgTypes_typesupport_micro_c
    PUBLIC
        RTIConnextMicroDDS::c
        ${RTIME_EXTERNAL_LIBS}
)

function(concat_dds_xml_files OUTPUT_FILE)
    set(combined_content "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<dds>\n")

    foreach(input_file ${ARGN})
        if(EXISTS "${input_file}")
            file(READ "${input_file}" file_content)

            string(REGEX REPLACE "<\\?xml[^>]*\\?>" "" file_content "${file_content}")
            string(REGEX REPLACE "<dds[^>]*>" "" file_content "${file_content}")
            string(REGEX REPLACE "</dds>" "" file_content "${file_content}")

            string(STRIP "${file_content}" file_content)
            string(APPEND combined_content "${file_content}\n")
        endif()
    endforeach()

    string(APPEND combined_content "</dds>")
    file(WRITE "${OUTPUT_FILE}" "${combined_content}")
endfunction()

file(GLOB_RECURSE xml_service_files "${CMAKE_SOURCE_DIR}/config/service/*.xml")
concat_dds_xml_files("${CMAKE_CURRENT_BINARY_DIR}/gen/VehicleAgent.xml" 
    "${CMAKE_SOURCE_DIR}/config/domain/MiningDomain.xml"
    # "${CMAKE_SOURCE_DIR}/config/service/Mining/VehicleAgent.xml"
    ${xml_service_files}
)

connextdds_rtiddsmag_run(
    XML_FILE "${CMAKE_CURRENT_BINARY_DIR}/gen/VehicleAgent.xml"
    IDL_FILE "Mining/MiningMsgTypes.idl"
    REFERENCED_FILES "${CMAKE_SOURCE_DIR}/config/qos/MiningQos.xml"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen/Mining"
    LANG C
)
add_library(
    VehicleAgent_appsupport_micro_c
        "${VehicleAgent_C_APPGEN_SOURCES}"
)
target_include_directories(
    VehicleAgent_appsupport_micro_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen/Mining>
)
target_link_libraries(
    VehicleAgent_appsupport_micro_c
    PUBLIC
        RTIConnextMicroDDS::appgen
        RTIConnextMicroDDS::rhsm
        RTIConnextMicroDDS::whsm
        RTIConnextMicroDDS::dpde
        RTIConnextMicroDDS::netio
        RTIConnextMicroDDS::c_api
        RTIConnextMicroDDS::osapi
        MiningMsgTypes_typesupport_micro_c
)


add_executable(
    vehicle_agent
    "${CMAKE_CURRENT_SOURCE_DIR}/monotonic_clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/vehicle_agent_service.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/vehicle_agent.c"
)

target_link_libraries(
    vehicle_agent
    PRIVATE
        VehicleAgent_appsupport_micro_c
)