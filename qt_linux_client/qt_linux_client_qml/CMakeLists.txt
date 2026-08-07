cmake_minimum_required(VERSION 3.16)

project(qt_linux_client_qml LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Sql)
find_package(Qt5 QUIET COMPONENTS Mqtt)

set(QT_LINUX_CLIENT_SOURCES
    AppController.cpp
    GatewayMqttClient.cpp
    main.cpp
    qml.qrc
)

set(QT_LINUX_CLIENT_HEADERS
    AppController.h
    GatewayMqttClient.h
)

add_executable(qt_linux_client_qml
    ${QT_LINUX_CLIENT_SOURCES}
    ${QT_LINUX_CLIENT_HEADERS}
)

target_link_libraries(qt_linux_client_qml PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Qml
    Qt5::Quick
    Qt5::QuickControls2
    Qt5::Network
    Qt5::Sql
)

if (TARGET Qt5::Mqtt)
    target_link_libraries(qt_linux_client_qml PRIVATE Qt5::Mqtt)
    target_compile_definitions(qt_linux_client_qml PRIVATE HAVE_QT_MQTT)
    message(STATUS "Qt MQTT module found, enabling MQTT support")
else()
    message(STATUS "Qt MQTT module not found, building HTTP-only MQTT stub")
endif()

if (UNIX AND NOT APPLE)
    install(TARGETS qt_linux_client_qml RUNTIME DESTINATION bin)
endif()
