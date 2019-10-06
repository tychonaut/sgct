# Copyright Linkoping University 2011
# SGCT Project Authors see Authors.txt

cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(SGCT_Examples)

option(SGCT_EXAMPLES_AL "Build OpenAL examples" OFF)
option(SGCT_EXAMPLES_FMOD "Build FMOD examples" OFF)
option(SGCT_EXAMPLES_FFMPEG "Build FFmpeg examples" OFF)
option(SGCT_EXAMPLES_NDI "Build NDI examples" OFF)
option(SGCT_EXAMPLES_IMGUI "Build imgui examples" OFF)
option(SGCT_EXAMPLES_OPENVR "Build OpenVR examples" OFF)
option(SGCT_EXAMPLES_OSG "Build OSG examples" OFF)
option(SGCT_EXAMPLES_RGBEASY "Build RGBEasy examples" OFF)
option(SGCT_EXAMPLES_WEBSOCKETS "Build Websockets examples" OFF)
#Spout doesn't support MinGW
if(MSVC)
	option(SGCT_EXAMPLES_SPOUT "Build Spout examples" OFF)
	option(SGCT_EXAMPLES_STITCHER "Build the Frame Stitcther" OFF)
endif()

if (MSVC)
	option(USE_MSVC_RUNTIMES "To use MSVC DLLs or to create a static build" ON)
endif()

if( APPLE )
	option(SGCT_CPP11 "Use libc++ instead of libstdc++" ON)
	set(CMAKE_OSX_ARCHITECTURES "x86_64")
	if(CMAKE_GENERATOR STREQUAL Xcode)
		set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9")
	endif()
endif()

#force sgct to build
link_libraries(${LIB_NAME})


# otherwise copy of resources doesn't work
set(SGCT_EXAMPLE_OUTPUT_DIR ${PROJECT_BINARY_DIR})
#message(PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR})
message(SGCT_EXAMPLE_OUTPUT_DIR: ${SGCT_EXAMPLE_OUTPUT_DIR})



#copy SharedResources
if(SGCT_COPY_EXAMPLE_RESOURCES AND NOT SGCT_PLACE_TARGETS_IN_SOURCE_TREE )
	file(GLOB sharedResFiles
		"${PROJECT_SOURCE_DIR}/SharedResources/*.*")
	file(COPY ${sharedResFiles} DESTINATION ${SGCT_EXAMPLE_OUTPUT_DIR}/SharedResources)
endif() #copy data


#HACK because glfw headers aren't found in windows otherwise (at least with VS2019)
if(NOT DEFINED SGCT_INCLUDE_DIRECTORY)
	if( WIN32 )
		find_path(SGCT_INCLUDE_DIRECTORY 
			NAMES sgct
			PATHS $ENV{SGCT_ROOT_DIR}/include NO_DEFAULT_PATH
			REQUIRED)
	else()
		find_path(SGCT_INCLUDE_DIRECTORY 
			NAMES sgct
			PATH_SUFFIXES sgct
			PATHS $ENV{SGCT_ROOT_DIR}/include
			REQUIRED)
	endif()
endif()
include_directories(${SGCT_INCLUDE_DIRECTORY} ${SGCT_INCLUDE_DIRECTORY}/../src/deps/glfw/include)
#end hack



	
#HACK; hardcode to find library binary; TODO make this more general
if(NOT DEFINED SGCT_RELEASE_LIBRARY)
	if(SGCT_CPP11)	
		find_library(SGCT_RELEASE_LIBRARY
			PATHS $ENV{SGCT_ROOT_DIR}/buildOut/lib/msvc16
			NAMES sgct_cpp11 libsgct_cpp11 REQUIRED)
	else()
		find_library(SGCT_RELEASE_LIBRARY
			PATHS $ENV{SGCT_ROOT_DIR}/buildOut/lib/msvc16
			NAMES sgct libsgct REQUIRED)
	endif()
