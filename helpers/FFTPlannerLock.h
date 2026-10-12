#pragma once
#include <mutex>
// FFTW planning and plan destruction share process-wide mutable state.
// This lock belongs exclusively to graph preparation and analysis control.
inline std::mutex fftwPlannerMutex;
