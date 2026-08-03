#pragma once

#include "TuiRenderer.h"

#include <QString>

#include <optional>

enum class TuiKeyKind {
    Character,
    Left,
    Right,
    Up,
    Down,
    PagePrevious,
    PageNext,
    ScrollUp,
    ScrollDown,
    Enter,
    Escape,
    Resize
};

struct TuiKey {
    TuiKeyKind kind = TuiKeyKind::Character;
    char32_t character = 0;
};

class NcursesTerminal final {
public:
    NcursesTerminal() = default;
    ~NcursesTerminal();

    NcursesTerminal(const NcursesTerminal &) = delete;
    NcursesTerminal &operator=(const NcursesTerminal &) = delete;

    bool initialize(QString *error);
    void shutdown();
    bool active() const;
    int width() const;
    int height() const;
    std::optional<TuiKey> readKey();
    void handleResize();
    void render(const TuiFrame &frame);

private:
    void *m_screen = nullptr;
    bool m_colors = false;
};
