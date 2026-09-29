cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)

find_package(RTIConnextDDS 7.7.0 REQUIRED)

include(ConnextDdsCodegen)

connextdds_rtiddsgen_run(
    IDL_FILE "${CMAKE_SOURCE_DIR}/idl/Mining/MiningMsgTypes.idl"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen/Mining"
    LANG C++11
)

add_library(
    MiningMsgTypes_typesupport_pro_cpp2
    OBJECT
        "${MiningMsgTypes_CXX11_SOURCES}"
)

target_include_directories(
    MiningMsgTypes_typesupport_pro_cpp2
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen/Mining>
)

target_link_libraries(
    MiningMsgTypes_typesupport_pro_cpp2
    PUBLIC
        RTIConnextDDS::cpp2_api
)


add_executable(
    control_station
    "${CMAKE_CURRENT_SOURCE_DIR}/control_station.cpp"
)

target_link_libraries(
    control_station
    PRIVATE
        MiningMsgTypes_typesupport_pro_cpp2
)