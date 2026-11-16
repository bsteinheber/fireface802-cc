# Run a command and check its exact exit code and (optionally) output.
# stdin is always /dev/null, i.e. never an interactive terminal.
#
# Usage: cmake -DCMD=<prog> "-DARGS=a|b" -DEXPECT_RC=<n>
#              [-DEXPECT_REGEX=<regex>] -P run_expect.cmake

if(NOT CMD OR NOT DEFINED EXPECT_RC)
    message(FATAL_ERROR "CMD and EXPECT_RC must be set")
endif()

string(REPLACE "|" ";" args "${ARGS}")
execute_process(
    COMMAND "${CMD}" ${args}
    INPUT_FILE /dev/null
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)

if(NOT rc STREQUAL "${EXPECT_RC}")
    message(FATAL_ERROR "exit code ${rc}, expected ${EXPECT_RC}\n"
                        "stdout:\n${out}\nstderr:\n${err}")
endif()
if(DEFINED EXPECT_REGEX AND NOT "${out}${err}" MATCHES "${EXPECT_REGEX}")
    message(FATAL_ERROR "output does not match '${EXPECT_REGEX}'\n"
                        "stdout:\n${out}\nstderr:\n${err}")
endif()
