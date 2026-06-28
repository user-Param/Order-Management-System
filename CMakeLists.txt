cmake_minimum_required(VERSION 3.10)
project(oms)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.74 REQUIRED COMPONENTS)


add_executable(oms
    main.cpp 
    oms/oms.cpp
    )



target_link_libraries(oms 
    PRIVATE
    ${Boost_LIBRARIES}
)


target_include_directories(oms PRIVATE 
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    )