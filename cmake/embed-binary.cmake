# Turns a binary file into a C++ source that defines its bytes as an array, so the file is compiled
# into the library instead of shipped beside it. Script mode:
#
#   cmake -DINPUT=<file> -DOUTPUT=<source> -DNAMESPACE=<namespace> -DSYMBOL=<name> -P embed-binary.cmake
#
# The source defines <namespace>::<SYMBOL>[] and <namespace>::<SYMBOL>_SIZE; declare both extern
# where they are used.

foreach(required INPUT OUTPUT NAMESPACE SYMBOL)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "embed-binary.cmake: ${required} is not set")
  endif()
endforeach()

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hexLength)
math(EXPR byteCount "${hexLength} / 2")

# one "0x..," per byte, broken into lines of 16 so the generated source stays workable in an editor
# (CMake's regex has no {n} repeat, hence the pattern spelled out by string(REPEAT))
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REPEAT "0x[0-9a-f][0-9a-f]," 16 linePattern)
string(REGEX REPLACE "(${linePattern})" "\\1\n" bytes "${bytes}")

get_filename_component(inputName "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
  "// Generated from ${inputName} by cmake/embed-binary.cmake - do not edit.\n"
  "#include <cstddef>\n"
  "#include <cstdint>\n"
  "\n"
  "namespace ${NAMESPACE}\n"
  "{\n"
  "    extern const std::uint8_t ${SYMBOL}[];\n"
  "    extern const std::size_t ${SYMBOL}_SIZE;\n"
  "\n"
  "    const std::uint8_t ${SYMBOL}[] = {\n"
  "${bytes}\n"
  "    };\n"
  "    const std::size_t ${SYMBOL}_SIZE = ${byteCount};\n"
  "}\n"
)
