#include "NcursesTerminal.h"

#include <QByteArray>
#include <QtGlobal>

#include <curses.h>
#include <locale.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace {

enum ColorPair : short {
    AccentPair = 1,
    GoodPair,
    WarningPair,
    ErrorPair,
    DimPair
};

int attributesFor(TuiStyle style, bool colors)
{
    const auto color = [colors](ColorPair pair) {
        return colors ? static_cast<int>(COLOR_PAIR(pair)) : 0;
    };
    switch (style) {
    case TuiStyle::Header:
        return A_BOLD | color(AccentPair);
    case TuiStyle::Accent:
        return A_BOLD | color(AccentPair);
    case TuiStyle::Good:
        return A_BOLD | color(GoodPair);
    case TuiStyle::Warning:
        return A_BOLD | color(WarningPair);
    case TuiStyle::Error:
        return A_BOLD | color(ErrorPair);
    case TuiStyle::Dim:
        return A_DIM | color(DimPair);
    case TuiStyle::Normal:
        return A_NORMAL;
    }
    return A_NORMAL;
}

} // namespace

NcursesTerminal::~NcursesTerminal()
{
    shutdown();
}

void NcursesTerminal::shutdown()
{
    if (!m_screen)
        return;
    set_term(static_cast<SCREEN *>(m_screen));
    endwin();
    delscreen(static_cast<SCREEN *>(m_screen));
    m_screen = nullptr;
}

bool NcursesTerminal::initialize(QString *error)
{
    if (m_screen)
        return true;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        if (error) {
            *error = QStringLiteral(
                "interactive mode needs a terminal on stdin and stdout; use --plain otherwise");
        }
        return false;
    }
    if (!setlocale(LC_ALL, "")) {
        if (error)
            *error = QStringLiteral("unable to activate the process locale");
        return false;
    }
    if (qEnvironmentVariableIsEmpty("ESCDELAY"))
        qputenv("ESCDELAY", QByteArrayLiteral("40"));

    SCREEN *screen = newterm(nullptr, stdout, stdin);
    if (!screen) {
        if (error)
            *error = QStringLiteral("ncurses could not initialize this terminal type");
        return false;
    }
    m_screen = screen;
    set_term(screen);

    if (cbreak() == ERR || noecho() == ERR || keypad(stdscr, TRUE) == ERR
        || nodelay(stdscr, TRUE) == ERR) {
        if (error)
            *error = QStringLiteral("ncurses could not configure terminal input");
        endwin();
        delscreen(screen);
        m_screen = nullptr;
        return false;
    }
    curs_set(0);

    m_colors = has_colors();
    if (m_colors) {
        start_color();
        init_pair(AccentPair, COLOR_CYAN, COLOR_BLACK);
        init_pair(GoodPair, COLOR_GREEN, COLOR_BLACK);
        init_pair(WarningPair, COLOR_YELLOW, COLOR_BLACK);
        init_pair(ErrorPair, COLOR_RED, COLOR_BLACK);
        init_pair(DimPair, COLOR_WHITE, COLOR_BLACK);
    }
    return true;
}

bool NcursesTerminal::active() const
{
    return m_screen != nullptr;
}

int NcursesTerminal::width() const
{
    if (!m_screen)
        return 0;
    int rows = 0;
    int columns = 0;
    getmaxyx(stdscr, rows, columns);
    Q_UNUSED(rows);
    return columns;
}

int NcursesTerminal::height() const
{
    if (!m_screen)
        return 0;
    int rows = 0;
    int columns = 0;
    getmaxyx(stdscr, rows, columns);
    Q_UNUSED(columns);
    return rows;
}

std::optional<TuiKey> NcursesTerminal::readKey()
{
    if (!m_screen)
        return std::nullopt;

    wint_t value = 0;
    const int result = wget_wch(stdscr, &value);
    if (result == ERR)
        return std::nullopt;
    if (result == OK) {
        if (value == 27)
            return TuiKey {TuiKeyKind::Escape, 0};
        if (value == '\n' || value == '\r')
            return TuiKey {TuiKeyKind::Enter, 0};
        if (value == '\t')
            return TuiKey {TuiKeyKind::PageNext, 0};
        return TuiKey {TuiKeyKind::Character, static_cast<char32_t>(value)};
    }

    const int keyCode = static_cast<int>(value);
    switch (keyCode) {
    case KEY_LEFT:
        return TuiKey {TuiKeyKind::Left, 0};
    case KEY_RIGHT:
        return TuiKey {TuiKeyKind::Right, 0};
    case KEY_UP:
        return TuiKey {TuiKeyKind::Up, 0};
    case KEY_DOWN:
        return TuiKey {TuiKeyKind::Down, 0};
    case KEY_PPAGE:
        return TuiKey {TuiKeyKind::ScrollUp, 0};
    case KEY_NPAGE:
        return TuiKey {TuiKeyKind::ScrollDown, 0};
#ifdef KEY_ENTER
    case KEY_ENTER:
        return TuiKey {TuiKeyKind::Enter, 0};
#endif
#ifdef KEY_BTAB
    case KEY_BTAB:
        return TuiKey {TuiKeyKind::PagePrevious, 0};
#endif
#ifdef KEY_RESIZE
    case KEY_RESIZE:
        return TuiKey {TuiKeyKind::Resize, 0};
#endif
    default:
        return std::nullopt;
    }
}

void NcursesTerminal::handleResize()
{
    if (!m_screen)
        return;
    struct winsize size {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0
        && size.ws_row > 0 && size.ws_col > 0) {
        resizeterm(size.ws_row, size.ws_col);
    }
    clearok(stdscr, TRUE);
}

void NcursesTerminal::render(const TuiFrame &frame)
{
    if (!m_screen)
        return;

    werase(stdscr);
    const int rowCount = qMin(height(), static_cast<int>(frame.lines.size()));
    const int columnCount = width();
    for (int row = 0; row < rowCount; ++row) {
        const TuiLine &line = frame.lines.at(row);
        wattrset(stdscr, attributesFor(line.style, m_colors));
        const std::wstring text = line.text.toStdWString();
        mvwaddnwstr(stdscr, row, 0, text.c_str(), columnCount);
    }
    wattrset(stdscr, A_NORMAL);
    wnoutrefresh(stdscr);
    doupdate();
}
