cmake_minimum_required (VERSION 3.1.0 FATAL_ERROR)
project (CIS505Final CXX)

# Set the C++ standard to 11 for all the goodies
set(CMAKE_CXX_STANDARD 11)
set(PROJECT_SRC ../src)

# Include all the source code directories
include_directories(${PROJECT_SRC}/http)
include_directories(${PROJECT_SRC}/kv)
include_directories(${PROJECT_SRC}/storage)
include_directories(${PROJECT_SRC}/webmail)
include_directories(${PROJECT_SRC}/lib)

# Set the source directories
file(GLOB HTTP_SRC    ${PROJECT_SRC}/http/*.cpp)
file(GLOB KV_SRC      ${PROJECT_SRC}/kv/*.cpp)
file(GLOB STORAGE_SRC ${PROJECT_SRC}/storage/*.cpp)
file(GLOB MAIL_SRC    ${PROJECT_SRC}/webmail/*.cpp)

# Set the source for common lib
file(GLOB LIB_SRC     ${PROJECT_SRC}/lib/*.cpp)

# Add the infrastructure lib as a target
add_library(infra STATIC ${LIB_SRC})

# Add the executable targets
add_executable(http    ${HTTP_SRC})
add_executable(kv      ${KV_SRC})
add_executable(storage ${STORAGE_SRC})
add_executable(mail    ${MAIL_SRC})

# Link the executables with the infra library
target_link_libraries(http    infra)
target_link_libraries(kv      infra)
target_link_libraries(storage infra)
target_link_libraries(mail    infra)
