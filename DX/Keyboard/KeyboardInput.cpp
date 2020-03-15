#include "KeyboardInput.h"

namespace GameEngine
{
	namespace Input
	{
		KeyboardInput::KeyboardInput()
		{
			for (int i = 0; i < 256; i++)
				this->keyStates[i] = false; //Initialize all key states to off (false)
		}

		bool KeyboardInput::KeyIsPressed(const unsigned char keycode)
		{
			return this->keyStates[keycode];
		}

		bool KeyboardInput::KeyBufferIsEmpty()
		{
			return this->keyBuffer.empty();
		}

		bool KeyboardInput::CharBufferIsEmpty()
		{
			return this->charBuffer.empty();
		}

		KeyboardEvent KeyboardInput::ReadKey()
		{
			if (this->keyBuffer.empty()) //If no keys to be read?
			{
				return KeyboardEvent(); //return empty keyboard event
			}
			else
			{
				KeyboardEvent e = this->keyBuffer.front(); //Get first Keyboard Event from queue
				this->keyBuffer.pop(); //Remove first item from queue
				return e; //Returns keyboard event
			}
		}

		unsigned char KeyboardInput::ReadChar()
		{
			if (this->charBuffer.empty()) //If no keys to be read?
			{
				return 0u; //return 0 (NULL char)
			}
			else
			{
				unsigned char e = this->charBuffer.front(); //Get first char from queue
				this->charBuffer.pop(); //Remove first char from queue
				return e; //Returns char
			}
		}

		void KeyboardInput::OnKeyPressed(const unsigned char key)
		{
			this->keyStates[key] = true;
			this->keyBuffer.push(KeyboardEvent(KeyboardEvent::EventType::Press, key));
		}

		void KeyboardInput::OnKeyReleased(const unsigned char key)
		{
			this->keyStates[key] = false;
			this->keyBuffer.push(KeyboardEvent(KeyboardEvent::EventType::Release, key));
		}

		void KeyboardInput::OnChar(const unsigned char key)
		{
			this->charBuffer.push(key);
		}

		void KeyboardInput::EnableAutoRepeatKeys()
		{
			this->autoRepeatKeys = true;
		}

		void KeyboardInput::DisableAutoRepeatKeys()
		{
			this->autoRepeatKeys = false;
		}

		void KeyboardInput::EnableAutoRepeatChars()
		{
			this->autoRepeatChars = true;
		}

		void KeyboardInput::DisableAutoRepeatChars()
		{
			this->autoRepeatChars = false;
		}

		bool KeyboardInput::IsKeysAutoRepeat()
		{
			return this->autoRepeatKeys;
		}

		bool KeyboardInput::IsCharsAutoRepeat()
		{
			return this->autoRepeatChars;
		}
	}
}