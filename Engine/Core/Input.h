#pragma once

namespace Kosmos
{
    class Window;

    enum class Key
    {
        W,
        A,
        S,
        D,
        Q,
        E,
        LeftShift,
        Digit0,
        Digit1,
        Digit2,
        Digit3,
        Digit4,
        Digit5,
        Digit6,
        Digit7,
        Digit8,
        Digit9
    };

    enum class MouseButton
    {
        Right
    };

    struct CursorPosition
    {
        double x = 0.0;
        double y = 0.0;
    };

    class Input
    {
        public:
            explicit Input(Window& window);

            bool IsKeyDown(Key key) const;
            bool IsMouseButtonDown(MouseButton button) const;
            CursorPosition GetCursorPosition() const;
            void SetCursorCaptured(bool captured) const;

        private:
            Window& m_Window;
    };
}
