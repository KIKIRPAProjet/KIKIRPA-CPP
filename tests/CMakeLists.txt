add_executable(validator-tests validator_tests.cpp)
target_link_libraries(validator-tests PRIVATE pipeline_validation)
add_test(NAME validator-tests COMMAND validator-tests)
add_test(NAME cli-valid COMMAND pipeline-validator validate ${PROJECT_SOURCE_DIR}/examples/valid.json)
add_test(NAME cli-invalid COMMAND pipeline-validator validate ${PROJECT_SOURCE_DIR}/examples/missing-dependency.json)
set_tests_properties(cli-invalid PROPERTIES WILL_FAIL TRUE)
add_test(NAME integration-example COMMAND integration-example)

add_executable(json-scenario-checker json_scenarios.cpp)
target_link_libraries(json-scenario-checker PRIVATE pipeline_validation)
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/reports")

# Chaque fichier numéroté est un scénario ; le manifeste définit les attentes.
file(GLOB scenarios CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/json/[0-9]*.json")
foreach(input IN LISTS scenarios)
  get_filename_component(name "${input}" NAME_WE)
  get_filename_component(filename "${input}" NAME)
  add_test(NAME json-${name}
    COMMAND ${CMAKE_COMMAND}
      "-DVALIDATOR=$<TARGET_FILE:pipeline-validator>"
      "-DCHECKER=$<TARGET_FILE:json-scenario-checker>"
      "-DINPUT=${input}"
      "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/json/manifest.json"
      "-DREPORT=${PROJECT_BINARY_DIR}/reports/${name}.json"
      "-DCASE=${filename}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_json.cmake")
endforeach()
