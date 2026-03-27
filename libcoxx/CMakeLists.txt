# libcoxx - C++17 wrapper for libco

add_library(coxx STATIC
    src/scheduler.cpp
    src/sync.cpp
)

target_include_directories(coxx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(coxx PUBLIC co)
target_compile_features(coxx PUBLIC cxx_std_17)
