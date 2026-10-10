cmake_minimum_required(VERSION 3.16)
project(line-hmi-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)
if (NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS SerialPort)
endif()

qt_add_executable(line-hmi-qt MANUAL_FINALIZATION
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MainWindow.ui
    src/NatsClient.cpp
    src/NatsClient.h
    src/PlcClient.cpp
    src/PlcClient.h
    src/PrepOverlay.cpp
    src/PrepOverlay.h
    src/RecoveryOverlay.cpp
    src/RecoveryOverlay.h
    src/ConnectionSettings.cpp
    src/ConnectionSettings.h
    src/RuntimeState.cpp
    src/RuntimeState.h
    src/ArduinoLink.cpp
    src/ArduinoLink.h
    src/AdminPanel.cpp
    src/AdminPanel.h
    src/ServicePanel.cpp
    src/ServicePanel.h
    src/Texts.cpp
    src/Texts.h
    resources.qrc
)

# Тексты пульта: каждая сборка дописывает новые тексты в раздел "texts"
# config.json (правки не трогает) и кладёт config.json рядом с программой.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB HMI_TEXT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ui")
add_custom_target(hmi_texts ALL
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/scripts/update_texts_config.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/config.json" ${HMI_TEXT_SOURCES}
    COMMENT "Update texts in config.json"
    VERBATIM
)
add_dependencies(line-hmi-qt hmi_texts)

target_link_libraries(line-hmi-qt PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network)
if (NOT ANDROID)
    target_link_libraries(line-hmi-qt PRIVATE Qt6::SerialPort)
    target_compile_definitions(line-hmi-qt PRIVATE LINE_HMI_HAS_SERIALPORT=1)
endif()

if (ANDROID)
    set_target_properties(line-hmi-qt PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "org.scara.linehmi"
        QT_ANDROID_APP_NAME "SCARA HMI"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_VERSION_NAME "1.0"
        QT_ANDROID_MIN_SDK_VERSION 26
        QT_ANDROID_TARGET_SDK_VERSION 34
        QT_ANDROID_ABIS "arm64-v8a"
    )

    set(ANDROID_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android/assets")
    file(MAKE_DIRECTORY "${ANDROID_ASSETS_DIR}")
    add_custom_command(
        OUTPUT "${ANDROID_ASSETS_DIR}/config.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
            "${ANDROID_ASSETS_DIR}/config.json"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
        COMMENT "Copy config.json to Android assets"
    )
    add_custom_target(copy_android_config
        DEPENDS "${ANDROID_ASSETS_DIR}/config.json")
    add_dependencies(copy_android_config hmi_texts)
    add_dependencies(line-hmi-qt copy_android_config)
endif()

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
if (ANDROID)
    set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)
endif()

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

qt_finalize_executable(line-hmi-qt)

if (ANDROID)
    add_dependencies(line-hmi-qt_make_apk copy_android_config)
endif()

if (NOT ANDROID)
    # Отдельной целью, а не POST_BUILD: правка текстов без пересборки программы
    # тоже должна попасть рядом с ней.
    add_custom_target(copy_config ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
            "$<TARGET_FILE_DIR:line-hmi-qt>/config.json"
        COMMENT "Copy config.json next to line-hmi-qt"
    )
    add_dependencies(copy_config hmi_texts line-hmi-qt)
    install(TARGETS line-hmi-qt RUNTIME DESTINATION bin)
    install(FILES config.json DESTINATION bin)
endif()