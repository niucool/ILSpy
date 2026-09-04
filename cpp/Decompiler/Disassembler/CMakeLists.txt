# Decompiler/Disassembler: the Phase-6 disassembler tree -- OpCodeInfo (header-only),
# ReflectionAttributes (header-only), EnumNameCollection (header-only), and
# DisassemblerHelpers (the static helper class MethodBodyDisassembler and
# ReflectionDisassembler consume). Headers are reachable as
# "Decompiler/Disassembler/..." via the root include directory on the ilspy target;
# only the .cpp that has out-of-line code is listed here.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/DisassemblerHelpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DisassemblerSignatureTypeProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ILParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ILStructure.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MethodBodyDisassembler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ReflectionDisassembler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SortByNameProcessor.cpp
)
