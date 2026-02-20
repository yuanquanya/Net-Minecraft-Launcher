cmake_minimum_required(VERSION 3.16)

project(SimpleMCLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6 (Core + Network + Gui)
find_package(Qt6 REQUIRED COMPONENTS Core Network Gui)

add_executable(NetMinecraftLauncher
    src/main.cpp
    src/LauncherCore.h
    src/LauncherCore.cpp
    src/HttpServer.h
    src/HttpServer.cpp
)

target_link_libraries(NetMinecraftLauncher PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Gui
)

# For Windows specifically
if(WIN32)
    # console app is fine for server
    # set_target_properties(NetMinecraftLauncher PROPERTIES WIN32_EXECUTABLE ON)
endif()
