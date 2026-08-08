# Decompiler/Util primitives (UTF-8/16, the C++17 Span polyfill, ImmutableStack).
# Headers are reachable as "Decompiler/Util/..." via the root include directory
# on the ilspy target; only the .cpp that has out-of-line code is listed here.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Utf.cpp
)
