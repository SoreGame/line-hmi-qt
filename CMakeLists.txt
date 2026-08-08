cmake_minimum_required(VERSION 3.16)
project(line-hmi-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

qt_add_executable(line-hmi-qt
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MainWindow.ui
    src/NatsClient.cpp
    src/NatsClient.h
    resources.qrc
    src/Natsclient.h
    src/Natsclient.cpp
)

target_link_libraries(line-hmi-qt PRIVATE Qt6::Widgets Qt6::Concurrent)

# --- nats.c как git-сабмодуль ----------------------------------------------
# Подключить сабмодуль (один раз, из корня репозитория):
#   git submodule add https://github.com/nats-io/nats.c.git extern/nats.c
#   git submodule update --init --recursive
#
# После клонирования репозитория с сабмодулями другими людьми:
#   git submodule update --init --recursive

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nats.c/CMakeLists.txt")
    message(FATAL_ERROR
        "nats.c пуст. Выполните:\n"
        "  git submodule add https://github.com/nats-io/nats.c.git extern/nats.c\n"
        "  git submodule update --init --recursive")
endif()

# Собираем только то, что нужно: без тестов/примеров, без TLS
# (сервер в локальной сети по IP, шифрование не требуется — включите
# NATS_BUILD_WITH_TLS, если понадобится, тогда нужен ещё и OpenSSL).
set(NATS_BUILD_STREAMING   OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_WITH_TLS    OFF CACHE BOOL "" FORCE)

add_subdirectory(nats.c EXCLUDE_FROM_ALL)

# CMake nats.c экспортирует таргет "nats_static" и/или "nats" в
# зависимости от версии/опций сборки — берём то, что реально есть.
if (TARGET nats_static)
    set(NATS_TARGET nats_static)
elseif (TARGET nats)
    set(NATS_TARGET nats)
else()
    message(FATAL_ERROR
        "После add_subdirectory(nats.c) не найден ни таргет "
        "'nats_static', ни 'nats'. Проверьте версию сабмодуля/опции сборки.")
endif()

target_link_libraries(line-hmi-qt PRIVATE ${NATS_TARGET})

if (WIN32)
    target_link_libraries(line-hmi-qt PRIVATE ws2_32)
endif()

# Заголовок nats.h лежит прямо в extern/nats.c/src (без nats/-префикса —
# он появляется только после `cmake --install`), поэтому в NatsClient.h
# используется #include <nats.h>, и include-путь указываем сюда напрямую.
target_include_directories(line-hmi-qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/nats.c/src
)
# ---------------------------------------------------------------------------

install(TARGETS line-hmi-qt RUNTIME DESTINATION bin)