#include "MouseInput.h"
namespace GameEngine
{
	namespace Input
	{
		void MouseInput::OnLeftPressed(int x, int y)
		{
			this->leftIsDown = true;
			MouseEvent me(MouseEvent::EventType::LPress, x, y);
			this->eventBuffer.push(me);
		}

		void MouseInput::OnLeftReleased(int x, int y)
		{
			this->leftIsDown = false;
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::LRelease, x, y));
		}

		void MouseInput::OnRightPressed(int x, int y)
		{
			this->rightIsDown = true;
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RPress, x, y));
		}

		void MouseInput::OnRightReleased(int x, int y)
		{
			this->rightIsDown = false;
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RRelease, x, y));
		}

		void MouseInput::OnMiddlePressed(int x, int y)
		{
			this->mbuttonDown = true;
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::MPress, x, y));
		}

		void MouseInput::OnMiddleReleased(int x, int y)
		{
			this->mbuttonDown = false;
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::MRelease, x, y));
		}

		void MouseInput::OnWheelUp(int x, int y)
		{
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::WheelUp, x, y));
		}

		void MouseInput::OnWheelDown(int x, int y)
		{
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::WheelDown, x, y));
		}

		void MouseInput::OnMouseMove(int x, int y)
		{
			this->x = x;
			this->y = y;
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::Move, x, y));
		}

		void MouseInput::OnMouseMoveRaw(int x, int y)
		{
			this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RAW_MOVE, x, y));
		}

		bool MouseInput::IsLeftDown()
		{
			return this->leftIsDown;
		}

		bool MouseInput::IsMiddleDown()
		{
			return this->mbuttonDown;
		}

		bool MouseInput::IsRightDown()
		{
			return this->rightIsDown;
		}

		int MouseInput::GetPosX()
		{
			return this->x;
		}

		int MouseInput::GetPosY()
		{
			return this->y;
		}

		MousePoint MouseInput::GetPos()
		{
			return{ this->x, this->y };
		}

		bool MouseInput::EventBufferIsEmpty()
		{
			return this->eventBuffer.empty();
		}

		MouseEvent MouseInput::ReadEvent()
		{
			if (this->eventBuffer.empty())
			{
				return MouseEvent();
			}
			else
			{
				MouseEvent e = this->eventBuffer.front(); //Get first event from buffer
				this->eventBuffer.pop(); //Remove first event from buffer
				return e;
			}
		}
	}
}