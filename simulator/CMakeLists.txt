cmake_minimum_required(VERSION 3.14)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

message(WARNING "Cmake build type is ${CMAKE_BUILD_TYPE}")

project(ems-simulator C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(LibOSRM REQUIRED)

link_directories(${LibOSRM_LIBRARY_DIRS})
include_directories(${LibOSRM_INCLUDE_DIRS})

include(FetchContent)

# The simulator framework
FetchContent_Declare(simcpp20
    GIT_REPOSITORY https://github.com/liuq/simcpp20
    GIT_TAG        5cbd2e4409399a08b6edaf976afc2ca070684788) # replace with latest revision

FetchContent_MakeAvailable(simcpp20)

# Logging facilities
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog
    GIT_TAG        ad0e89cbfb4d0c1ce4d097e134eb7be67baebb36) # replace with latest revision

FetchContent_MakeAvailable(spdlog)

# Progress bar indication facilities
FetchContent_Declare(indicators
    GIT_REPOSITORY https://github.com/p-ranav/indicators
    GIT_TAG        6432875ef4c866d25e324c19a787b5bb90b0ab12) # replace with latest rev

FetchContent_MakeAvailable(indicators)

# Color console
FetchContent_Declare(termcolor
    GIT_REPOSITORY https://github.com/ikalnytskyi/termcolor
    GIT_TAG 67eb0aa55e48ead9fe2aab049f0b1aa7943ba0ea) # replace with latest rev
    
FetchContent_MakeAvailable(termcolor)

# Range v3
FetchContent_Declare(range-v3
    GIT_REPOSITORY https://github.com/ericniebler/range-v3.git
    GIT_TAG 0fa54d7de5dc00655e38a08e87cda61f7aa6d5b9) # replace with latest rev
    
FetchContent_MakeAvailable(range-v3)

# SQLiteCpp
FetchContent_Declare(SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG b85df14905dc123a2f8c72a24648c851e4001fb5) # replace with latest rev
    
FetchContent_MakeAvailable(SQLiteCpp)

# Datetime and program option management
find_package(Boost 1.71 REQUIRED COMPONENTS date_time program_options)

include_directories(SYSTEM ${LibOSRM_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
add_executable(app app.cpp helpers.cpp routing.cpp emergency.cpp ambulance.cpp hospital.cpp dispatcher.cpp data.hpp emergency.hpp ambulance.hpp hospital.hpp dispatcher.hpp helpers.hpp routing.hpp)
target_link_libraries(app PRIVATE simcpp20 boost_date_time boost_program_options spdlog indicators termcolor range-v3 SQLiteCpp ${LibOSRM_LIBRARIES} ${LibOSRM_DEPENDENT_LIBRARIES})
target_compile_features(app PRIVATE cxx_std_20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LibOSRM_CXXFLAGS}")

install(TARGETS app
        CONFIGURATIONS Release
        RUNTIME DESTINATION bin)
