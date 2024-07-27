cmake_minimum_required(VERSION 3.10)
project(cpp_beast)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic ")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mtune=native -Ofast ")

find_package(Boost 1.84.0 CONFIG REQUIRED COMPONENTS system)
link_libraries(Boost::system)
add_executable(cpp_beast main.cpp)

target_link_libraries(cpp_beast uring)
target_compile_definitions(cpp_beast PRIVATE
    BOOST_BEAST_USE_STD_STRING_VIEW=1
    BOOST_ASIO_NO_DEPRECATED=1
    #TRUE_HTTP=1
    #CACHED_RESPONSE=1
)
