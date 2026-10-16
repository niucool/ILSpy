# Decompiler/CSharp/TypeSystem -- mirrors ICSharpCode.Decompiler/CSharp/TypeSystem:
# the C#-flavoured type resolve context (`CSharpTypeResolveContext`) and the
# `UsingScope` it chains (the per-scope using/namespace state the CSharpResolver
# skeleton holds and mutates through).

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/CSharpTypeResolveContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UsingScope.cpp
)
