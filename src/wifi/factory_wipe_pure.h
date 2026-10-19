#pragma once

/** Abort + restore HTTP only when NVS was not mutated and the partition was not erased. */
inline auto factoryWipeShouldAbort(bool allCleared, bool anyMutated, bool erased) -> bool {
    return !allCleared && !anyMutated && !erased;
}

/** Restart without HTTP when wipe is not ready but NVS was mutated or the partition was erased. */
inline auto factoryWipeMustRestartUnready(bool ready, bool anyMutated, bool erased) -> bool {
    return !ready && (anyMutated || erased);
}
