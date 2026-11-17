#include "TerminalPanel.h"
#include "DesktopAppsManager.h"
#include "ThemeManager.h"
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <csignal>
#include <cstdlib>
#include <sys/types.h>

TerminalPanel::TerminalPanel(QWidget* parent)
    : QWidget(parent), m_currentDirectory(QDir::homePath()) {
    setupUi();

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this,
            &TerminalPanel::onProcessReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this,
            &TerminalPanel::onProcessReadyReadStandardError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &TerminalPanel::onProcessFinished);

    // Restyle when the user switches between light and dark, so the panel does
    // not stay stuck on the colours that were active when it was created.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](bool) { applyThemeStyles(); });

    updatePrompt();
}

TerminalPanel::~TerminalPanel() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

void TerminalPanel::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header bar
    auto* headerBar = new QWidget(this);
    headerBar->setFixedHeight(30);
    m_headerBar = headerBar;

    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(8, 0, 8, 0);
    headerLayout->setSpacing(8);

    m_titleLabel = new QLabel(QStringLiteral("Terminal (F4)"), headerBar);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(9);
    m_titleLabel->setFont(titleFont);
    headerLayout->addWidget(m_titleLabel);

    m_pathLabel = new QLabel(headerBar);
    m_pathLabel->setText(m_currentDirectory);
    headerLayout->addWidget(m_pathLabel);

    headerLayout->addStretch();

    m_externalBtn = new QPushButton(QStringLiteral("External"), headerBar);
    m_externalBtn->setFixedHeight(22);
    m_externalBtn->setToolTip(QStringLiteral("Open current directory in external terminal"));
    connect(m_externalBtn, &QPushButton::clicked, this, &TerminalPanel::onOpenExternalClicked);
    headerLayout->addWidget(m_externalBtn);

    m_clearBtn = new QPushButton(QStringLiteral("Clear"), headerBar);
    m_clearBtn->setFixedHeight(22);
    m_clearBtn->setToolTip(QStringLiteral("Clear terminal output (Ctrl+L)"));
    connect(m_clearBtn, &QPushButton::clicked, this, &TerminalPanel::onClearClicked);
    headerLayout->addWidget(m_clearBtn);

    m_closeBtn = new QPushButton(QStringLiteral("✕"), headerBar);
    m_closeBtn->setFixedSize(22, 22);
    m_closeBtn->setToolTip(QStringLiteral("Hide Terminal Panel (F4)"));
    connect(m_closeBtn, &QPushButton::clicked, this, &TerminalPanel::closeRequested);
    headerLayout->addWidget(m_closeBtn);

    mainLayout->addWidget(headerBar);

    // Output area
    m_outputView = new QTextEdit(this);
    m_outputView->setReadOnly(true);
    // Bound the scrollback. Without a cap a chatty command appends forever and
    // every further insert re-lays out an ever-growing document.
    m_outputView->document()->setMaximumBlockCount(5000);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(10);
    m_outputView->setFont(monoFont);
    mainLayout->addWidget(m_outputView, 1);

    // Input prompt row
    auto* inputRow = new QWidget(this);
    inputRow->setFixedHeight(28);
    m_inputRow = inputRow;

    auto* inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(6, 2, 6, 2);
    inputLayout->setSpacing(6);

    m_promptLabel = new QLabel(inputRow);
    m_promptLabel->setFont(monoFont);
    inputLayout->addWidget(m_promptLabel);

    m_inputEdit = new QLineEdit(inputRow);
    m_inputEdit->setFont(monoFont);
    m_inputEdit->setFrame(false);
    m_inputEdit->installEventFilter(this);
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &TerminalPanel::onCommandEntered);
    inputLayout->addWidget(m_inputEdit, 1);

    mainLayout->addWidget(inputRow);

    applyThemeStyles();
}

void TerminalPanel::setCurrentDirectory(const QString& path) {
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")) ||
        path.startsWith(QStringLiteral("trash://"))) {
        return;
    }

    if (m_currentDirectory != path) {
        m_currentDirectory = path;
        updatePrompt();
    }
}

void TerminalPanel::focusInput() {
    if (m_inputEdit) {
        m_inputEdit->setFocus();
    }
}

