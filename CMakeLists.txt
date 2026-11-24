cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" IPDE_FILE_VERSION LIMIT_COUNT 1)
set(IPDE_VERSION "${IPDE_FILE_VERSION}" CACHE STRING "IPDE version (X.Y.Z)")
set(IPDE_CHANNEL "devbuild" CACHE STRING "Release channel: stable or devbuild")
set(IPDE_REPOSITORY "IkerPlays/IPDE" CACHE STRING "GitHub repository used by the updater")

project(IPDE VERSION ${IPDE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Static MSVC runtime: the exe runs without installing anything.
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Embedded resources.
set(IPDE_RESOURCES
	"assets/fonts/Roboto-Medium.ttf|res_font_roboto"
	"assets/logo_tile.png|res_logo_tile"
	"assets/logo.png|res_logo_wordmark"
)
set(IPDE_RESOURCE_SOURCES "")
foreach(entry ${IPDE_RESOURCES})
	string(REPLACE "|" ";" parts "${entry}")
	list(GET parts 0 res_file)
	list(GET parts 1 res_name)
	set(out "${CMAKE_CURRENT_BINARY_DIR}/resources/${res_name}.cpp")
	add_custom_command(
		OUTPUT "${out}"
		COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${res_file} -DOUTPUT=${out} -DNAME=${res_name} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${res_file}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake"
		COMMENT "Embedding ${res_file}")
	list(APPEND IPDE_RESOURCE_SOURCES "${out}")
endforeach()

configure_file(src/core/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

add_library(imgui STATIC
	thirdparty/imgui/imgui.cpp
	thirdparty/imgui/imgui_draw.cpp
	thirdparty/imgui/imgui_tables.cpp
	thirdparty/imgui/imgui_widgets.cpp)
target_include_directories(imgui PUBLIC thirdparty/imgui)

file(GLOB IPDE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/render/*.cpp src/render3d/*.cpp src/app/*.cpp)
if(WIN32)
	list(APPEND IPDE_SOURCES src/platform/platform_win32.cpp assets/ipde.rc)
else()
	list(APPEND IPDE_SOURCES src/platform/platform_x11.cpp)
endif()

add_executable(IPDE WIN32 src/main.cpp ${IPDE_SOURCES} ${IPDE_RESOURCE_SOURCES})
target_include_directories(IPDE PRIVATE src thirdparty "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(IPDE PRIVATE imgui)

if(WIN32)
	target_link_libraries(IPDE PRIVATE winhttp shell32 ole32 shlwapi dwmapi)
	target_compile_definitions(IPDE PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
	find_package(X11 REQUIRED)
	target_link_libraries(IPDE PRIVATE ${X11_LIBRARIES} pthread)
	target_include_directories(IPDE PRIVATE ${X11_INCLUDE_DIR})
endif()

if(MSVC)
	target_compile_options(IPDE PRIVATE /W3 /utf-8)
	target_compile_options(imgui PRIVATE /utf-8)
else()
	target_compile_options(IPDE PRIVATE -Wall -Wno-unused-parameter)
endif()
