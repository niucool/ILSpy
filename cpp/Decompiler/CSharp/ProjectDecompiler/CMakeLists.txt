# Decompiler/CSharp/ProjectDecompiler -- mirrors
# ICSharpCode.Decompiler/CSharp/ProjectDecompiler: the whole-project
# decompiler. Only the static name-sanitizer family of WholeProjectDecompiler
# is ported so far (the CleanUp*/SanitizeFileName members the ilspycmd
# --resource -o extraction and the future project-output paths consume);
# the decompile machinery itself is deferred with the Phase-5/7 back end.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/WholeProjectDecompiler.cpp
)
