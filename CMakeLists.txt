cmake_minimum_required(VERSION 3.16)
project(line-hmi-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(line-hmi-qt
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MainWindow.ui
    resources.qrc
)

target_link_libraries(line-hmi-qt PRIVATE Qt6::Widgets)

install(TARGETS line-hmi-qt RUNTIME DESTINATION bin)
