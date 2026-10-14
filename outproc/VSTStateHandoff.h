#pragma once
#include "OutProcVSTConfig.h"

// The saved graph is a preset request, not a newer snapshot of an open editor.
// Identical requests retain live DSP/controller state during bypass/reactivation.
inline bool OutProcVSTPresetChanged(const OutProcVSTConfig& previous,const OutProcVSTConfig& incoming) {
    return previous.libraryPath!=incoming.libraryPath || previous.classUID!=incoming.classUID
        || previous.vst3ClassIndex!=incoming.vst3ClassIndex
        || previous.chunkData!=incoming.chunkData || previous.paramMap!=incoming.paramMap;
}
