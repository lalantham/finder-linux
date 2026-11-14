#include "TerminalPanel.h"
#include "DesktopAppsManager.h"
#include "ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QScrollBar>
#include <QFontDatabase>
#include <QRegularExpression>

TerminalPanel::TerminalPanel(QWidget* parent)
    : QWidget(parent)
    , m_currentDirectory(QDir::homePath())
{
    setupUi();

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &TerminalPanel::onProcessReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &TerminalPanel::onProcessReadyReadStandardError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalPanel::onProcessFinished);

    updatePrompt();
}

TerminalPanel::~TerminalPanel()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

void TerminalPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header bar
    auto* headerBar = new QWidget(this);
    headerBar->setFixedHeight(30);
    headerBar->setStyleSheet(QStringLiteral("background-color: #252526; border-top: 1px solid #3c3c3c; border-bottom: 1px solid #3c3c3c;"));

    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(8, 0, 8, 0);
    headerLayout->setSpacing(8);

    m_titleLabel = new QLabel(QStringLiteral("Terminal (F4)"), headerBar);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(9);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(QStringLiteral("color: #d4d4d4;"));
    headerLayout->addWidget(m_titleLabel);

    m_pathLabel = new QLabel(headerBar);
    m_pathLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    m_pathLabel->setText(m_currentDirectory);
    headerLayout->addWidget(m_pathLabel);

    headerLayout->addStretch();

    m_externalBtn = new QPushButton(QStringLiteral("External"), headerBar);
    m_externalBtn->setFixedHeight(22);
    m_externalBtn->setToolTip(QStringLiteral("Open current directory in external terminal"));
    m_externalBtn->setStyleSheet(QStringLiteral("QPushButton { background: #333333; color: #cccccc; border: 1px solid #444444; border-radius: 3px; padding: 2px 8px; font-size: 11px; } QPushButton:hover { background: #444444; color: #ffffff; }"));
    connect(m_externalBtn, &QPushButton::clicked, this, &TerminalPanel::onOpenExternalClicked);
    headerLayout->addWidget(m_externalBtn);

    m_clearBtn = new QPushButton(QStringLiteral("Clear"), headerBar);
    m_clearBtn->setFixedHeight(22);
    m_clearBtn->setToolTip(QStringLiteral("Clear terminal output (Ctrl+L)"));
    m_clearBtn->setStyleSheet(QStringLiteral("QPushButton { background: #333333; color: #cccccc; border: 1px solid #444444; border-radius: 3px; padding: 2px 8px; font-size: 11px; } QPushButton:hover { background: #444444; color: #ffffff; }"));
    connect(m_clearBtn, &QPushButton::clicked, this, &TerminalPanel::onClearClicked);
    headerLayout->addWidget(m_clearBtn);

    m_closeBtn = new QPushButton(QStringLiteral("✕"), headerBar);
    m_closeBtn->setFixedSize(22, 22);
    m_closeBtn->setToolTip(QStringLiteral("Hide Terminal Panel (F4)"));
    m_closeBtn->setStyleSheet(QStringLiteral("QPushButton { background: transparent; color: #888888; border: none; font-size: 12px; } QPushButton:hover { color: #ffffff; background: #c42b1c; border-radius: 3px; }"));
    connect(m_closeBtn, &QPushButton::clicked, this, &TerminalPanel::closeRequested);
    headerLayout->addWidget(m_closeBtn);

    mainLayout->addWidget(headerBar);

    // Output area
    m_outputView = new QTextEdit(this);
    m_outputView->setReadOnly(true);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(10);
    m_outputView->setFont(monoFont);
    m_outputView->setStyleSheet(QStringLiteral(
        "QTextEdit { background-color: #1e1e1e; color: #d4d4d4; border: none; selection-background-color: #264f78; }"
        "QScrollBar:vertical { background: #1e1e1e; width: 10px; }"
        "QScrollBar::handle:vertical { background: #424242; min-height: 20px; border-radius: 4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    ));
    mainLayout->addWidget(m_outputView, 1);

    // Input prompt row
    auto* inputRow = new QWidget(this);
    inputRow->setFixedHeight(28);
    inputRow->setStyleSheet(QStringLiteral("background-color: #1e1e1e; border-top: 1px solid #2d2d2d;"));

    auto* inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(6, 2, 6, 2);
    inputLayout->setSpacing(6);

    m_promptLabel = new QLabel(inputRow);
    m_promptLabel->setFont(monoFont);
    m_promptLabel->setStyleSheet(QStringLiteral("color: #4ec9b0;"));
    inputLayout->addWidget(m_promptLabel);

    m_inputEdit = new QLineEdit(inputRow);
    m_inputEdit->setFont(monoFont);
    m_inputEdit->setFrame(false);
    m_inputEdit->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; color: #ffffff; border: none; }"));
    m_inputEdit->installEventFilter(this);
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &TerminalPanel::onCommandEntered);
    inputLayout->addWidget(m_inputEdit, 1);

    mainLayout->addWidget(inputRow);
}

