# Decompiler/Util primitives (UTF-8/16, the C++17 Span polyfill, ImmutableStack,
# LongSet/LongInterval -- the immutable interval-set the switch-family transforms
# need to represent complements of finite value sets).
# Headers are reachable as "Decompiler/Util/..." via the root include directory
# on the ilspy target; only the .cpp that has out-of-line code is listed here.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Utf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CSharpPrimitiveCast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StringComparers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ResourcesFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Char.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Sha1ForNonSecretPurposes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BusyManager.cpp
)
