cmake_minimum_required(VERSION 3.16)

project(HostGUI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# The MinGW-hosted moc 6.11 on Windows crashes (0xC0000005) when fed
# moc_predefs.h alongside the GCC system include-path list — disabling
# the predefs forwarding sidesteps the crash without losing anything
# (we don't rely on compiler-specific macros inside any Q_OBJECT header).
set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network OpenGLWidgets)

file(GLOB_RECURSE HOSTGUI_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
)

add_executable(HostGUI WIN32
    ${HOSTGUI_SOURCES}
)

target_include_directories(HostGUI PRIVATE
    src
    src/core
    src/gui
)

target_link_libraries(HostGUI PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
)

# Windows: link DWM API so MainWindow can flip its title bar to dark mode
# via DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE).
if(WIN32)
    target_link_libraries(HostGUI PRIVATE dwmapi)
endif()

# Copy arm_model assets next to the exe so runtime can find them without
# the source tree. Idempotent — CMake checks mtimes.
add_custom_command(TARGET HostGUI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:HostGUI>/assets"
    COMMENT "Staging arm_model assets next to HostGUI.exe"
)

if(MSVC)
    target_compile_options(HostGUI PRIVATE /W4 /permissive-)
else()
    target_compile_options(HostGUI PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    set_target_properties(HostGUI PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${QT_HOST_PATH}/bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_target(deploy_windows
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release "$<TARGET_FILE:HostGUI>"
            DEPENDS HostGUI
            COMMENT "Deploying Qt runtime files next to HostGUI.exe"
        )
    endif()
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(HostGUI)
endif()
