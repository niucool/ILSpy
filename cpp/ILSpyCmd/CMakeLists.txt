# ilspycmd CLI front-end (ICSharpCode.ILSpyCmd). Phase 10 fills in the full
# option set; Phase 0 wires a minimal main that proves the toolchain links
# (ilspy + cxxopts). The IlspyCmdProgram members the -il path consumes
# (ShowIL/TryLoadPDB) attach to the aggregate library so the test driver
# can exercise them directly (the ILSpyX convention); main.cpp parses the
# options and calls them.

# The IlspyCmdProgram port (ShowIL/TryLoadPDB).
target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/IlspyCmdProgram.cpp
)

add_executable(ilspy_cli ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
add_executable(ILSpy::ilspy_cli ALIAS ilspy_cli)
target_link_libraries(ilspy_cli PRIVATE ILSpy::ilspy cxxopts::cxxopts)
