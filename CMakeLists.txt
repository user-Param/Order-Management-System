cmake_minimum_required(VERSION 3.10)
project(oms)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.74 REQUIRED COMPONENTS thread)

# Find libpqxx via pkg-config
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(PQXX libpqxx)
endif()

if(NOT PQXX_FOUND)
    set(PQXX_INCLUDE_DIRS "/opt/homebrew/include")
    set(PQXX_LIBRARY_DIRS "/opt/homebrew/lib")
    set(PQXX_LIBRARIES pqxx)
endif()

add_executable(oms
    main.cpp
    oms/oms.cpp
    riskmanager/riskmanager.cpp
    oadapter/oadapter.cpp
    database/database.cpp
    exchange/src/exchange1.cpp
    exchange/src/exchange2.cpp
    exchange/src/exchange3.cpp
)

target_include_directories(oms PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PQXX_INCLUDE_DIRS}
)

target_link_libraries(oms
    PRIVATE
    Boost::thread
    pthread
    ${PQXX_LIBRARIES}
)

target_link_directories(oms PRIVATE ${PQXX_LIBRARY_DIRS})
