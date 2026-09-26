// ILSpyCmd/DecompileLib.cpp — the in-process entries for the bennu umbrella
// (DOTNET_PLAN.md Phase B). See DecompileLib.hpp for the contract.
//
// The load + render sequence mirrors ILSpyCmd/main.cpp's wantCSharp arm
// (MetadataFile load → IsValid gate → CSharpDecompiler →
// DecompileWholeModuleToString) so the session leg and the CLI produce
// byte-identical text for the same input. All port exceptions
// (LowlevelError from the metadata/IL layers, DecoderError from the PE
// decode, std::bad_alloc on pathological inputs) are caught here and
// reported through the (ret, error) channel — the bennu session runs this
// behind the deadline/detach pattern and must never see an escape.

#include "ILSpyCmd/DecompileLib.hpp"

#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/DecompilerSettings.hpp"   // complete type for the default-constructed settings
#include "Decompiler/Metadata/MetadataFile.hpp"  // MetadataFile (complete type for the ctor)

namespace ilspy {

int decompile_module_to_string(const std::string& inputPath,
                               std::string& outText, std::string& error) {
    try {
        // The ILSpyCmd wantCSharp arm: MetadataFile (winmd metadata layer)
        // + default DecompilerSettings. The port's ctor throws on malformed
        // metadata; IsValid() is the cheap pre-gate that mirrors the CLI's
        // ClassifyCliOpenFailure arms (missing file / not a PE / not CLI).
        ::ILSpy::Decompiler::Metadata::MetadataFile file(inputPath);
        if (!file.IsValid()) {
            error = "not a managed assembly (metadata load failed): " + inputPath;
            return 1;
        }
        ::ILSpy::Decompiler::DecompilerSettings decompilerSettings;
        ::ILSpy::Decompiler::CSharp::CSharpDecompiler decompiler(
            file, decompilerSettings);
        outText = decompiler.DecompileWholeModuleToString();
        return 0;
    } catch (const std::exception& e) {
        // LowlevelError / DecoderError / allocation failures all surface as
        // std::exception-derived types in the port.
        error = e.what();
        return 1;
    } catch (...) {
        error = "unknown exception during decompilation";
        return 1;
    }
}

}  // namespace ilspy

namespace ilspycmd {

// Forward declaration of the CLI entry — RunMain is defined in main.cpp at
// GLOBAL scope (not inside any namespace), so the declaration must match
// that spelling or the link fails with an unresolved ilspycmd::RunMain.
// The ilspy_cmd_lib target compiles main.cpp with ILSPY_CMD_AS_LIB so the
// TU contributes RunMain but NOT a main symbol.
}  // namespace ilspycmd

int RunMain(int argc, char** argv);

namespace ilspycmd {

int run_cli(int argc, char** argv) {
    return ::RunMain(argc, argv);
}

}  // namespace ilspycmd
