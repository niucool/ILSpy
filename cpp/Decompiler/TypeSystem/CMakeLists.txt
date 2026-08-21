# Decompiler/TypeSystem (Phase 2 start). Mirrors ICSharpCode.Decompiler/TypeSystem.
# Naming primitives, the known-type table, the IType hierarchy, and signature
# decoding on top of the vendored winmd reader.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/KnownTypeCode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TopLevelTypeName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FullTypeName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeKindDerivation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeParameterSubstitution.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KnownTypeReference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ByReferenceTypeReference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PointerTypeReference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ArrayTypeReference.cpp
)