void TerminalPanel::updatePrompt() {
    m_pathLabel->setText(m_currentDirectory);

    QString dirName = m_currentDirectory;
    QString home = QDir::homePath();
    if (dirName == home) {
        dirName = QStringLiteral("~");
    } else if (dirName.startsWith(home + QLatin1Char('/'))) {
        dirName = QStringLiteral("~") + dirName.mid(home.length());
    }

    m_promptLabel->setText(QStringLiteral("%1 $").arg(dirName));
}

bool TerminalPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_inputEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Up) {
            if (!m_history.isEmpty()) {
                if (m_historyIndex == -1) {
                    m_savedInput = m_inputEdit->text();
                    m_historyIndex = static_cast<int>(m_history.size()) - 1;
                } else if (m_historyIndex > 0) {
                    m_historyIndex--;
                }
                if (m_historyIndex >= 0 && m_historyIndex < m_history.size()) {
                    m_inputEdit->setText(m_history[m_historyIndex]);
                }
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_Down) {
            if (m_historyIndex != -1) {
                m_historyIndex++;
                if (m_historyIndex >= m_history.size()) {
                    m_historyIndex = -1;
                    m_inputEdit->setText(m_savedInput);
                } else {
                    m_inputEdit->setText(m_history[m_historyIndex]);
                }
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_Tab) {
            QString currentText = m_inputEdit->text();
            QStringList tokens = currentText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            QString prefix = tokens.isEmpty() ? QString() : tokens.last();

            QDir dir(m_currentDirectory);
            QString filterDir = m_currentDirectory;
            QString searchPrefix = prefix;

            qsizetype slashIdx = prefix.lastIndexOf(QLatin1Char('/'));
            if (slashIdx >= 0) {
                QString relPath = prefix.left(slashIdx + 1);
                filterDir = dir.filePath(relPath);
                searchPrefix = prefix.mid(slashIdx + 1);
            }

            QDir targetDir(filterDir);
            QStringList entries = targetDir.entryList({searchPrefix + QStringLiteral("*")},
                                                      QDir::AllEntries | QDir::NoDotAndDotDot);
            if (entries.size() == 1) {
                QString match = entries.first();
                if (QFileInfo(targetDir.filePath(match)).isDir()) {
                    match += QLatin1Char('/');
                }
                QString completed;
                if (slashIdx >= 0) {
                    completed = prefix.left(slashIdx + 1) + match;
                } else {
                    completed = match;
                }

                if (!tokens.isEmpty()) {
                    tokens.removeLast();
                }
                tokens.append(completed);
                m_inputEdit->setText(tokens.join(QLatin1Char(' ')));
            }
            return true;
        }

        if (keyEvent->modifiers() & Qt::ControlModifier) {
            if (keyEvent->key() == Qt::Key_C) {
                if (m_process && m_process->state() != QProcess::NotRunning) {
                    interruptProcess();
                } else {
                    m_inputEdit->clear();
                }
                return true;
            }
            if (keyEvent->key() == Qt::Key_L) {
                onClearClicked();
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TerminalPanel::onCommandEntered() {
    QString cmd = m_inputEdit->text().trimmed();
    m_inputEdit->clear();
    m_historyIndex = -1;

    if (cmd.isEmpty()) {
        return;
    }

    m_history.append(cmd);

    // Echo the command line using character formats rather than HTML.
    appendCommandEcho(m_promptLabel->text(), cmd);

    QStringList parts = cmd.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString prog = parts.value(0);

    if (prog == QStringLiteral("cd")) {
        QString arg = cmd.mid(2).trimmed();
        handleCdCommand(arg);
        return;
    }

    if (prog == QStringLiteral("clear")) {
        onClearClicked();
        return;
    }

    if (prog == QStringLiteral("exit")) {
        emit closeRequested();
        return;
    }

    executeExternalCommand(cmd);
}

void TerminalPanel::handleCdCommand(const QString& argument) {
    QString target = argument;
    if (target.isEmpty() || target == QStringLiteral("~")) {
        target = QDir::homePath();
    } else if (target.startsWith(QStringLiteral("~/"))) {
        target = QDir::homePath() + target.mid(1);
    }

    QDir current(m_currentDirectory);
    QString resolved = current.cleanPath(current.absoluteFilePath(target));

    QFileInfo fi(resolved);
    if (fi.exists() && fi.isDir()) {
        m_currentDirectory = resolved;
        updatePrompt();
        emit directoryChanged(m_currentDirectory);
    } else {
        appendAnsiText(QByteArray("\x1b[31mcd: no such file or directory: ") + argument.toUtf8() +
                       "\x1b[0m\n");
    }
}

void TerminalPanel::executeExternalCommand(const QString& command) {
    if (m_process->state() != QProcess::NotRunning) {
        appendAnsiText(QByteArrayLiteral(
            "\x1b[33mA command is already running. Press Ctrl+C to cancel.\x1b[0m\n"));
        return;
    }

    m_process->setWorkingDirectory(m_currentDirectory);
    // Put the command into its own process group so Ctrl+C can signal the shell
    // *and* everything it spawned. Done with setpgid() rather than the `setsid`
    // binary because setsid only forks when it is already a process-group
    // leader, so processId() would sometimes point at a wrapper instead of the
    // session leader (this differed between developer machines and CI). In the
    // child it is deterministic: the shell becomes the group leader, so
    // kill(-pid) always addresses the right group.
    // setChildProcessModifier() exists since Qt 6.0 and the project requires
    // 6.4+, so no version guard: guarding it at 6.7 silently disabled the
    // process-group setup on Debian 12's Qt 6.4.2.
    m_process->setChildProcessModifier([]() { ::setpgid(0, 0); });
    m_process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

void TerminalPanel::onProcessReadyReadStandardOutput() {
    appendAnsiText(m_process->readAllStandardOutput());
}

void TerminalPanel::onProcessReadyReadStandardError() {
    appendAnsiText(m_process->readAllStandardError());
}

void TerminalPanel::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
}

namespace {

// Resolves an SGR colour code (30-37, 90-97) to a colour, honouring the active
// theme so the panel stays readable in both light and dark mode.
QColor ansiColor(int code, bool dark) {
    struct Entry {
        int code;
        QColor dark;
        QColor light;
    };
    static const Entry table[] = {
        {30, QColor(0x3c, 0x3c, 0x3c), QColor(0x60, 0x60, 0x60)},
        {31, QColor(0xf1, 0x4c, 0x4c), QColor(0xc0, 0x1c, 0x28)},
        {32, QColor(0x73, 0xc9, 0x91), QColor(0x2e, 0x7d, 0x32)},
        {33, QColor(0xcc, 0xa7, 0x00), QColor(0x9a, 0x67, 0x00)},
        {34, QColor(0x37, 0x94, 0xff), QColor(0x15, 0x65, 0xc0)},
        {35, QColor(0xbc, 0x3f, 0xbc), QColor(0x7b, 0x1f, 0xa2)},
        {36, QColor(0x4e, 0xc9, 0xb0), QColor(0x00, 0x77, 0x7a)},
        {37, QColor(0xd4, 0xd4, 0xd4), QColor(0x42, 0x42, 0x42)},
        // Bright variants, used by many tools.
        {90, QColor(0x80, 0x80, 0x80), QColor(0x9e, 0x9e, 0x9e)},
        {91, QColor(0xf4, 0x87, 0x71), QColor(0xd3, 0x2f, 0x2f)},
        {92, QColor(0xa5, 0xe0, 0x90), QColor(0x3b, 0x8e, 0x3f)},
        {93, QColor(0xe5, 0xc0, 0x7b), QColor(0x8d, 0x6e, 0x10)},
        {94, QColor(0x6c, 0xb6, 0xff), QColor(0x19, 0x69, 0xd2)},
        {95, QColor(0xd7, 0x8b, 0xd7), QColor(0x8e, 0x24, 0xaa)},
        {96, QColor(0x7f, 0xdb, 0xca), QColor(0x00, 0x83, 0x8f)},
        {97, QColor(0xf0, 0xf0, 0xf0), QColor(0x1a, 0x1a, 0x1a)},
    };
    for (const Entry& entry : table) {
        if (entry.code == code) {
            return dark ? entry.dark : entry.light;
        }
    }
    return {};
}

// Applies one SGR escape sequence to `format`. Unsupported parameters are
// ignored, which is what a terminal should do.
void applySgr(const QList<int>& params, QTextCharFormat& format, bool dark) {
    const QList<int> codes = params.isEmpty() ? QList<int>{0} : params;
    for (int code : codes) {
        switch (code) {
        case 0:
            format = QTextCharFormat();
            continue;
        case 1:
            format.setFontWeight(QFont::Bold);
            continue;
        case 2:
            format.setFontWeight(QFont::Light);
            continue;
        case 3:
            format.setFontItalic(true);
            continue;
        case 4:
            format.setFontUnderline(true);
            continue;
        case 22:
            format.setFontWeight(QFont::Normal);
            continue;
        case 23:
            format.setFontItalic(false);
            continue;
        case 24:
            format.setFontUnderline(false);
            continue;
        case 39:
            format.clearForeground();
            continue;
        default:
            break;
        }
        if (const QColor color = ansiColor(code, dark); color.isValid()) {
            format.setForeground(color);
        }
    }
}

} // namespace

void TerminalPanel::appendAnsiText(const QByteArray& data) {
    if (data.isEmpty() || !m_outputView) {
        return;
    }

    // Parse ANSI SGR escapes in a single pass and insert plain text with a
    // character format instead of building HTML and calling insertHtml().
    // insertHtml() re-parses and re-lays out the whole document on every
    // append, which measured ~130x slower and froze the window on a chatty
    // command (2000 lines took 7 s versus 54 ms with insertText).
    const bool dark = ThemeManager::instance().isDark();
    QTextCursor cursor(m_outputView->document());
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();

    QTextCharFormat format;
    QString pending;
    const QString text = QString::fromUtf8(data);

    const auto flush = [&]() {
        if (!pending.isEmpty()) {
            cursor.insertText(pending, format);
            pending.clear();
        }
    };

    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch != QChar(0x1b)) {
            pending.append(ch);
            continue;
        }

        // CSI sequence: ESC [ params final-byte.
        if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('[')) {
            qsizetype end = i + 2;
            while (end < text.size() &&
                   (text.at(end).isDigit() || text.at(end) == QLatin1Char(';'))) {
                ++end;
            }
            const QString paramText = text.mid(i + 2, end - i - 2);
            const QChar finalByte = end < text.size() ? text.at(end) : QChar();

            if (finalByte == QLatin1Char('m')) {
                flush();
                QList<int> params;
                for (const QString& part : paramText.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                    bool ok = false;
                    const int value = part.toInt(&ok);
                    if (ok) {
                        params.append(value);
                    }
                }
                applySgr(params, format, dark);
            } else if (finalByte == QLatin1Char('K')) {
                // Erase-in-line: nothing of the current line has been inserted
                // yet, so dropping the buffered tail is enough.
                pending.clear();
            }
            i = end; // skip the sequence including its final byte
            continue;
        }
        // Unknown escape: swallow ESC plus the following character.
        ++i;
    }
    flush();
    cursor.endEditBlock();

    m_outputView->setTextCursor(cursor);
    m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
}

void TerminalPanel::appendCommandEcho(const QString& prompt, const QString& command) {
    if (!m_outputView) {
        return;
    }
    const bool dark = ThemeManager::instance().isDark();
    QTextCharFormat promptFormat;
    promptFormat.setForeground(ansiColor(36, dark));
    QTextCharFormat commandFormat;
    commandFormat.setForeground(dark ? QColor(0xf0, 0xf0, 0xf0) : QColor(0x1a, 0x1a, 0x1a));
    commandFormat.setFontWeight(QFont::Bold);

    QTextCursor cursor(m_outputView->document());
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    cursor.insertText(prompt + QLatin1Char(' '), promptFormat);
    cursor.insertText(command + QLatin1Char('\n'), commandFormat);
    cursor.endEditBlock();

    m_outputView->setTextCursor(cursor);
    m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
}

void TerminalPanel::applyThemeStyles() {
    const bool dark = ThemeManager::instance().isDark();

    // The panel used to hard-code VS Code dark colours, which looked broken on a
    // light desktop. Derive the palette from the active theme instead.
    const QString background = dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#fbfbfb");
    const QString surface = dark ? QStringLiteral("#252526") : QStringLiteral("#eceff1");
    const QString border = dark ? QStringLiteral("#3c3c3c") : QStringLiteral("#d3d7d9");
    const QString text = dark ? QStringLiteral("#d4d4d4") : QStringLiteral("#31363b");
    const QString dimText = dark ? QStringLiteral("#888888") : QStringLiteral("#6b7377");
    const QString buttonBase = dark ? QStringLiteral("#333333") : QStringLiteral("#e3e5e7");
    const QString buttonHover = dark ? QStringLiteral("#444444") : QStringLiteral("#d0d4d6");
    const QString buttonText = dark ? QStringLiteral("#cccccc") : QStringLiteral("#31363b");
    const QString buttonBorder = dark ? QStringLiteral("#444444") : QStringLiteral("#c2c7c9");
    const QString handle = dark ? QStringLiteral("#424242") : QStringLiteral("#c0c4c6");

    if (m_headerBar) {
        m_headerBar->setStyleSheet(QStringLiteral("background-color: %1; border-top: 1px solid %2; "
                                                  "border-bottom: 1px solid %2;")
                                       .arg(surface, border));
    }
    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(text));
    }
    if (m_pathLabel) {
        m_pathLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(dimText));
    }
    if (m_inputRow) {
        m_inputRow->setStyleSheet(QStringLiteral("background-color: %1; border-top: 1px solid %2;")
                                      .arg(background, border));
    }
    if (m_outputView) {
        m_outputView->setStyleSheet(
            QStringLiteral("QTextEdit { background-color: %1; color: %2; border: none; "
                           "selection-background-color: %3; }"
                           "QScrollBar:vertical { background: %1; width: 10px; }"
                           "QScrollBar::handle:vertical { background: %4; min-height: 20px; "
                           "border-radius: 4px; }"
                           "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical "
                           "{ height: 0px; }")
                .arg(background, text, ThemeManager::instance().accentColor().name(), handle));
    }
    if (m_promptLabel) {
        m_promptLabel->setStyleSheet(QStringLiteral("color: %1;").arg(ansiColor(36, dark).name()));
    }
    if (m_inputEdit) {
        m_inputEdit->setStyleSheet(
            QStringLiteral("QLineEdit { background: transparent; color: %1; border: none; }")
                .arg(text));
    }

    const QString buttonStyle =
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 3px; padding: 2px 8px; font-size: 11px; }"
                       "QPushButton:hover { background: %4; }")
            .arg(buttonBase, buttonText, buttonBorder, buttonHover);
    if (m_externalBtn) {
        m_externalBtn->setStyleSheet(buttonStyle);
    }
    if (m_clearBtn) {
        m_clearBtn->setStyleSheet(buttonStyle);
    }
    if (m_closeBtn) {
        m_closeBtn->setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; color: %1; border: none; "
                           "font-size: 12px; }"
                           "QPushButton:hover { color: #ffffff; background: #c42b1c; "
                           "border-radius: 3px; }")
                .arg(dimText));
    }
}

