#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>

enum class SearchScope {
    ThisFolder,
    Everywhere
};

class SearchScopeBar : public QWidget {
    Q_OBJECT

public:
    explicit SearchScopeBar(QWidget* parent = nullptr);

    void setCurrentFolderName(const QString& name);
    [[nodiscard]] const QString& currentFolderName() const noexcept { return m_folderName; }

    void setStatusText(const QString& text);
    void reset();

    [[nodiscard]] SearchScope currentScope() const noexcept { return m_scope; }
    [[nodiscard]] QString currentKind() const;
    [[nodiscard]] bool isSubfolders() const;

signals:
    void scopeChanged(SearchScope scope);
    void kindChanged(const QString& kind);
    void subfoldersToggled(bool subfolders);
    void clearRequested();

public slots:
    void updateThemeStyles(bool isDark);

private slots:
    void onThisFolderClicked();
    void onEverywhereClicked();

private:
    void setupUi();
    void updateScopeButtons();

    SearchScope m_scope{SearchScope::ThisFolder};
    QString m_folderName;
    bool m_isDark{false};

    QLabel* m_labelSearch{nullptr};
    QPushButton* m_thisFolderBtn{nullptr};
    QPushButton* m_everywhereBtn{nullptr};
    QCheckBox* m_subfoldersCb{nullptr};
    QLabel* m_kindLabel{nullptr};
    QComboBox* m_kindCombo{nullptr};
    QLabel* m_statusLabel{nullptr};
    QPushButton* m_clearBtn{nullptr};
};
