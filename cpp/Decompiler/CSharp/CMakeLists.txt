# Decompiler/CSharp -- the C# back end (Phase 5). Mirrors ICSharpCode.Decompiler/CSharp.
# ILAstToCSharp is the seed of the back end: an ILAst -> C#-text walker that
# demonstrates the IL -> ILAst -> text pipeline end-to-end ahead of the real
# resolver/AST/output stages (see PORT_PLAN.md, Phase 5).

add_subdirectory(TypeSystem)
add_subdirectory(ProjectDecompiler)
target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ILAstToCSharp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Annotations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TranslatedExpression.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ExpressionBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CallBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StatementBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/AddCheckedBlocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/ReplaceMethodCallsWithOperators.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/FlattenSwitchBlocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/FixNameCollisions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/EscapeInvalidIdentifiers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Syntax/AstNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Syntax/AstType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Syntax/TypeSystemAstBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/TokenWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/CSharpOutputVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/CSharpAmbience.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/InsertParenthesesVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/GenericGrammarAmbiguityVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/LookupHelpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/MemberLookup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/OverloadResolutionHelpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/OverloadResolution.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/MethodGroupResolveResult.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/CSharpConversions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/CSharpConversionsHelpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/CSharpOperators.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/CSharpResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resolver/TypeInferenceHelpers.cpp
)
