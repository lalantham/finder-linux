#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>
#include <QWidget>

class QTextEdit;
class QLineEdit;
class QLabel;
class QPushButton;

class TerminalPanel : public QWidget {
    Q_OBJECT

public:
    explicit TerminalPanel(QWidget* parent = nullptr);
    ~TerminalPanel() override;

    void setCurrentDirectory(const QString& path);
    [[nodiscard]] QString currentDirectory() const noexcept {
        return m_currentDirectory;
    }
    void focusInput();

signals:
    void directoryChanged(const QString& newPath);
    void closeRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onCommandEntered();
    void onProcessReadyReadStandardOutput();
    void onProcessReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onClearClicked();
    void onOpenExternalClicked();

private:
    void setupUi();
    void updatePrompt();
    void applyThemeStyles();
    void appendAnsiText(const QByteArray& data);
    void appendCommandEcho(const QString& prompt, const QString& command);
    void interruptProcess();
    void handleCdCommand(const QString& argument);
    void executeExternalCommand(const QString& command);

    QString m_currentDirectory;
    QProcess* m_process{nullptr};

    QLabel* m_titleLabel{nullptr};
    QLabel* m_pathLabel{nullptr};
    QPushButton* m_externalBtn{nullptr};
    QPushButton* m_clearBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};
    QWidget* m_headerBar{nullptr};
    QWidget* m_inputRow{nullptr};

    QTextEdit* m_outputView{nullptr};
    QLabel* m_promptLabel{nullptr};
    QLineEdit* m_inputEdit{nullptr};

    QStringList m_history;
    int m_historyIndex{-1};
    QString m_savedInput;
    QTimer* m_interruptTimer{nullptr};
    bool m_interruptPending{false};
};
