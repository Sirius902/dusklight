cmake_minimum_required(VERSION 3.16)

set(RANDOMIZER_ONLY "0" CACHE STRING "Runs only the randomizer generator")
set(RANDO_SAVE_PATH "${CMAKE_BINARY_DIR}/randomizer/")

set(GAME_COMPILE_DEFS ${GAME_COMPILE_DEFS} 
                        RANDOMIZER_ONLY=${RANDOMIZER_ONLY}
                        RANDO_SAVE_PATH="${RANDO_SAVE_PATH}"
                        RANDO_DATA_PATH="src/dusk/randomizer/generator/data/"
                        RANDO_ASSETS_PATH="src/dusk/randomizer/assets/"
                        RANDO_LOGIC_TESTS_PATH="${CMAKE_SOURCE_DIR}/src/dusk/randomizer/generator/data/tests/logic")

if(RANDO_ERROR_LOG)
  message("Error Log will be saved")
  set(GAME_COMPILE_DEFS ${GAME_COMPILE_DEFS} RANDO_ERROR_LOG)
endif()

if(RANDO_DEBUG)
  set(GAME_COMPILE_DEFS ${GAME_COMPILE_DEFS} RANDO_DEBUG)
endif()

if(LOGIC_TESTS)
  message("Configuring for Logic Tests")

  set(GAME_COMPILE_DEFS ${GAME_COMPILE_DEFS} LOGIC_TESTS)

  if(TEST_COUNT)
    message("Test Count: " ${TEST_COUNT})
    set(GAME_COMPILE_DEFS ${GAME_COMPILE_DEFS} TEST_COUNT=${TEST_COUNT})
  endif()
  set(GAME_COMPILE_DEFS ${GAME_COMPILE_DEFS} SETTINGS_PATH="${RANDO_SAVE_PATH}/randomizer_settings.yaml.test" PREFERENCES_PATH="${RANDO_SAVE_PATH}/randomizer_preferences.yaml.test")
else()
  set(GAME_COMPILE_DEFS ${GAME_COMPILE_DEFS} SETTINGS_PATH="${RANDO_SAVE_PATH}/randomizer_settings.yaml" PREFERENCES_PATH="${RANDO_SAVE_PATH}/randomizer_preferences.yaml")
endif()

message(STATUS "randomizer: Fetching yaml-cpp")
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG yaml-cpp-0.9.0
)
message(STATUS "randomizer: Fetching base64pp")
FetchContent_Declare(
        base64pp
        GIT_REPOSITORY https://github.com/matheusgomes28/base64pp.git
        GIT_TAG v0.2.0-rc0
)

message(STATUS "randomizer: Fetching battery-embed")
FetchContent_Declare(
        battery-embed
        GIT_REPOSITORY https://github.com/batterycenter/embed.git
        GIT_TAG        fdbae3f
)

message(STATUS "randomizer: Fetching websocketpp")
FetchContent_Declare(
        websocketpp
        GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
        GIT_TAG        0.8.2
)

message(STATUS "randomizer: Fetching asio")
FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG        asio-1-30-2
)

message(STATUS "randomizer: Fetching wswrap")
FetchContent_Declare(
        wswrap
        GIT_REPOSITORY https://github.com/black-sliver/wswrap.git
        GIT_TAG        aeba7ac428028723fb26ce92488f260660f786b1
)

message(STATUS "randomizer: Fetching nlohmann_json")
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
)

message(STATUS "randomizer: Fetching apclientpp")
FetchContent_Declare(
        apclientpp
        GIT_REPOSITORY https://github.com/black-sliver/apclientpp.git
        GIT_TAG        79621690a3e845645f43888b0fe234a99c74892e
)

FetchContent_MakeAvailable(yaml-cpp base64pp battery-embed)

FetchContent_Populate(websocketpp)
FetchContent_Populate(asio)
FetchContent_Populate(wswrap)
FetchContent_Populate(nlohmann_json)
FetchContent_Populate(apclientpp)

add_library(websocketpp INTERFACE)
target_include_directories(websocketpp SYSTEM INTERFACE ${websocketpp_SOURCE_DIR})
target_compile_definitions(websocketpp INTERFACE _WEBSOCKETPP_CPP11_STL_)

add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

add_library(wswrap INTERFACE)
target_include_directories(wswrap SYSTEM INTERFACE ${wswrap_SOURCE_DIR}/include)

add_library(apclientpp INTERFACE)
target_include_directories(apclientpp SYSTEM INTERFACE
        ${apclientpp_SOURCE_DIR}
        ${nlohmann_json_SOURCE_DIR}/include)
target_compile_definitions(apclientpp INTERFACE AP_NO_SCHEMA)
target_link_libraries(apclientpp INTERFACE websocketpp asio wswrap)

find_package(OpenSSL REQUIRED)

string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)
set(GAME_COMPILE_DEFS ${GAME_COMPILE_DEFS} SOURCE_PATH_SIZE=${SOURCE_PATH_SIZE})
set(GAME_LIBS ${GAME_LIBS} yaml-cpp::yaml-cpp base64pp apclientpp OpenSSL::SSL OpenSSL::Crypto)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/randomizer")
# Put data files together for easier manipulation
# file(COPY "${CMAKE_SOURCE_DIR}/src/dusk/randomizer/data/" DESTINATION "${CMAKE_BINARY_DIR}/randomizer/data/" REGEX "^.*example.*$" EXCLUDE) # World, macros, and location info