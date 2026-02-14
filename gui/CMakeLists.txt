cmake_minimum_required(VERSION 3.10)
project(MyProject VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 (or Qt5 as fallback)
find_package(Qt6 COMPONENTS Core Widgets PrintSupport QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Widgets PrintSupport REQUIRED)
endif()

# Find PortAudio
find_library(PORTAUDIO_LIBRARY portaudio HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/portaudio/lib/.libs
    /usr/local/lib
    /usr/lib
)
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/portaudio/include
    /usr/local/include
    /usr/include
)

# Find FFTW
find_library(FFTW_LIBRARY fftw3 HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/fftw-3.3.10/.libs
    /usr/local/lib
    /usr/lib
)
find_path(FFTW_INCLUDE_DIR fftw3.h HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/fftw-3.3.10/api
    /usr/local/include
    /usr/include
)

# Find ALSA (required for pa_linux_alsa.h)
find_library(ALSA_LIBRARY asound)

# Add executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/app.cpp
    src/qcustomplot.cpp
    include/app.h
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PORTAUDIO_INCLUDE_DIR}
    ${FFTW_INCLUDE_DIR}
)

# Link Qt libraries
if(Qt6_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::PrintSupport)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt5::Core Qt5::Widgets Qt5::PrintSupport)
endif()

# Link PortAudio and FFTW
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${PORTAUDIO_LIBRARY}
    ${FFTW_LIBRARY}
    ${ALSA_LIBRARY}
    pthread
)

# Compiler warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
