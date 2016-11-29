cmake_minimum_required (VERSION 3.1.0 FATAL_ERROR)
project (CIS505Final C CXX)

find_package (Threads)

# Set the C++ standard to 11 for all the goodies
set(CMAKE_CXX_STANDARD 11)
set(PROJECT_SRC ${CMAKE_SOURCE_DIR}/../src)
set(BUILD_DIR   ${CMAKE_SOURCE_DIR})
set(PROTOC_BIN  ${BUILD_DIR}/protobuf-build/bin/protoc)

message("Using protoc at: " ${PROTOC_BIN})

# Include all the source code directories
include_directories(${PROJECT_SRC}/http)
include_directories(${PROJECT_SRC}/kv)
include_directories(${PROJECT_SRC}/storage)
include_directories(${PROJECT_SRC}/webmail)
include_directories(${PROJECT_SRC}/lib)
include_directories(${BUILD_DIR})
include_directories(${BUILD_DIR}/protobuf-build/include)

# Set the source directories
file(GLOB HTTP_SRC    ${PROJECT_SRC}/http/*.cpp)
file(GLOB KV_SRC      ${PROJECT_SRC}/kv/*.cpp)
file(GLOB STORAGE_SRC ${PROJECT_SRC}/storage/*.cpp)
file(GLOB MAIL_SRC    ${PROJECT_SRC}/webmail/*.cpp)

# Glob all the .proto files
file(GLOB PROTO_SRCS  ${PROJECT_SRC}/**/*.proto)

set(PROTO_COMPILED_HS)
set(PROTO_COMPILED_CCS)

foreach (_file ${PROTO_SRCS})
  get_filename_component(_filename ${_file} NAME_WE)
  set(_pb_h_out  "${BUILD_DIR}/${_filename}.pb.h")
  set(_pb_cc_out "${BUILD_DIR}/${_filename}.pb.cc")

  add_custom_command(OUTPUT  ${_pb_h_out} ${_pb_cc_out}
  	             DEPENDS ${_file}
		     COMMAND "${PROTOC_BIN}"
		     ARGS    -I${PROJECT_SRC}/http
		             -I${PROJECT_SRC}/kv
			     -I${PROJECT_SRC}/storage
			     -I${PROJECT_SRC}/webmail
			     -I${PROJECT_SRC}/lib
			     --cpp_out=${BUILD_DIR} ${_file})

  list(APPEND PROTO_COMPILED_HS  ${_pb_h_out})
  list(APPEND PROTO_COMPILED_CCS ${_pb_cc_out})
endforeach()

add_custom_target(PROTO_SRCS
                  DEPENDS ${PROTO_COMPILED_HS} ${PROTO_COMPILED_CCS})

# Set the source for common lib
file(GLOB LIB_SRC     ${PROJECT_SRC}/lib/*.cpp)

# Add the infrastructure lib as a target
add_library(infra STATIC ${LIB_SRC})

# Add the message lib as a target
add_library(message STATIC ${PROTO_COMPILED_CCS})

# Find the protobuf runtime library
find_library(PROTOBUF
             NAMES "libprotobuf.a"
	     HINTS "${BUILD_DIR}/protobuf-build/lib"
	     ONLY_CMAKE_FIND_ROOT_PATH)

# Add the executable targets
add_executable(http    ${HTTP_SRC})
add_executable(kv      ${KV_SRC})
add_executable(storage ${STORAGE_SRC})
add_executable(mail    ${MAIL_SRC})

# Link the executables with the infra library
target_link_libraries(http    infra ${PROTOBUF} message ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(kv      infra ${PROTOBUF} message ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(storage infra ${PROTOBUF} message ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(mail    infra ${PROTOBUF} message ${CMAKE_THREAD_LIBS_INIT})

# Add dependency to protoc generated files
add_dependencies(infra   PROTO_SRCS)
add_dependencies(http    PROTO_SRCS)
add_dependencies(kv      PROTO_SRCS)
add_dependencies(storage PROTO_SRCS)
add_dependencies(mail    PROTO_SRCS)
