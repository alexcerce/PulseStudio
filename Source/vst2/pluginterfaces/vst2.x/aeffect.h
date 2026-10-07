// The VST 2 plugin interface: what a host and a plugin of that kind say to each other.
//
// This file was written for Pulse Studio from the publicly known shape of the interface (the layout of the
// plugin's record and the numbers of the requests), so that plugins of that kind can be loaded. It is not
// Steinberg's SDK file and contains none of it. "VST" is a trademark of Steinberg Media Technologies GmbH.
//
// It is included from inside a namespace by the host code, so it must not include anything itself.
#ifndef PULSE_VST2_AEFFECT_H
#define PULSE_VST2_AEFFECT_H

typedef short              VstInt16;
typedef int                VstInt32;
typedef long long          VstInt64;
#if defined (_WIN64) || defined (__LP64__)
typedef long long          VstIntPtr;
#else
typedef int                VstIntPtr;
#endif

#if defined (_WIN32)
 #define VSTCALLBACK __cdecl
#else
 #define VSTCALLBACK
#endif

#define CCONST(a, b, c, d) ((((VstInt32) (a)) << 24) | (((VstInt32) (b)) << 16) | (((VstInt32) (c)) << 8) | (((VstInt32) (d)) << 0))

#define kEffectMagic CCONST ('V', 's', 't', 'P')

struct AEffect;

typedef VstIntPtr (VSTCALLBACK* audioMasterCallback) (AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef VstIntPtr (VSTCALLBACK* AEffectDispatcherProc) (AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef void (VSTCALLBACK* AEffectProcessProc) (AEffect* effect, float** inputs, float** outputs, VstInt32 sampleFrames);
typedef void (VSTCALLBACK* AEffectProcessDoubleProc) (AEffect* effect, double** inputs, double** outputs, VstInt32 sampleFrames);
typedef void (VSTCALLBACK* AEffectSetParameterProc) (AEffect* effect, VstInt32 index, float parameter);
typedef float (VSTCALLBACK* AEffectGetParameterProc) (AEffect* effect, VstInt32 index);

#if defined (_WIN32)
 #pragma pack (push, 8)
#endif

/** The record a plugin hands to its host. */
struct AEffect
{
    VstInt32 magic;                         // kEffectMagic
    AEffectDispatcherProc dispatcher;
    AEffectProcessProc process;             // (the old, adding kind of processing)
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;
    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;
    VstInt32 flags;
    VstIntPtr resvd1;
    VstIntPtr resvd2;
    VstInt32 initialDelay;
    VstInt32 realQualities;
    VstInt32 offQualities;
    float ioRatio;
    void* object;
    void* user;
    VstInt32 uniqueID;
    VstInt32 version;
    AEffectProcessProc processReplacing;
    AEffectProcessDoubleProc processDoubleReplacing;
    char future[56];
};

#if defined (_WIN32)
 #pragma pack (pop)
#endif

enum VstAEffectFlags
{
    effFlagsHasEditor          = 1 << 0,
    effFlagsHasClip            = 1 << 1,
    effFlagsHasVu              = 1 << 2,
    effFlagsCanMono            = 1 << 3,
    effFlagsCanReplacing       = 1 << 4,
    effFlagsProgramChunks      = 1 << 5,
    effFlagsIsSynth            = 1 << 8,
    effFlagsNoSoundInStop      = 1 << 9,
    effFlagsExtIsAsync         = 1 << 10,
    effFlagsExtHasBuffer       = 1 << 11,
    effFlagsCanDoubleReplacing = 1 << 12
};

/** What a host asks of a plugin (the first twenty-five requests; the rest are in aeffectx.h). */
enum AEffectOpcodes
{
    effOpen = 0,
    effClose,
    effSetProgram,
    effGetProgram,
    effSetProgramName,
    effGetProgramName,
    effGetParamLabel,
    effGetParamDisplay,
    effGetParamName,
    effGetVu,
    effSetSampleRate,
    effSetBlockSize,
    effMainsChanged,
    effEditGetRect,
    effEditOpen,
    effEditClose,
    effEditDraw,
    effEditMouse,
    effEditKey,
    effEditIdle,
    effEditTop,
    effEditSleep,
    effIdentify,
    effGetChunk,
    effSetChunk,
    effNumOpcodes
};

/** What a plugin asks of its host (the first five; the rest are in aeffectx.h). */
enum AudioMasterOpcodes
{
    audioMasterAutomate = 0,
    audioMasterVersion,
    audioMasterCurrentId,
    audioMasterIdle,
    audioMasterPinConnected
};

enum VstStringConstants
{
    kVstMaxProgNameLen   = 24,
    kVstMaxParamStrLen   = 8,
    kVstMaxVendorStrLen  = 64,
    kVstMaxProductStrLen = 64,
    kVstMaxEffectNameLen = 32
};

#if defined (_WIN32)
 #pragma pack (push, 8)
#endif

/** The size of a plugin's own window. */
struct ERect
{
    VstInt16 top;
    VstInt16 left;
    VstInt16 bottom;
    VstInt16 right;
};

#if defined (_WIN32)
 #pragma pack (pop)
#endif

#endif
