cmake_minimum_required(VERSION 3.20)
project(aurora_control_fw_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(APP_SOURCES
    app/src/app.c
    app/src/measurement.c
    app/src/mppt.c
    app/src/charger.c
    app/src/protection.c
    app/src/power_stage.c
    app/src/ui.c
    app/src/protocol.c
    app/src/storage.c
)

set(SERVICE_SOURCES
    service/service.c
    service/debug.c
)

add_executable(aurora_host_tests
    ${APP_SOURCES}
    ${SERVICE_SOURCES}
    board/board.c
    tests/mock_driver.c
    tests/test_main.c
)

target_include_directories(aurora_host_tests PRIVATE
    app/inc service driver/inc board tests
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aurora_host_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
    )
endif()

add_test(NAME aurora_host_tests COMMAND aurora_host_tests)

add_executable(aurora_host_debug_tests
    ${APP_SOURCES}
    ${SERVICE_SOURCES}
    board/board.c
    tests/mock_driver.c
    tests/test_main.c
)

target_include_directories(aurora_host_debug_tests PRIVATE
    app/inc service driver/inc board tests
)

target_compile_definitions(aurora_host_debug_tests PRIVATE
    AURORA_HOST_DEBUG_TEST=1
    DEBUG_ENABLE=1
    BOARD_USART_MODE=BOARD_USART_MODE_DEBUG
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aurora_host_debug_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
    )
endif()

add_test(NAME aurora_host_debug_tests COMMAND aurora_host_debug_tests)
