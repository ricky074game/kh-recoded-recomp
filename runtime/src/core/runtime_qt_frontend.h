#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class SDL2Renderer;
class NDSMemory;

using UiSnapshotCallback = std::function<void(const std::string&)>;

int RunQtFrontend(int argc,
                  char* argv[],
                  SDL2Renderer* renderer,
                  NDSMemory* memory,
                  std::atomic<bool>* running,
                  std::atomic<uint64_t>* uiPresentCount,
                  UiSnapshotCallback snapshotCallback);
