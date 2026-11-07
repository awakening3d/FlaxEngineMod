// Copyright (c) Wojciech Figat. All rights reserved.

#if PLATFORM_WINDOWS && !PLATFORM_SDL

#include "WindowsInput.h"
#include "WindowsWindow.h"
#include "Engine/Input/Input.h"
#include "Engine/Input/Mouse.h"
#include "Engine/Input/Keyboard.h"
#include "Engine/Input/Gamepad.h"
#include "Engine/Engine/Engine.h"
#include "Engine/Profiler/ProfilerCPU.h"
#include "../Win32/IncludeWindowsHeaders.h"

#define DIRECTINPUT_VERSION 0x0800
#include <xinput.h>

/// <summary>
/// Implementation of the keyboard device for Windows platform.
/// </summary>
/// <seealso cref="Keyboard" />
class WindowsKeyboard : public Keyboard
{
public:

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowsKeyboard"/> class.
    /// </summary>
    explicit WindowsKeyboard()
        : Keyboard()
    {
    }

public:

    bool WndProc(Window* window, Windows::UINT msg, Windows::WPARAM wParam, Windows::LPARAM lParam);
};

/// <summary>
/// Implementation of the mouse device for Windows platform.
/// </summary>
/// <seealso cref="Mouse" />
class WindowsMouse : public Mouse
{
public:

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowsMouse"/> class.
    /// </summary>
    explicit WindowsMouse()
        : Mouse()
    {
    }

public:

    bool WndProc(Window* window, Windows::UINT msg, Windows::WPARAM wParam, Windows::LPARAM lParam);

public:

    // [Mouse]
    void SetMousePosition(const Float2& newPosition) final override
    {
        ::SetCursorPos(static_cast<int32>(newPosition.X), static_cast<int32>(newPosition.Y));

        OnMouseMoved(newPosition);
    }
};

/// <summary>
/// Implementation of the gamepad device for Windows platform.
/// </summary>
/// <seealso cref="Gamepad" />
class WindowsGamepad : public Gamepad
{
private:

    Windows::DWORD _userIndex;

public:

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowsGamepad"/> class.
    /// </summary>
    /// <param name="userIndex">The user index.</param>
    explicit WindowsGamepad(Windows::DWORD userIndex);

    /// <summary>
    /// Finalizes an instance of the <see cref="WindowsGamepad"/> class.
    /// </summary>
    ~WindowsGamepad();

public:

    // [Gamepad]
    void SetVibration(const GamepadVibrationState& state) override;
    Windows::DWORD GetUserIndex() const { return _userIndex; }
protected:

    // [Gamepad]
    bool UpdateState() override;
};

namespace WindowsInputImpl
{
    float XInputLastUpdateTime = 0;
    bool XInputGamepads[XUSER_MAX_COUNT] = { false };
    WindowsMouse* Mouse = nullptr;
    WindowsKeyboard* Keyboard = nullptr;
}

void WindowsInput::Init()
{
    Input::Mouse = WindowsInputImpl::Mouse = New<WindowsMouse>();
    Input::Keyboard = WindowsInputImpl::Keyboard = New<WindowsKeyboard>();
}

