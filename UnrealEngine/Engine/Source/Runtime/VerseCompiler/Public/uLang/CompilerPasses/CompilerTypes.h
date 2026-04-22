// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Diagnostics/Diagnostics.h"
#include "uLang/CompilerPasses/ApiLayerInjections.h"

namespace uLang
{

// Forward declarations
enum class EAstNodeSetKey : uintptr_t;

/** Collection of all injection types for the toolchain -- conveniently bundled up for portability. */
struct SToolchainInjections
{
    TSRefArray<IPreParseInjection>         _PreParseInjections;
    TSRefArray<IPostParseInjection>        _PostParseInjections;
    TSRefArray<IPreSemAnalysisInjection>   _PreSemAnalysisInjections;
    TSRefArray<IIntraSemAnalysisInjection> _IntraSemAnalysisInjections;
    TSRefArray<IPostSemAnalysisInjection>  _PostSemAnalysisInjections;
    TSRefArray<IPreTranslateInjection>     _PreTranslateInjections;
    TSRefArray<IPreLinkInjection>          _PreLinkInjections;
};

/**
* Abstraction for versioning objects used in compilation
*/
struct FSolFingerprint
{
    alignas(uint32_t) uint8_t Bytes[20]{};
    static const FSolFingerprint Zero;

    void Reset()
    {
        memset(Bytes, 0, sizeof(Bytes));
    }

    friend inline bool operator==(const FSolFingerprint& A, const FSolFingerprint& B)
    {
        return memcmp(A.Bytes, B.Bytes, sizeof(A.Bytes)) == 0;
    }

    friend inline bool operator!=(const FSolFingerprint& A, const FSolFingerprint& B)
    {
        return memcmp(A.Bytes, B.Bytes, sizeof(A.Bytes)) != 0;
    }

    CUTF8String ToCUTF8String() const
    {
        const auto NibbleToHex = [](uint8_t Value) { return char(Value + (Value > 9 ? 'A' - 10 : '0')); };

        CUTF8String Output;
        const uint64_t CharLength = sizeof(Bytes) * 2;
        Output.Resize(CharLength + 1);

        const uint8_t* Data = Bytes;
        char* WriteHead = (char*) Output.begin().CurrentByte();
        for (const uint8_t* DataEnd = Data + sizeof(Bytes); Data != DataEnd; ++Data)
        {
            *WriteHead++ = NibbleToHex(*Data >> 4);
            *WriteHead++ = NibbleToHex(*Data & 15);
        }
        *WriteHead++ = 0;
        return Output;
    }
};
inline const FSolFingerprint FSolFingerprint::Zero;

struct FSolFingerprintDiagnostics : public CSharedMix
{
    TArray<CUTF8String> _Updates;
};

/**
* Abstraction for generating fingerprints
*/
class ISolFingerprintGenerator : public CSharedMix
{
public:
    virtual ~ISolFingerprintGenerator() {}

    /** Clears existing generator state. Useful when re-using generators. */
    virtual void Reset() = 0;
    /** Updates the generator state with a buffer of data to be used for fingerprint creation */
    virtual void Update(const void* Data, uint64_t Size, const char* DebugContext = nullptr) = 0;
    /** Generates a fingerprint from the generators current state. */
    virtual FSolFingerprint Finalize(const char* DebugContext = nullptr) = 0;
};

/** */
struct SCommandLine
{
    TArray<CUTF8String> _Tokens;
    TArray<CUTF8String> _Switches;
    CUTF8String _Unparsed;
};

/** Per package, remember what dependencies it uses */
struct SPackageUsageEntry
{
    CUTF8String _PackageName;
    TArray<CUTF8String> _UsedDependencies; // Only _directly_ used dependencies, not transitive closure
};

/** Remember what packages use which dependencies */
struct SPackageUsage
{
    TArray<SPackageUsageEntry> _Packages;
};

/** Build parameters. This is the `uLang` equivalent of `FSolIdeBuildSettings` with some additional fields. */
struct SBuildParams
{
    /// This allows us to determine when a package was uploaded for a given Fortnite release version.
    /// It is a HACK that conditionally enables/disables behaviour in the compiler in order to
    /// support previous mistakes allowed to slip through in previous Verse langauge releases  but
    /// now need to be supported for backwards compatability.
    /// When we can confirm that all Fortnite packages that are currently uploaded are beyond this
    /// version being used in all instances of the codebase, this can then be removed.
    uint32_t _UploadedAtFNVersion = VerseFN::UploadedAtFNVersion::Latest;

    /// Maximum number of allowed persistent `var` definitions
    int32_t _MaxNumPersistentVars = 0;

    /// Link-step settings
    enum class ELinkParam
    {
        RequireComplete, // Require complete link
        Skip,            // Skip link step
        Default,
    };

    ELinkParam _LinkType = ELinkParam::Default;

    // HACK_VMSWITCH - remove this once VerseVM is fully brought up
    // Specifies the VM we are compiling the code for
    // We need a variable for this, since even though in most cases
    // WITH_VERSE_BPVM will select the correct value, in the VNI tool we
    // take this information from the command line
    enum class EWhichVM
    {
        VerseVM,
        BPVM
    };
#if WITH_VERSE_BPVM
    EWhichVM _TargetVM = EWhichVM::BPVM;
#else
    EWhichVM _TargetVM = EWhichVM::VerseVM;
#endif

    /// If true, we'll run the build only up to semantic analysis
    bool _bSemanticAnalysisOnly : 1 = false;

    /// If to generate digests when possible
    bool _bGenerateDigests : 1 = true;

    /// If to generate bytecode
    bool _bGenerateCode : 1 = true;

    /// Whether to qualify all unidentified qualifiers in-place in the VST.
    bool _bQualifyIdentifiers : 1 = false;

    /// Get more information
    bool _bVerbose : 1 = false;
};

/** Settings pertaining to individual runs through the toolchain (build flags, etc.) */
struct SBuildContext
{
    /// Accumulated issues/glitches over all compile phases
    TSRef<CDiagnostics> _Diagnostics;
    /// Additional API injections for the individual build pass only
    SToolchainInjections _AddedInjections;
    /// Name of package providing built-in functionality
    TArray<CUTF8String> _BuiltInPackageNames = {"Solaris/VerseNative"}; // HACK for now, at least just in one place :-)
    /// Optional database of dependencies actually used by packages
    TUPtr<SPackageUsage> _PackageUsage;
    /// Params passed into the Build command
    SBuildParams _Params;

    /// Allow the toolchain to reuse VSTs for files that haven't changed
    bool bCloneValidSnippetVsts = false;

    explicit SBuildContext(TSRef<CDiagnostics> Diagnostics) : _Diagnostics(Move(Diagnostics)) {}

    SBuildContext()
        : _Diagnostics(TSRef<CDiagnostics>::New())
    {}
};

/** Persistent data from consecutive toolchain runs -- provides a holistic view of the entire program. */
struct SProgramContext
{
    /// Whole view of checked program ready for conversion to runtime equivalent - its types, routines and code bodies of expressions
    TSRef<CSemanticProgram> _Program;

    SProgramContext(const TSRef<CSemanticProgram>& Program)
        : _Program(Program)
    {}
};

} // namespace uLang
