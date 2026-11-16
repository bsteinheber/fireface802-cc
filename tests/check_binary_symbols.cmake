# Check the dynamic symbols a built binary imports.
#
# Fails if BINARY references any symbol listed in FORBIDDEN, or does not
# reference a symbol listed in REQUIRED. Lists are separated by '|'.
# This verifies the actual linked program, independent of the sources.
#
# Usage: cmake -DNM=<nm> -DBINARY=<file> -DFORBIDDEN=a|b -DREQUIRED=c|d
#              -P check_binary_symbols.cmake

if(NOT NM OR NOT BINARY)
    message(FATAL_ERROR "NM and BINARY must be set")
endif()

execute_process(
    COMMAND "${NM}" -D --undefined-only "${BINARY}"
    OUTPUT_VARIABLE out
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "nm failed on ${BINARY}")
endif()

# Collect imported symbol names (strip version suffix like @LIBUSB_1.0).
set(symbols "")
string(REPLACE "\n" ";" lines "${out}")
foreach(line IN LISTS lines)
    if(line MATCHES "U[ \t]+([A-Za-z0-9_]+)")
        list(APPEND symbols "${CMAKE_MATCH_1}")
    endif()
endforeach()

set(failed 0)
string(REPLACE "|" ";" forbidden "${FORBIDDEN}")
foreach(sym IN LISTS forbidden)
    list(FIND symbols "${sym}" idx)
    if(NOT idx EQUAL -1)
        message(SEND_ERROR "${BINARY} imports forbidden symbol ${sym}")
        set(failed 1)
    endif()
endforeach()

string(REPLACE "|" ";" required "${REQUIRED}")
foreach(sym IN LISTS required)
    list(FIND symbols "${sym}" idx)
    if(idx EQUAL -1)
        message(SEND_ERROR "${BINARY} does not import expected symbol ${sym}")
        set(failed 1)
    endif()
endforeach()

if(failed)
    message(FATAL_ERROR "symbol check failed for ${BINARY}")
endif()
list(LENGTH symbols n)
message(STATUS "${BINARY}: ${n} imported symbols checked")