void WindowsInput::Update()
{
    const float time = (float)Platform::GetTimeSeconds();
    if (time - WindowsInputImpl::XInputLastUpdateTime < 0.2f)
        return;
    WindowsInputImpl::XInputLastUpdateTime = time;

    PROFILE_CPU_NAMED("Input.ScanGamepads");

    bool hasChanged = false;
    XINPUT_STATE state;

    // 我们要找的是：当前哪个槽位正在产生真实的物理输入？
    int32 activeSlot = -1;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
    {
        if (XInputGetState(i, &state) == ERROR_SUCCESS)
        {
            // 只要有任何按键或摇杆动了，就认定它为当前激活的手柄
            if (state.Gamepad.wButtons != 0 ||
                state.Gamepad.sThumbLX != 0 || state.Gamepad.sThumbLY != 0 ||
                state.Gamepad.sThumbRX != 0 || state.Gamepad.sThumbRY != 0 ||
                state.Gamepad.bLeftTrigger != 0 || state.Gamepad.bRightTrigger != 0)
            {
                activeSlot = i;
                break; // 找到正在动的手柄，立刻锁定
            }
        }
    }

    // 如果当前没有任何手柄在动，但列表里有手柄，我们需要检查它们是否断开
    if (activeSlot == -1)
    {
        for (int32 i = Input::Gamepads.Count() - 1; i >= 0; i--)
        {
            auto gp = static_cast<WindowsGamepad*>(Input::Gamepads[i]);
            // 如果它对应的系统槽位无法获取状态，说明被拔了
            if (XInputGetState(gp->GetUserIndex(), &state) != ERROR_SUCCESS)
            {
                Input::Gamepads.RemoveAt(i);
                Delete(gp);
                WindowsInputImpl::XInputGamepads[gp->GetUserIndex()] = false;
                hasChanged = true;
            }
        }
    }
    else
    {
        // 如果找到了一个正在动的手柄！
        // 我们强行把它伪装成 Slot 0 注册进引擎！
        if (Input::Gamepads.Count() > 0)
        {
            auto gp = static_cast<WindowsGamepad*>(Input::Gamepads[0]);
            // 如果当前 Gamepads[0] 不是这个正在动的手柄，直接干掉重建
            if (gp->GetUserIndex() != activeSlot)
            {
                for (int32 i = 0; i < Input::Gamepads.Count(); i++)
                {
                    auto oldGp = static_cast<WindowsGamepad*>(Input::Gamepads[i]);
                    WindowsInputImpl::XInputGamepads[oldGp->GetUserIndex()] = false;
                    Delete(oldGp);
                }
                Input::Gamepads.Clear();
                hasChanged = true;
            }
        }
        else
        {
            hasChanged = true;
        }

        if (hasChanged)
        {
            // 强行将当前有输入的物理槽位，注册为 Gamepads[0]
            Input::Gamepads.Add(New<WindowsGamepad>(activeSlot));
            WindowsInputImpl::XInputGamepads[activeSlot] = true;
        }
    }

    if (hasChanged)
    {
        Input::OnGamepadsChanged();
    }
}

bool WindowsInput::WndProc(Window* window, Windows::UINT msg, Windows::WPARAM wParam, Windows::LPARAM lParam)
{
    if (WindowsInputImpl::Mouse->WndProc(window, msg, wParam, lParam))
        return true;
    if (WindowsInputImpl::Keyboard->WndProc(window, msg, wParam, lParam))
        return true;
    return false;
}

bool WindowsKeyboard::WndProc(Window* window, const Windows::UINT msg, Windows::WPARAM wParam, Windows::LPARAM lParam)
{
    bool result = false;

    // Handle keyboard related messages
    switch (msg)
    {
    case WM_CHAR:
    {
        OnCharInput(static_cast<Char>(wParam), window);
        result = true;
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        OnKeyDown(static_cast<KeyboardKeys>(wParam), window);
        result = true;
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        OnKeyUp(static_cast<KeyboardKeys>(wParam), window);
        result = true;
        break;
    }
    }

    return result;
}

