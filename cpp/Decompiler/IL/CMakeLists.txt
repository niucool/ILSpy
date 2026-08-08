# Decompiler/IL -- the ILAst instruction model (Phase 3 start). Mirrors
# ICSharpCode.Decompiler/IL. The base (ILInstruction) and the type->StackType
# mapping have out-of-line code; the concrete instructions are header-only so far
# and are reached via includes from the tests and (later) the IL reader.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ILInstruction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StackTypeOf.cpp
)
