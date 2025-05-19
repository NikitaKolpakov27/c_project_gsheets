cmake_minimum_required(VERSION 3.10)
project(google_sheets C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Настройки для библиотек
set(CURL_ROOT "D:/ProgTools/mingw32/include/curl-8.13.0")    # Укажите ваш путь к CURL
set(CJSON_ROOT "D:/ProgTools/cJSON-master")  # Укажите ваш путь к cJSON

# Поиск необходимых библиотек
find_package(CURL REQUIRED)
find_package(cJSON REQUIRED)

# Добавление исполняемого файла
add_executable(google_sheets
    google_sheets.c
)

# Подключение библиотек
target_include_directories(google_sheets PRIVATE
    ${CURL_INCLUDE_DIR}
    ${CJSON_INCLUDE_DIR}
)

target_link_libraries(google_sheets
    ${CURL_LIBRARIES}
    ${cJSON_LIBRARY}
)

# Для Windows добавляем системные библиотеки
if(WIN32)
    target_link_libraries(google_sheets
        wldap32
        ws2_32
        crypt32
        advapi32
    )
    
    # Добавляем определение для Windows-специфичных функций
    target_compile_definitions(google_sheets PRIVATE
        -D_WIN32
    )
endif()

# Пути к библиотекам (если не найдены через find_package)
if(NOT CURL_FOUND)
    message(WARNING "Using manual CURL paths")
    include_directories(${CURL_ROOT}/include)
    link_directories(${CURL_ROOT}/lib)
endif()

if(NOT cJSON_FOUND)
    message(WARNING "Using manual cJSON paths")
    include_directories(${CJSON_ROOT})
    link_directories(${CJSON_ROOT}/build)
endif()