bool WindowsMouse::WndProc(Window* window, const UINT msg, WPARAM wParam, LPARAM lParam)
{
    bool result = false;

    // Get mouse position in screen coordinates
    POINT p;
    p.x = static_cast<LONG>(WINDOWS_GET_X_LPARAM(lParam));
    p.y = static_cast<LONG>(WINDOWS_GET_Y_LPARAM(lParam));
    const Float2 mousePosScreen(static_cast<float>(p.x), static_cast<float>(p.y));
    ::ClientToScreen(window->GetHWND(), &p);
    const Float2 mousePos(static_cast<float>(p.x), static_cast<float>(p.y));

    // Handle mouse related messages
    switch (msg)
    {
    case WM_MOUSEMOVE:
    {
        OnMouseMove(mousePos, window);
        result = true;
        break;
    }    
    case WM_NCMOUSEMOVE:
    {
        // The position in the message is already reported in screen-space
        OnMouseMove(mousePosScreen, window);
        result = true;
        break;
    }
    case WM_MOUSELEAVE:
    {
        OnMouseLeave(window);
        result = true;
        break;
    }
    case WM_LBUTTONDOWN:
    {
        OnMouseDown(mousePos, MouseButton::Left, window);
        result = true;
        break;
    }
    case WM_RBUTTONDOWN:
    {
        OnMouseDown(mousePos, MouseButton::Right, window);
        result = true;
        break;
    }
    case WM_MBUTTONDOWN:
    {
        OnMouseDown(mousePos, MouseButton::Middle, window);
        result = true;
        break;
    }
    case WM_XBUTTONDOWN:
    {
        const auto button = (HIWORD(wParam) & XBUTTON1) ? MouseButton::Extended1 : MouseButton::Extended2;
        OnMouseDown(mousePos, button, window);
        result = true;
        break;
    }
    case WM_LBUTTONUP:
    {
        OnMouseUp(mousePos, MouseButton::Left, window);
        result = true;
        break;
    }
    case WM_RBUTTONUP:
    {
        OnMouseUp(mousePos, MouseButton::Right, window);
        result = true;
        break;
    }
    case WM_MBUTTONUP:
    {
        OnMouseUp(mousePos, MouseButton::Middle, window);
        result = true;
        break;
    }
    case WM_XBUTTONUP:
    {
        const auto button = (HIWORD(wParam) & XBUTTON1) ? MouseButton::Extended1 : MouseButton::Extended2;
        OnMouseUp(mousePos, button, window);
        result = true;
        break;
    }
    case WM_LBUTTONDBLCLK:
    {
        if (!Input::Mouse->IsRelative())
            OnMouseDoubleClick(mousePos, MouseButton::Left, window);
        else
            OnMouseDown(mousePos, MouseButton::Left, window);
        result = true;
        break;
    }
    case WM_RBUTTONDBLCLK:
    {
        if (!Input::Mouse->IsRelative())
            OnMouseDoubleClick(mousePos, MouseButton::Right, window);
        else
            OnMouseDown(mousePos, MouseButton::Right, window);
        result = true;
        break;
    }
    case WM_MBUTTONDBLCLK:
    {
        if (!Input::Mouse->IsRelative())
            OnMouseDoubleClick(mousePos, MouseButton::Middle, window);
        else
            OnMouseDown(mousePos, MouseButton::Middle, window);
        result = true;
        break;
    }
    case WM_XBUTTONDBLCLK:
    {
        const auto button = (HIWORD(wParam) & XBUTTON1) ? MouseButton::Extended1 : MouseButton::Extended2;
        if (!Input::Mouse->IsRelative())
            OnMouseDoubleClick(mousePos, button, window);
        else
            OnMouseDown(mousePos, button, window);
        result = true;
        break;
    }
    case WM_MOUSEWHEEL:
    {
        const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta != 0)
        {
            const float deltaNormalized = static_cast<float>(delta) / WHEEL_DELTA;
            // Use cached mouse position, the input pos is sometimes wrong in Win32
            OnMouseWheel(_state.MousePosition, deltaNormalized, window);
        }
        result = true;
        break;
    }
    }

    return result;
}

float NormalizeXInputAxis(const int16 axisVal)
{
    // Normalize [-32768..32767] -> [-1..1]
    const float norm = axisVal <= 0 ? 32768.0f : 32767.0f;
    return float(axisVal) / norm;
}

WindowsGamepad::WindowsGamepad(Windows::DWORD userIndex)
    : Gamepad(Guid(0, 0, 0, userIndex), TEXT("XInput Gamepad"))
    , _userIndex(userIndex)
{
    WindowsInputImpl::XInputGamepads[_userIndex] = true;
}

