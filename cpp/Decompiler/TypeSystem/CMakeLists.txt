# Decompiler/TypeSystem (Phase 2 start). Mirrors ICSharpCode.Decompiler/TypeSystem.
# Naming primitives, the known-type table, the IType hierarchy, and signature
# decoding on top of the vendored winmd reader.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/KnownTypeCode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TopLevelTypeName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FullTypeName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Version.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeKindDerivation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ApplyAttributeTypeVisitor.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/DefaultAttribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/CustomAttribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/AttributeListBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/SpecializedMember.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/SpecializedParameterizedMember.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/AbstractTypeParameter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/SpecializedMethod.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/GetMembersHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MetadataNamespace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MinimalCorlib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MetadataTypeDefinition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/DecimalConstantHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MetadataField.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MetadataMethod.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MetadataProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MetadataEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MetadataParameter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/MetadataTypeParameter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/DefaultTypeParameter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/InheritanceHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ReflectionHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/NullableType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TupleType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TaskType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IntersectionType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MetadataModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VarArgInstanceMethod.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/FakeMember.cpp
)
