cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)

find_package(RTIConnextDDS 7.7.0 REQUIRED)

include(ConnextDdsCodegen)

connextdds_rtiddsgen_run(
    IDL_FILE "${CMAKE_SOURCE_DIR}/idl/HpcSafety/HpcSafetyTypes.idl"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen/HpcSafety"
    LANG C++11
)

add_library(
    HpcSafetyTypes_typesupport_pro_cpp2
    OBJECT
        "${HpcSafetyTypes_CXX11_SOURCES}"
)

target_include_directories(
    HpcSafetyTypes_typesupport_pro_cpp2
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gen/HpcSafety>
)

target_link_libraries(
    HpcSafetyTypes_typesupport_pro_cpp2
    PUBLIC
        RTIConnextDDS::cpp2_api
)


add_executable(
    hpc_node
    "${CMAKE_CURRENT_SOURCE_DIR}/hpc_node.cpp"
)

target_link_libraries(
    hpc_node
    PRIVATE
        HpcSafetyTypes_typesupport_pro_cpp2
)