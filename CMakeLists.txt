cmake_minimum_required(VERSION 3.20)
project(aurora_control_fw_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(APP_SOURCES
    app/app.c
    app/measurement.c
    app/mppt.c
    app/charger.c
    app/protection.c
    app/power_stage.c
    app/ui.c
    app/protocol.c
    app/storage.c
)

add_executable(aurora_host_tests
    ${APP_SOURCES}
    service/service.c
    board/board.c
    tests/mock_driver.c
    tests/test_main.c
)

target_include_directories(aurora_host_tests PRIVATE
    app service driver board tests
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aurora_host_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
    )
endif()

add_test(NAME aurora_host_tests COMMAND aurora_host_tests)
