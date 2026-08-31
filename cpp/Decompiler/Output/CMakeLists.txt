# Decompiler/Output: the output-stage interface layer -- IAmbience (header-only),
# ITextOutput (header-only) and PlainTextOutput (the plain-text ITextOutput sink the
# IL view's ILAmbience and the Phase-6 ReflectionDisassembler write through).
# Headers are reachable as "Decompiler/Output/..." via the root include directory on
# the ilspy target; only the .cpp that has out-of-line code is listed here.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/PlainTextOutput.cpp
)
