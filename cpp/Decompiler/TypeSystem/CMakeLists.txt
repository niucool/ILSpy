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
    ${CMAKE_CURRENT_SOURCE_DIR}/ParameterizedTypeReference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/NestedTypeReference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DefaultAssemblyReference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KnownTypeCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MergedNamespace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SimpleCompilation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeSystemExtensions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/NormalizeTypeVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/DummyTypeParameter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/SpecializedMember.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/SpecializedParameterizedMember.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/AbstractTypeParameter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/SpecializedMethod.cpp
)