void TerminalPanel::setCurrentDirectory(const QString& path)
{
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")) || path.startsWith(QStringLiteral("trash://"))) {
        return;
    }

    if (m_currentDirectory != path) {
        m_currentDirectory = path;
        updatePrompt();
    }
}

void TerminalPanel::focusInput()
{
    if (m_inputEdit) {
        m_inputEdit->setFocus();
    }
}

void TerminalPanel::updatePrompt()
{
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

bool TerminalPanel::eventFilter(QObject* watched, QEvent* event)
{
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
            QStringList entries = targetDir.entryList({searchPrefix + QStringLiteral("*")}, QDir::AllEntries | QDir::NoDotAndDotDot);
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
                    m_process->kill();
                    m_outputView->append(QStringLiteral("^C"));
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

void TerminalPanel::onCommandEntered()
{
    QString cmd = m_inputEdit->text().trimmed();
    m_inputEdit->clear();
    m_historyIndex = -1;

    if (cmd.isEmpty()) {
        return;
    }

    m_history.append(cmd);

    // Print command invocation to output
    QString prompt = m_promptLabel->text();
    m_outputView->append(QStringLiteral("<span style='color: #4ec9b0;'>%1</span> <span style='color: #ffffff;'><b>%2</b></span>")
                         .arg(prompt.toHtmlEscaped(), cmd.toHtmlEscaped()));

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

void TerminalPanel::handleCdCommand(const QString& argument)
{
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
        m_outputView->append(QStringLiteral("<span style='color: #f14c4c;'>cd: no such file or directory: %1</span>").arg(argument.toHtmlEscaped()));
    }
}

void TerminalPanel::executeExternalCommand(const QString& command)
{
    if (m_process->state() != QProcess::NotRunning) {
        m_outputView->append(QStringLiteral("<span style='color: #cca700;'>A command is already running. Press Ctrl+C to cancel.</span>"));
        return;
    }

    m_process->setWorkingDirectory(m_currentDirectory);
    m_process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

void TerminalPanel::onProcessReadyReadStandardOutput()
{
    appendAnsiText(m_process->readAllStandardOutput());
}

void TerminalPanel::onProcessReadyReadStandardError()
{
    appendAnsiText(m_process->readAllStandardError());
}

void TerminalPanel::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
}

void TerminalPanel::appendAnsiText(const QByteArray& data)
{
    QString text = QString::fromUtf8(data);

    // Simple ANSI SGR escape code to HTML formatting
    static const QRegularExpression ansiRegex(QStringLiteral("\x1b\\[[0-9;]*m"));
    QString html = text.toHtmlEscaped();

    // Map common ANSI colors
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[0?m")), QStringLiteral("</span>"));
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[1m")), QStringLiteral("<span style='font-weight:bold;'>"));
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[31m")), QStringLiteral("<span style='color:#f14c4c;'>"));
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[32m")), QStringLiteral("<span style='color:#73c991;'>"));
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[33m")), QStringLiteral("<span style='color:#cca700;'>"));
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[34m")), QStringLiteral("<span style='color:#3794ff;'>"));
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[35m")), QStringLiteral("<span style='color:#bc3fbc;'>"));
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[36m")), QStringLiteral("<span style='color:#4ec9b0;'>"));
    html.replace(QRegularExpression(QStringLiteral("\x1b\\[37m")), QStringLiteral("<span style='color:#d4d4d4;'>"));
    html.remove(ansiRegex); // Remove any remaining unhandled escape codes

    html.replace(QLatin1Char('\n'), QStringLiteral("<br>"));

    QTextCursor cursor = m_outputView->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);
    m_outputView->setTextCursor(cursor);
    m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
}

void TerminalPanel::onClearClicked()
{
    m_outputView->clear();
}

void TerminalPanel::onOpenExternalClicked()
{
    DesktopAppsManager::openTerminal(m_currentDirectory);
}
