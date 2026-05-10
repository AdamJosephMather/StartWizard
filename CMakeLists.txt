cmake_minimum_required(VERSION 3.10)
project(StartWizard)

# debug...

# if (MSVC)
# 	message(STATUS "Building with MSVC AddressSanitizer enabled")
# 
# 	# Let CMake pick the correct /MDd or /MD (recommended for ASan on MSVC)
# 	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")
# 
# 	add_compile_options(
# 		/fsanitize=address
# 		/Zi
# 	)
# 
# 	add_link_options(
# 		/fsanitize=address
# 		/INCREMENTAL:NO
# 		/DEBUG:FULL
# 		# REMOVE this line:
# 		# /WHOLEARCHIVE:clang_rt.asan-x86_64.lib
# 	)
# endif()




set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
	add_compile_options(/wd4244 /wd4305 /wd4267)
endif()

include(FetchContent)

add_definitions(-DNOMINMAX)
# Windows needs these for ConPTY APIs
add_definitions(-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000008 -DUNICODE -D_UNICODE)


add_executable(${PROJECT_NAME} startwizard.cpp text_renderer.cpp )

# Use vcpkg-provided GLFW and ICU
find_package(glfw3 CONFIG REQUIRED)
find_package(ICU   REQUIRED COMPONENTS uc i18n data)  # must list each component :contentReference[oaicite:0]{index=0}
find_package(Freetype REQUIRED)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	glfw
	opengl32
	ICU::uc            # core Unicode (formerly icuuc) :contentReference[oaicite:1]{index=1}
	ICU::i18n          # internationalization (formerly icui18n)
	ICU::data          # ICU data blobs (formerly icudata)
	Freetype::Freetype
)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	
	ole32
	shell32
	strsafe
	dwmapi
)

if (MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()

if(MSVC AND WIN32)
	set(VC_REDIST_EXE "${CMAKE_SOURCE_DIR}/build_needs/vc_redist.x64.exe")

	if(NOT EXISTS "${VC_REDIST_EXE}")
		message(FATAL_ERROR "vc_redist not found at: ${VC_REDIST_EXE}")
	endif()

	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
						"${VC_REDIST_EXE}"
						"$<TARGET_FILE_DIR:${PROJECT_NAME}>/vc_redist.x64.exe"
		COMMENT "Copying VC++ Redistributable to output folder"
		VERBATIM
	)
endif()

set(CASCADIA_TTF "${CMAKE_SOURCE_DIR}/build_needs/CascadiaCode-Regular.ttf")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${CASCADIA_TTF}"
					"$<TARGET_FILE_DIR:${PROJECT_NAME}>/CascadiaCode-Regular.ttf"
	COMMENT "Copying Cascadia TTF to output folder"
	VERBATIM
)

# if (MSVC)
# 	set_target_properties(${PROJECT_NAME} PROPERTIES
# 		WIN32_EXECUTABLE TRUE
# 	)
# endif()