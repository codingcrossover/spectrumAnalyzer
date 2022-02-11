cmake_minimum_required(VERSION 2.8.11)

project(terminalDancer CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "/System/Volumes/Data/opt/homebrew/opt/pulseaudio/lib/cmake/PulseAudio")

message(STATUS "cmake compiler id is '${CMAKE_CXX_COMPILER_ID}'")
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    message(STATUS "cmake compiler id is clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g3")
    if(APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
endif()

find_package(PulseAudio)

include_directories(${PULSEAUDIO_INCLUDE_DIR}
                        "/opt/aquila/include")
message(STATUS "pulse audio libs: ${PULSEAUDIO_LIBRARY}")


add_executable(spectrumAnalyzer analyzer.cpp)


target_link_libraries(spectrumAnalyzer ${PULSEAUDIO_LIBRARY} "/opt/homebrew/Cellar/pulseaudio/14.2/lib/libpulse-simple.dylib" 
                                                            "/opt/aquila/lib/libAquila.a"
                                                            "/opt/aquila/lib/libOoura_fft.a"
                                                            )
