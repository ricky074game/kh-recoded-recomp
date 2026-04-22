#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class SoftwareRenderer;
class NDSMemory;

using UiSnapshotCallback = std::function<void(const std::string&)>;

int RunQtFrontend(int argc,
                  char* argv[],
                  SoftwareRenderer* renderer,
                  NDSMemory* memory,
                  std::atomic<bool>* running,
                  std::atomic<uint64_t>* uiPresentCount,
                  UiSnapshotCallback snapshotCallback);
