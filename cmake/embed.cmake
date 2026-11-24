# Converts a binary file into a C++ source with a byte array.
# Usage: cmake -DINPUT=file -DOUTPUT=out.cpp -DNAME=symbol -P embed.cmake
file(READ "${INPUT}" HEX_CONTENT HEX)
string(LENGTH "${HEX_CONTENT}" HEX_LENGTH)
math(EXPR BYTE_COUNT "${HEX_LENGTH} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ARRAY_CONTENT "${HEX_CONTENT}")
file(WRITE "${OUTPUT}" "// Generated from ${INPUT}. Do not edit.\n#include <cstddef>\n#include <cstdint>\nextern const uint8_t ${NAME}[] = {${ARRAY_CONTENT}};\nextern const size_t ${NAME}_size = ${BYTE_COUNT};\n")
