#pragma once
#include <queue>

class KeyboardEvent
{
public:
    enum EventType
    {
        Press,
        Release,
        Invalid
    };

    KeyboardEvent();;

    KeyboardEvent(EventType type, unsigned char key);;

    bool IsPress() const;;

    bool IsRelease() const;;

    bool IsValid() const;;

    unsigned char GetKeyCode() const;;

private:
    EventType type;
    unsigned char key;
};


class KeyboardDevice
{
public:
    KeyboardDevice();;
    bool KeyIsPressed(unsigned char keycode) const;;
    bool KeyBufferIsEmpty() const;;
    bool CharBufferIsEmpty() const;;
    KeyboardEvent ReadKey();;
    unsigned char ReadChar();;
    void OnKeyPressed(unsigned char key);;
    void OnKeyReleased(unsigned char key);;
    void OnChar(unsigned char key);;
    void EnableAutoRepeatKeys();;
    void DisableAutoRepeatKeys();;
    void EnableAutoRepeatChars();;
    void DisableAutoRepeatChars();;
    bool IsKeysAutoRepeat() const;;
    bool IsCharsAutoRepeat() const;;

private:
    bool autoRepeatKeys = false;
    bool autoRepeatChars = false;
    bool keyStates[256];
    std::queue<KeyboardEvent> keyBuffer;
    std::queue<unsigned char> charBuffer;
};
