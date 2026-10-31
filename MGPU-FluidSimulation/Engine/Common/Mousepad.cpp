#include "pch.h"
#include "Mousepad.h"

MouseEvent::MouseEvent()
    :
    type(Invalid),
    x(0),
    y(0)
{
}

MouseEvent::MouseEvent(const EventType type, const int x, const int y)
    :
    type(type),
    x(x),
    y(y)
{
}

bool MouseEvent::IsValid() const
{
    return this->type != Invalid;
}

MouseEvent::EventType MouseEvent::GetType() const
{
    return this->type;
}

MousePoint MouseEvent::GetPos() const
{
    return {this->x, this->y};
}

int MouseEvent::GetPosX() const
{
    return this->x;
}

int MouseEvent::GetPosY() const
{
    return this->y;
}

void Mousepad::OnLeftPressed(const int x, const int y)
{
    this->leftIsDown = true;
    MouseEvent me(MouseEvent::EventType::LPress, x, y);
    this->eventBuffer.push(me);
}

void Mousepad::OnLeftReleased(const int x, const int y)
{
    this->leftIsDown = false;
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::LRelease, x, y));
}

void Mousepad::OnRightPressed(const int x, const int y)
{
    this->rightIsDown = true;
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RPress, x, y));
}

void Mousepad::OnRightReleased(const int x, const int y)
{
    this->rightIsDown = false;
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RRelease, x, y));
}

void Mousepad::OnMiddlePressed(const int x, const int y)
{
    this->mbuttonDown = true;
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::MPress, x, y));
}

void Mousepad::OnMiddleReleased(const int x, const int y)
{
    this->mbuttonDown = false;
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::MRelease, x, y));
}

void Mousepad::OnWheelUp(const int x, const int y)
{
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::WheelUp, x, y));
}

void Mousepad::OnWheelDown(const int x, const int y)
{
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::WheelDown, x, y));
}

void Mousepad::OnMouseMove(const int x, const int y)
{
    this->x = x;
    this->y = y;
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::Move, x, y));
}

void Mousepad::OnMouseMoveRaw(const int x, const int y)
{
    this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RAW_MOVE, x, y));
}

bool Mousepad::IsLeftDown() const
{
    return this->leftIsDown;
}

bool Mousepad::IsMiddleDown() const
{
    return this->mbuttonDown;
}

bool Mousepad::IsRightDown() const
{
    return this->rightIsDown;
}

int Mousepad::GetPosX() const
{
    return this->x;
}

int Mousepad::GetPosY() const
{
    return this->y;
}

MousePoint Mousepad::GetPos() const
{
    return {this->x, this->y};
}

bool Mousepad::EventBufferIsEmpty() const
{
    return this->eventBuffer.empty();
}

MouseEvent Mousepad::ReadEvent()
{
    if (this->eventBuffer.empty())
    {
        return MouseEvent();
    }
    MouseEvent e = this->eventBuffer.front(); //Get first event from buffer
    this->eventBuffer.pop(); //Remove first event from buffer
    return e;
}
