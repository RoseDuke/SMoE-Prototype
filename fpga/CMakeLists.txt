add_library(smartnic_fpga_v0
    hls/top.cpp
)

target_include_directories(smartnic_fpga_v0
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(smartnic_fpga_v0 PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(fpga_run_trace
    host/load_trace.cpp
    host/run_trace.cpp
)

target_include_directories(fpga_run_trace
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/host
)

target_link_libraries(fpga_run_trace PRIVATE smartnic_fpga_v0)
target_compile_options(fpga_run_trace PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(fpga_verify_against_sim
    host/verify_against_sim.cpp
)

target_compile_options(fpga_verify_against_sim PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(DEFINED ENV{XILINX_XRT})
    add_executable(fpga_run_trace_xrt
        host/load_trace.cpp
        host/run_trace_xrt.cpp
    )

    target_include_directories(fpga_run_trace_xrt
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/host
            $ENV{XILINX_XRT}/include
    )

    target_link_directories(fpga_run_trace_xrt
        PRIVATE
            $ENV{XILINX_XRT}/lib
    )

    target_link_libraries(fpga_run_trace_xrt PRIVATE xrt_coreutil pthread)
    target_compile_options(fpga_run_trace_xrt PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
