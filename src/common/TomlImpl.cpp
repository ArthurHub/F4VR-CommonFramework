// The implementation of toml++, compiled once into the framework. The framework and every mod that links it include
// its declarations only (TOML_HEADER_ONLY=0, see CMakeLists.txt). Built without the PCH, so nothing includes toml++
// before TOML_IMPLEMENTATION is defined.
#define TOML_IMPLEMENTATION
#include <toml++/toml.hpp>
