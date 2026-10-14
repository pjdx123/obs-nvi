cmake_minimum_required(VERSION 3.16)

project(example)

if (${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../cmake")
    find_package(NVI CONFIG REQUIRED)
endif()

add_executable(SendVideo "${CMAKE_CURRENT_SOURCE_DIR}/SendVideo.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/SendVideo.cpp")
target_link_libraries(SendVideo PRIVATE NVI)
set_target_properties(SendVideo PROPERTIES FOLDER "Example")

add_executable(SendEncoded "${CMAKE_CURRENT_SOURCE_DIR}/SendEncoded.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/SendEncoded.cpp")
target_link_libraries(SendEncoded PRIVATE NVI)
set_target_properties(SendEncoded PROPERTIES FOLDER "Example")

add_executable(SendYUV422HC "${CMAKE_CURRENT_SOURCE_DIR}/SendYUV422HC.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/SendYUV422HC.cpp")
target_link_libraries(SendYUV422HC PRIVATE NVI)
set_target_properties(SendYUV422HC PROPERTIES FOLDER "Example")

add_executable(RecvVideo "${CMAKE_CURRENT_SOURCE_DIR}/RecvVideo.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/RecvVideo.cpp")
target_link_libraries(RecvVideo PRIVATE NVI)
set_target_properties(RecvVideo PROPERTIES FOLDER "Example")

add_executable(RecvEncoded "${CMAKE_CURRENT_SOURCE_DIR}/RecvEncoded.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/RecvEncoded.cpp")
target_link_libraries(RecvEncoded PRIVATE NVI)
set_target_properties(RecvEncoded PROPERTIES FOLDER "Example")

add_executable(PTZController "${CMAKE_CURRENT_SOURCE_DIR}/PTZController.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/PTZController.cpp")
target_link_libraries(PTZController PRIVATE NVI)
set_target_properties(PTZController PROPERTIES FOLDER "Example")

find_package(nlohmann_json CONFIG QUIET)
add_executable(PTZHandler "${CMAKE_CURRENT_SOURCE_DIR}/PTZHandler.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/PTZHandler.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/rapidxml/rapidxml.hpp")
target_link_libraries(PTZHandler PRIVATE NVI)
if (TARGET nlohmann_json)
    target_link_libraries(PTZHandler PRIVATE nlohmann_json)
    target_compile_definitions(PTZHandler PRIVATE _nlohmann_json)
endif()
set_target_properties(PTZHandler PROPERTIES FOLDER "Example")

add_executable(SendYUV422HF "${CMAKE_CURRENT_SOURCE_DIR}/SendYUV422HF.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/SendYUV422HF.cpp")
target_link_libraries(SendYUV422HF PRIVATE NVI)
set_target_properties(SendYUV422HF PROPERTIES FOLDER "Example")

add_executable(SendROI "${CMAKE_CURRENT_SOURCE_DIR}/SendROI.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/SendROI.cpp")
target_link_libraries(SendROI PRIVATE NVI)
set_target_properties(SendROI PROPERTIES FOLDER "Example")

add_executable(SendHDR "${CMAKE_CURRENT_SOURCE_DIR}/SendHDR.cpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES "${CMAKE_CURRENT_SOURCE_DIR}/SendHDR.cpp")
target_link_libraries(SendHDR PRIVATE NVI)
set_target_properties(SendHDR PROPERTIES FOLDER "Example")
