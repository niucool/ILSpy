# Decompiler/DebugInfo: the debug-information provider contract --
# SequencePoint (header-only) and IDebugInfoProvider (header-only), consumed
# by the MethodBodyDisassembler's ShowSequencePoints rendering and the
# .locals debug names. Headers are reachable as "Decompiler/DebugInfo/..."
# via the root include directory on the ilspy target; the real PDB reader
# that implements the interface is the Phase-8 PdbProvider.
