#pragma once

namespace Core::Threading {
    void InitCores();
    void ShutdownCores();
    void MasterTimingLoop();
    void ARM9_Thread();
    void ARM7_Thread();
    void SyncThreads();
}
