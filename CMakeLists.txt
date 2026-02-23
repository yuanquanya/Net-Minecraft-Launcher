cmake_minimum_required(VERSION 3.16)

project(SimpleMCLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6 (Core + Network + Gui + Concurrent)
# Note: WebSockets module might be missing in some minimal Qt installations.
# If you encounter build errors, ensure Qt WebSockets component is installed.
find_package(Qt6 REQUIRED COMPONENTS Core Network Gui Concurrent)
find_package(Qt6 COMPONENTS WebSockets)

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
    Qt6::Concurrent
)

if(Qt6WebSockets_FOUND)
    target_link_libraries(NetMinecraftLauncher PRIVATE Qt6::WebSockets)
    target_compile_definitions(NetMinecraftLauncher PRIVATE NMCL_USE_WEBSOCKETS)
else()
    message(WARNING "Qt6 WebSockets not found! WebSocket features will be disabled.")
endif()

# Windows: 部署 Qt 与 MinGW 运行时 DLL，使 exe 可独立运行
if(WIN32)
    # 获取 Qt bin 目录 (windeployqt)
    get_filename_component(QT_CMAKE_DIR "${Qt6_DIR}" DIRECTORY)
    get_filename_component(QT_LIB_DIR "${QT_CMAKE_DIR}" DIRECTORY)
    get_filename_component(QT_KIT_ROOT "${QT_LIB_DIR}" DIRECTORY)
    set(QT_BIN_DIR "${QT_KIT_ROOT}/bin")
    set(WINDEPLOYQT "${QT_BIN_DIR}/windeployqt.exe")

    # 获取 MinGW 运行时目录
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    add_custom_command(TARGET NetMinecraftLauncher POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations "$<TARGET_FILE:NetMinecraftLauncher>"
        COMMENT "Deploying Qt DLLs with windeployqt..."
    )

    # 复制 MinGW 运行时 DLL
    add_custom_command(TARGET NetMinecraftLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll" "$<TARGET_FILE_DIR:NetMinecraftLauncher>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN_DIR}/libstdc++-6.dll" "$<TARGET_FILE_DIR:NetMinecraftLauncher>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN_DIR}/libwinpthread-1.dll" "$<TARGET_FILE_DIR:NetMinecraftLauncher>"
        COMMENT "Copying MinGW runtime DLLs..."
    )
endif()

# For Windows specifically
if(WIN32)
    # console app is fine for server
    # set_target_properties(NetMinecraftLauncher PROPERTIES WIN32_EXECUTABLE ON)
endif()
