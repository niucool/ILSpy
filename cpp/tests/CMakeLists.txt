# Google Test driver. Test files mirror the source directory structure
# (e.g. Decompiler/Util/Utf.cpp -> Util/Util_Test.cpp), per the mirror layout.

include(GoogleTest)

add_executable(ilspy_tests
    Util/Util_Test.cpp
    Decompiler/Metadata/Metadata_Test.cpp
    Decompiler/Metadata/MethodBody_Test.cpp
    Decompiler/Metadata/ILDisassembler_Test.cpp
    Decompiler/Metadata/EntitySurface_Test.cpp
    Decompiler/Metadata/CustomAttributes_Test.cpp
    Decompiler/Metadata/ILTextEmitter_Test.cpp
    Decompiler/TypeSystem/TypeName_Test.cpp
    Decompiler/TypeSystem/SignatureDecoder_Test.cpp
    Decompiler/TypeSystem/TypeKindDerivation_Test.cpp
    Decompiler/IL/ILAst_Test.cpp
    Decompiler/IL/ILAstInstructions_Test.cpp
    Decompiler/IL/ILReader_Test.cpp
    Decompiler/IL/ReadIL_Test.cpp
    Decompiler/IL/EHNodes_Test.cpp
    Decompiler/IL/BlockBuilder_Test.cpp
    Decompiler/CSharp/ILAstToCSharp_Test.cpp
    Decompiler/IL/ControlFlowSimplification_Test.cpp
    Decompiler/IL/ControlFlowGraph_Test.cpp
    Decompiler/FlowAnalysis/Dominance_Test.cpp
    Decompiler/IL/ILInlining_Test.cpp
    Decompiler/IL/LoopDetection_Test.cpp
    Decompiler/IL/ConditionDetection_Test.cpp
    Decompiler/IL/InlineReturnTransform_Test.cpp
    Decompiler/IL/AssignVariableNames_Test.cpp
    Decompiler/IL/StObjToStLoc_Test.cpp
    Decompiler/IL/RemoveRedundantReturn_Test.cpp
    Decompiler/IL/RemoveInfeasiblePathTransform_Test.cpp
    Decompiler/IL/DetectCatchWhenConditionBlocks_Test.cpp
    Decompiler/IL/DetectPinnedRegions_Test.cpp
    Decompiler/IL/LdLocaDupInitObj_Test.cpp
    Decompiler/IL/EarlyExpressionTransforms_Test.cpp
    Decompiler/IL/RemoveDeadVariableInit_Test.cpp
)
target_link_libraries(ilspy_tests PRIVATE
    ILSpy::ilspy
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(ilspy_tests)
