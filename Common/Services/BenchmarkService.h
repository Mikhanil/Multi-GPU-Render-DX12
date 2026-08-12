#pragma once
#include <memory>
#include <vector>

#include "States/BenchmarkState.h"

class FileQueueWriter;


class BenchmarkService final
{
    std::vector<std::shared_ptr<BenchmarkState>> states;
    int currentStateIndex = -1;

    BenchmarkState* CurrentState = nullptr;
    bool looping = true;

    void SetState(BenchmarkState* state);

public:
    // Existing samples historically looped their benchmark queue.  Long-running
    // exhaustive benchmarks can opt out so the last state is measured once.
    void SetLooping(bool enabled) { looping = enabled; }
    bool IsFinished() const { return currentStateIndex < 0; }

    void Start();

    template <class T = BenchmarkState, typename... Args>
    T& AddState(Args&&... args)
    {
        states.emplace_back(std::make_shared<T>(std::forward<Args>(args)...));
        const auto& state = states.back();
        return *static_cast<T*>(state.get());
    }

    void Tick(float deltaTime);
};
