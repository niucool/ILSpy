# Decompiler/CSharp -- the C# back end (Phase 5). Mirrors ICSharpCode.Decompiler/CSharp.
# ILAstToCSharp is the seed of the back end: an ILAst -> C#-text walker that
# demonstrates the IL -> ILAst -> text pipeline end-to-end ahead of the real
# resolver/AST/output stages (see PORT_PLAN.md, Phase 5).

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ILAstToCSharp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Syntax/AstNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/TokenWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/CSharpOutputVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/InsertParenthesesVisitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputVisitor/GenericGrammarAmbiguityVisitor.cpp
)