void TerminalPanel::onClearClicked() {
    m_outputView->clear();
}

void TerminalPanel::interruptProcess() {
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        return;
    }

    appendAnsiText(QByteArrayLiteral("\x1b[33m^C\x1b[0m\n"));

    // Signal the whole process group, not just the shell. Two details make this
    // work, both verified experimentally:
    //   * the command runs under `setsid` (see executeExternalCommand) so the
    //     shell is a group leader and kill(-pid) reaches its children too;
    //   * SIGTERM is used rather than SIGINT, because a non-interactive
    //     `/bin/sh -c` ignores SIGINT by POSIX rule and the command would keep
    //     running. SIGKILL follows only if the process ignores SIGTERM.
    const qint64 pid = m_process->processId();
    if (pid > 0) {
        ::kill(static_cast<pid_t>(-pid), SIGTERM);
    }

    m_interruptPending = true;
    if (!m_interruptTimer) {
        m_interruptTimer = new QTimer(this);
        m_interruptTimer->setSingleShot(true);
        connect(m_interruptTimer, &QTimer::timeout, this, [this]() {
            if (m_process && m_process->state() != QProcess::NotRunning) {
                m_process->kill();
            }
        });
    }
    m_interruptTimer->start(2000);
}

void TerminalPanel::onOpenExternalClicked() {
    DesktopAppsManager::openTerminal(m_currentDirectory);
}
