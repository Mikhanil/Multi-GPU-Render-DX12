#pragma once
#include <queue>

struct MousePoint
{
    int x;
    int y;
};

class MouseEvent
{
public:
    enum EventType
    {
        LPress,
        LRelease,
        RPress,
        RRelease,
        MPress,
        MRelease,
        WheelUp,
        WheelDown,
        Move,
        RAW_MOVE,
        Invalid
    };

private:
    EventType type;
    int x;
    int y;

public:
    MouseEvent();
    MouseEvent(EventType type, int x, int y);
    bool IsValid() const;
    EventType GetType() const;
    MousePoint GetPos() const;
    int GetPosX() const;
    int GetPosY() const;
};

class Mousepad
{
public:
    void OnLeftPressed(int x, int y);
    void OnLeftReleased(int x, int y);
    void OnRightPressed(int x, int y);
    void OnRightReleased(int x, int y);
    void OnMiddlePressed(int x, int y);
    void OnMiddleReleased(int x, int y);
    void OnWheelUp(int x, int y);
    void OnWheelDown(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseMoveRaw(int x, int y);

    bool IsLeftDown() const;
    bool IsMiddleDown() const;
    bool IsRightDown() const;

    int GetPosX() const;
    int GetPosY() const;
    MousePoint GetPos() const;

    bool EventBufferIsEmpty() const;
    MouseEvent ReadEvent();

private:
    std::queue<MouseEvent> eventBuffer;
    bool leftIsDown = false;
    bool rightIsDown = false;
    bool mbuttonDown = false;
    int x = 0;
    int y = 0;
};
