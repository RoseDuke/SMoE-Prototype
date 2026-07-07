cmake_minimum_required(VERSION 3.16)

project(NexusMoESmartNICPrototype LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(smartnic_ref_model
    software_model/src/config.cpp
    software_model/src/metrics.cpp
    software_model/src/scheduler.cpp
    software_model/src/simulator.cpp
    software_model/src/trace_reader.cpp
)

target_include_directories(smartnic_ref_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/software_model/include
)

target_compile_options(smartnic_ref_model PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(smartnic_ref software_model/src/main.cpp)
target_link_libraries(smartnic_ref PRIVATE smartnic_ref_model)
target_compile_options(smartnic_ref PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(test_reference_model tests/test_reference_model.cpp)
target_link_libraries(test_reference_model PRIVATE smartnic_ref_model)
target_compile_options(test_reference_model PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_subdirectory(fpga)

enable_testing()
add_test(NAME reference_model_tests COMMAND test_reference_model)