WindowsGamepad::~WindowsGamepad()
{
    WindowsInputImpl::XInputGamepads[_userIndex] = false;
}

void WindowsGamepad::SetVibration(const GamepadVibrationState& state)
{
    Gamepad::SetVibration(state);

    XINPUT_VIBRATION xInputVibration;
    const float leftMotor = Math::Saturate((state.LeftLarge + state.LeftSmall) * 0.5f);
    const float rightMotor = Math::Saturate((state.RightLarge + state.RightSmall) * 0.5f);
    xInputVibration.wLeftMotorSpeed = static_cast<WORD>(leftMotor * 65535.0f);
    xInputVibration.wRightMotorSpeed = static_cast<WORD>(rightMotor * 65535.0f);
    XInputSetState(_userIndex, &xInputVibration);
}

bool WindowsGamepad::UpdateState()
{
    // Gather XInput device state
    XINPUT_STATE inputState;
    if (XInputGetState(_userIndex, &inputState) != ERROR_SUCCESS)
    {
        return true;
    }

    // Process buttons state
    _state.Buttons[(int32)GamepadButton::A] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_A);
    _state.Buttons[(int32)GamepadButton::B] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_B);
    _state.Buttons[(int32)GamepadButton::X] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_X);
    _state.Buttons[(int32)GamepadButton::Y] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_Y);
    _state.Buttons[(int32)GamepadButton::LeftShoulder] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
    _state.Buttons[(int32)GamepadButton::RightShoulder] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
    _state.Buttons[(int32)GamepadButton::Back] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_BACK);
    _state.Buttons[(int32)GamepadButton::Start] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_START);
    _state.Buttons[(int32)GamepadButton::LeftThumb] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
    _state.Buttons[(int32)GamepadButton::RightThumb] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
    _state.Buttons[(int32)GamepadButton::LeftTrigger] = inputState.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    _state.Buttons[(int32)GamepadButton::RightTrigger] = inputState.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    _state.Buttons[(int32)GamepadButton::DPadUp] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP);
    _state.Buttons[(int32)GamepadButton::DPadDown] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
    _state.Buttons[(int32)GamepadButton::DPadLeft] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
    _state.Buttons[(int32)GamepadButton::DPadRight] = !!(inputState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
    _state.Buttons[(int32)GamepadButton::LeftStickUp] = inputState.Gamepad.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    _state.Buttons[(int32)GamepadButton::LeftStickDown] = inputState.Gamepad.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    _state.Buttons[(int32)GamepadButton::LeftStickLeft] = inputState.Gamepad.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    _state.Buttons[(int32)GamepadButton::LeftStickRight] = inputState.Gamepad.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    _state.Buttons[(int32)GamepadButton::RightStickUp] = inputState.Gamepad.sThumbRY > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
    _state.Buttons[(int32)GamepadButton::RightStickDown] = inputState.Gamepad.sThumbRY < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
    _state.Buttons[(int32)GamepadButton::RightStickLeft] = inputState.Gamepad.sThumbRX < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
    _state.Buttons[(int32)GamepadButton::RightStickRight] = inputState.Gamepad.sThumbRX > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;

    // Process axes state
    _state.Axis[(int32)GamepadAxis::LeftStickX] = NormalizeXInputAxis(inputState.Gamepad.sThumbLX);
    _state.Axis[(int32)GamepadAxis::LeftStickY] = NormalizeXInputAxis(inputState.Gamepad.sThumbLY);
    _state.Axis[(int32)GamepadAxis::RightStickX] = NormalizeXInputAxis(inputState.Gamepad.sThumbRX);
    _state.Axis[(int32)GamepadAxis::RightStickY] = NormalizeXInputAxis(inputState.Gamepad.sThumbRY);
    _state.Axis[(int32)GamepadAxis::LeftTrigger] = inputState.Gamepad.bLeftTrigger / 255.0f;
    _state.Axis[(int32)GamepadAxis::RightTrigger] = inputState.Gamepad.bRightTrigger / 255.0f;

    return false;
}

#endif
