#pragma once

// Explicit no-content diagnostics for the actual 64-bit client executable.
// This path never calls CommonMain and never qualifies game initialization.
namespace esteria::entrycheck {
enum class Mode { None, Requested, OfflineRequested, LoopbackRequested, Invalid };
Mode ParseCommandLine();
int OnMainFiber(void* callerFiber, void* expectedMainFiber);
int FinishAfterFiberReturn(int result, void* callerFiber);
}
