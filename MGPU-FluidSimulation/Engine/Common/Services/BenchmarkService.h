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

    void SetState(BenchmarkState* state);

public:
    void Start();

    template <class T = BenchmarkState, typename... Args>
    inline T& AddState(Args&&... args)
    {
        states.emplace_back(std::make_shared<T>(std::forward<Args>(args)...));
        const auto& state = states.back();
        return *static_cast<T*>(state.get());
    }

    void Tick(float deltaTime);
};
