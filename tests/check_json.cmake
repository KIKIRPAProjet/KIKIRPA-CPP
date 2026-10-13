# Exécuter la commande réelle et vérifier le statut, les codes et, si prévu, l’ordre exact.
execute_process(COMMAND "${VALIDATOR}" validate "${INPUT}" RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE stderr)
file(WRITE "${REPORT}" "${report}")
# Un programme C++ vérifie le contenu du rapport JSON : CMake 3.16 ne propose pas d’API JSON.
execute_process(COMMAND "${CHECKER}" "${MANIFEST}" "${CASE}" "${REPORT}" "${status}" RESULT_VARIABLE checked OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT checked EQUAL 0)
  message(FATAL_ERROR "${output}${error}")
endif()