endif()
if(NOT DEFINED SGCT_DEBUG_LIBRARY)		
	if(SGCT_CPP11)	
		find_library(SGCT_DEBUG_LIBRARY 
			PATHS $ENV{SGCT_ROOT_DIR}/buildOut/lib/msvc16
			NAMES sgct_cpp11d libsgct_cpp11d REQUIRED)
	else()
		find_library(SGCT_DEBUG_LIBRARY 
			PATHS $ENV{SGCT_ROOT_DIR}/buildOut/lib/msvc16
			NAMES sgctd libsgctd REQUIRED)
	endif()
endif()
#end hack


add_subdirectory(calibrator)
add_subdirectory(clustertest)
add_subdirectory(dataTransfer_opengl3)
add_subdirectory(domeImageViewer_opengl3)
add_subdirectory(depthBuffer)
add_subdirectory(example1)
add_subdirectory(example1_opengl3)
add_subdirectory(gamepadExample)
add_subdirectory(heightMappingExample)
add_subdirectory(heightMappingExample_opengl3)
if(SGCT_EXAMPLES_IMGUI)
	add_subdirectory(imguiExample)
endif()
add_subdirectory(kinectExample)
add_subdirectory(MRTExample)
add_subdirectory(MRTExample_opengl3)
add_subdirectory(model_loader)
add_subdirectory(model_loader_opengl3)
if(SGCT_EXAMPLES_OPENVR)
add_subdirectory(openvrExample)
add_subdirectory(openvrFullDomeViewer)
endif()
if(SGCT_EXAMPLES_OSG)
	add_subdirectory(osgExample)
	add_subdirectory(osgExampleRTT)
endif()
add_subdirectory(postFXExample)
add_subdirectory(postFXExample_opengl3)
add_subdirectory(renderToTexture)
add_subdirectory(sgct_template)
add_subdirectory(SGCTRemote)
add_subdirectory(simpleNavigationExample)
add_subdirectory(simpleNavigationExample_opengl3)
add_subdirectory(simpleShaderExample)
add_subdirectory(simpleShaderExample_opengl3)
if(SGCT_EXAMPLES_AL)
	add_subdirectory(soundExample)
endif()
if(SGCT_EXAMPLES_STITCHER)
	add_subdirectory(stitcher)
endif()
add_subdirectory(textureExample)
add_subdirectory(textureExample_opengl3)
add_subdirectory(OmniStereoTest_opengl3)
add_subdirectory(touchExample)
add_subdirectory(trackingExample)
if(SGCT_EXAMPLES_FMOD)
	add_subdirectory(fmodExample_opengl3)
endif()
add_subdirectory(networkExample_opengl3)

if(SGCT_EXAMPLES_SPOUT)
	SET(SPOUT_ENABLED "1" PARENT_SCOPE)
	add_subdirectory(SpoutExample)
	add_subdirectory(SpoutSenderExample)
endif()

if(SGCT_EXAMPLES_FFMPEG)
	add_subdirectory(FFmpegCaptureAndDomeImageViewer)
	add_subdirectory(FFmpegCaptureExample)
endif()

if(SGCT_EXAMPLES_RGBEASY)
	add_subdirectory(RGBEasyCaptureExample)
endif()

if(SGCT_EXAMPLES_NDI)
	add_subdirectory(heightMappingNDISenderExample_opengl3)
endif()

if(SGCT_EXAMPLES_WEBSOCKETS)
	SET(WEBSOCKETS_ENABLED "1" PARENT_SCOPE)
	add_subdirectory(websocketsExample)
endif()

if( WIN32 )
	add_definitions(-D__WIN32__)
	if( MINGW )
		set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
	endif()
elseif( APPLE  )
	add_definitions(-D__APPLE__)
	if(SGCT_CPP11)
		set(CMAKE_CXX_FLAGS "-std=c++11 -stdlib=libc++ ${CMAKE_CXX_FLAGS}")
	else()
		set(CMAKE_CXX_FLAGS "-std=c++0x -stdlib=libstdc++ ${CMAKE_CXX_FLAGS}")
	endif()
else()
	add_definitions(-D__LINUX__)
	set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
endif()
	
#if( CMAKE_COMPILER_IS_GNUCXX )
#	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
#endif()
