cmake_minimum_required(VERSION 3.16)
project(pipeline_validator VERSION 1.0.0 LANGUAGES CXX)

add_library(pipeline_validation
  src/validation_report.cpp
  src/step_config_validator.cpp
  src/step_registry.cpp
  src/dependency_resolver.cpp
  src/pipeline_schema.cpp
  src/pipeline_rules.cpp
  src/pipeline_validator.cpp
  src/pipeline_io.cpp
  src/stitching.cpp
  src/ordered_json.cpp
  src/ordered_options.cpp
  src/ordered_stitching.cpp)
target_compile_features(pipeline_validation PUBLIC cxx_std_17)
set_target_properties(pipeline_validation PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(pipeline_validation PUBLIC include)
target_include_directories(pipeline_validation SYSTEM PUBLIC third_party)
if(MSVC)
  target_compile_options(pipeline_validation PRIVATE /W4)
else()
  target_compile_options(pipeline_validation PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pipeline-validator src/main.cpp)
target_link_libraries(pipeline-validator PRIVATE pipeline_validation)
add_executable(integration-example examples/integration.cpp)
target_link_libraries(integration-example PRIVATE pipeline_validation)

# Les tests possèdent leur propre configuration pour alléger celle du projet.
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
