#include "pch.h"
#include "KeyboardDevice.h"

KeyboardEvent::KeyboardEvent():
    type(Invalid),
    key(0u)
{
}

KeyboardEvent::KeyboardEvent(const EventType type, const unsigned char key):
    type(type),
    key(key)
{
}

bool KeyboardEvent::IsPress() const
{
    return this->type == Press;
}

bool KeyboardEvent::IsRelease() const
{
    return this->type == Release;
}

bool KeyboardEvent::IsValid() const
{
    return this->type != Invalid;
}

unsigned char KeyboardEvent::GetKeyCode() const
{
    return this->key;
}

KeyboardDevice::KeyboardDevice()
{
    for (int i = 0; i < 256; i++)
        this->keyStates[i] = false; //Initialize all key states to off (false)
}

bool KeyboardDevice::KeyIsPressed(const unsigned char keycode) const
{
    return this->keyStates[keycode];
}

bool KeyboardDevice::KeyBufferIsEmpty() const
{
    return this->keyBuffer.empty();
}

bool KeyboardDevice::CharBufferIsEmpty() const
{
    return this->charBuffer.empty();
}

KeyboardEvent KeyboardDevice::ReadKey()
{
    if (this->keyBuffer.empty()) //If no keys to be read?
    {
        return KeyboardEvent(); //return empty keyboard event
    }
    KeyboardEvent e = this->keyBuffer.front(); //Get first Keyboard Event from queue
    this->keyBuffer.pop(); //Remove first item from queue
    return e; //Returns keyboard event
}

unsigned char KeyboardDevice::ReadChar()
{
    if (this->charBuffer.empty()) //If no keys to be read?
    {
        return 0u; //return 0 (NULL char)
    }
    unsigned char e = this->charBuffer.front(); //Get first char from queue
    this->charBuffer.pop(); //Remove first char from queue
    return e; //Returns char
}

void KeyboardDevice::OnKeyPressed(const unsigned char key)
{
    this->keyStates[key] = true;
    this->keyBuffer.push(KeyboardEvent(KeyboardEvent::EventType::Press, key));
}

void KeyboardDevice::OnKeyReleased(const unsigned char key)
{
    this->keyStates[key] = false;
    this->keyBuffer.push(KeyboardEvent(KeyboardEvent::EventType::Release, key));
}

void KeyboardDevice::OnChar(const unsigned char key)
{
    this->charBuffer.push(key);
}

void KeyboardDevice::EnableAutoRepeatKeys()
{
    this->autoRepeatKeys = true;
}

void KeyboardDevice::DisableAutoRepeatKeys()
{
    this->autoRepeatKeys = false;
}

void KeyboardDevice::EnableAutoRepeatChars()
{
    this->autoRepeatChars = true;
}

void KeyboardDevice::DisableAutoRepeatChars()
{
    this->autoRepeatChars = false;
}

bool KeyboardDevice::IsKeysAutoRepeat() const
{
    return this->autoRepeatKeys;
}

bool KeyboardDevice::IsCharsAutoRepeat() const
{
    return this->autoRepeatChars;
}
