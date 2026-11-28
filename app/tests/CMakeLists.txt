# LAN play transport tests. Standalone: no SDK and no game files needed.
#   cmake -S app/tests -B build-lan-tests && cmake --build build-lan-tests
#   build-lan-tests/ht_lan_transport_test
cmake_minimum_required(VERSION 3.25)
project(ht_lan_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
add_executable(ht_lan_transport_test
    ht_lan_transport_test.cpp
    ../src/ht_lan_transport.cpp)
target_link_libraries(ht_lan_transport_test PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(ht_lan_transport_test PRIVATE ws2_32 iphlpapi)
    if(MINGW)
        target_link_options(ht_lan_transport_test PRIVATE -static)
    endif()
endif()
