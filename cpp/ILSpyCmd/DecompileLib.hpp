// ILSpyCmd/DecompileLib.hpp — the in-process entries for the bennu umbrella
// (DOTNET_PLAN.md Phase B). Mirrors sleigh's hada_lib/HADA_AS_LIB pattern:
// the same CLI sources compiled with ILSPY_CMD_AS_LIB contribute
// ilspycmd::run_cli (and, via DecompileLib.cpp, the direct
// decompile_module_to_string entry) but NOT a main symbol, so the bennu
// umbrella links the lib and dispatches in-process.
//
// Two entries:
//   ilspycmd::run_cli(argc, argv)              — the full ilspycmd CLI
//                                                (argv0-passthrough shape:
//                                                args carry the subcommand
//                                                args only).
//   ilspy::decompile_module_to_string(input,   — the direct whole-module
//                                    out, err)   C# render the session leg
//                                                calls (no CLI parse).
#pragma once

#include <string>

namespace ilspycmd {

// The ilspycmd CLI entry (the port's RunMain). Parses its own options
// (--csharp / -t / -o / --il ...); exits via return code, never via
// exit() from the library path (the CLI-parse error path inside
// RunMain does std::exit — acceptable for argv0-passthrough dispatch,
// which replaces the process anyway; the session leg does NOT call
// run_cli for exactly that reason).
int run_cli(int argc, char** argv);

}  // namespace ilspycmd

namespace ilspy {

// Decompile a managed assembly to C# text (whole module), in-process.
// Built for the bennu session's run_dotnet_decompile_task (DOTNET_PLAN.md
// §3/Phase B): no CLI parse, no stdout writes, errors returned not thrown.
//
// Returns 0 on success with outText carrying the decompiled C# (CRLF text,
// the same bytes the C# port's TextWriter convention produces). On failure
// returns non-zero and fills `error` with the exception's message
// (LowlevelError / DecoderError / bad input path — all caught inside; this
// function throws nothing). Safe to call behind the session's
// deadline/detach pattern: no globals mutated, no threads spawned.
int decompile_module_to_string(const std::string& inputPath,
                               std::string& outText, std::string& error);

}  // namespace ilspy
