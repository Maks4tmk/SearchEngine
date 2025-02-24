# CMakeList.txt: проект CMake для SearchEngine; включите исходный код и определения,
# укажите здесь логику для конкретного проекта.
#
cmake_minimum_required(VERSION 3.20)
project(SearchEngine LANGUAGES CXX)

cmake_policy(SET CMP0144 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Zi")

set(BOOST_ROOT "D:/Work Folder/visualC++/boost")
set(BOOST_INCLUDEDIR "D:/Work Folder/visualC++/boost/include/boost-1_87")

find_package(Boost REQUIRED COMPONENTS system filesystem locale url)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found!")
endif()

set(PostgreSQL_ROOT "C:/Program Files/PostgreSQL/17")
find_package(PostgreSQL REQUIRED)
if(NOT PostgreSQL_FOUND)
    message(FATAL_ERROR "PostgreSQL not found!")
endif()

add_subdirectory("D:/Work Folder/visualC++/libpqxx" libpqxx-build)

add_executable(SearchEngine main.cpp spider.hpp spider.cpp search.hpp search.cpp)

configure_file(${CMAKE_SOURCE_DIR}/config.ini ${CMAKE_BINARY_DIR} COPYONLY)

target_include_directories(SearchEngine PRIVATE 
    ${Boost_INCLUDE_DIRS} 
    ${PostgreSQL_INCLUDE_DIRS}
)
target_link_libraries(SearchEngine PRIVATE 
    ${Boost_LIBRARIES}
    ${PostgreSQL_LIBRARIES} 
    pqxx
)


