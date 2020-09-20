#include "Mouse.h"

MouseEvent::MouseEvent()
	:
	type(Invalid),
	x(0),
	y(0)
{
}

MouseEvent::MouseEvent(EventType type, int x, int y)
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

void Mouse::OnLeftPressed(int x, int y)
{
	this->leftIsDown = true;
	MouseEvent me(MouseEvent::EventType::LPress, x, y);
	this->eventBuffer.push(me);
}

void Mouse::OnLeftReleased(int x, int y)
{
	this->leftIsDown = false;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::LRelease, x, y));
}

void Mouse::OnRightPressed(int x, int y)
{
	this->rightIsDown = true;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RPress, x, y));
}

void Mouse::OnRightReleased(int x, int y)
{
	this->rightIsDown = false;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RRelease, x, y));
}

void Mouse::OnMiddlePressed(int x, int y)
{
	this->mbuttonDown = true;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::MPress, x, y));
}

void Mouse::OnMiddleReleased(int x, int y)
{
	this->mbuttonDown = false;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::MRelease, x, y));
}

void Mouse::OnWheelUp(int x, int y)
{
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::WheelUp, x, y));
}

void Mouse::OnWheelDown(int x, int y)
{
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::WheelDown, x, y));
}

void Mouse::OnMouseMove(int x, int y)
{
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::Move, x, y));
}

void Mouse::OnMouseMoveRaw(int x, int y)
{
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RAW_MOVE, x, y));
}

bool Mouse::IsLeftDown()
{
	return this->leftIsDown;
}

bool Mouse::IsMiddleDown()
{
	return this->mbuttonDown;
}

bool Mouse::IsRightDown()
{
	return this->rightIsDown;
}

int Mouse::GetPosX()
{
	return this->x;
}

int Mouse::GetPosY()
{
	return this->y;
}

MousePoint Mouse::GetPos()
{
	return {this->x, this->y};
}

bool Mouse::EventBufferIsEmpty()
{
	return this->eventBuffer.empty();
}

MouseEvent Mouse::ReadEvent()
{
	if (this->eventBuffer.empty())
	{
		return MouseEvent();
	}
	MouseEvent e = this->eventBuffer.front(); //Get first event from buffer
	this->eventBuffer.pop(); //Remove first event from buffer
	return e;
}
