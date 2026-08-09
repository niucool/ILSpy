# Decompiler/IL -- the ILAst instruction model (Phase 3 start). Mirrors
# ICSharpCode.Decompiler/IL. The base (ILInstruction) and the type->StackType
# mapping have out-of-line code; the concrete instructions are header-only so far
# and are reached via includes from the tests and (later) the IL reader.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ILInstruction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StackTypeOf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ILReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BlockBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/VariableUsage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/ControlFlowSimplification.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/ControlFlowGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/LoopDetection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/ConditionDetection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/DetectPinnedRegions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/RemoveRedundantReturn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/SwitchAnalysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlow/SwitchDetection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/ILInlining.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/InlineReturnTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/AssignVariableNames.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/StObjToStLoc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/RemoveInfeasiblePathTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/DetectCatchWhenConditionBlocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/LdLocaDupInitObjTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/EarlyExpressionTransforms.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/RemoveDeadVariableInit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/HighLevelLoopTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/NullableLiftingTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/SwitchOnNullableTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/PatternMatchingTransform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transforms/LockTransform.cpp
)
