#pragma once

#include "SearchQuery.h"
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QWidget>

enum class SearchScope { ThisFolder, ThisPC };

class QLineEdit;
class QToolButton;

class SearchScopeBar : public QWidget {
    Q_OBJECT

public:
    explicit SearchScopeBar(QWidget* parent = nullptr);

    void setCurrentFolderName(const QString& name);
    [[nodiscard]] const QString& currentFolderName() const noexcept {
        return m_folderName;
    }

    void setStatusText(const QString& text);
    void setFilterSummary(const QString& text);
    void setSaveSearchEnabled(bool enabled);
    void setIndexProvider(const QString& provider);
    void setFromFilter(const SearchFilter& filter);
    void reset();

    void setChips(const QVector<SearchQuery::Chip>& chips);
    void setRefineVisible(bool visible);
    void clearRefine();
    [[nodiscard]] QString refineText() const;
    void setLiveEnabled(bool enabled);
    void setLiveVisible(bool visible);
    [[nodiscard]] bool isLiveEnabled() const;
    void setSortMode(const QString& mode);
    [[nodiscard]] QString sortMode() const;

    [[nodiscard]] SearchScope currentScope() const noexcept {
        return m_scope;
    }
    [[nodiscard]] QString currentKind() const;
    [[nodiscard]] bool isSubfolders() const;
    [[nodiscard]] bool isContentSearch() const;
    void setContentSearch(bool enabled);
    [[nodiscard]] bool isIndexEnabled() const;

signals:
    void scopeChanged(SearchScope scope);
    void kindChanged(const QString& kind);
    void subfoldersToggled(bool subfolders);
    void contentToggled(bool enabled);
    void indexToggled(bool enabled);
    void clearRequested();
    void saveSearchRequested();
    void chipRemoved(const QString& token);
    void sortModeChanged(const QString& mode);
    void refineChanged(const QString& text);
    void liveToggled(bool enabled);

public slots:
    void updateThemeStyles(bool isDark);

private slots:
    void onThisFolderClicked();
    void onThisPCClicked();

private:
    void setupUi();
    void updateScopeButtons();
    void updateLiveVisibility();

    SearchScope m_scope{SearchScope::ThisFolder};
    QString m_folderName;
    bool m_isDark{false};

    QLabel* m_labelSearch{nullptr};
    QPushButton* m_thisFolderBtn{nullptr};
    QPushButton* m_thisPCBtn{nullptr};
    QCheckBox* m_subfoldersCb{nullptr};
    QCheckBox* m_contentsCb{nullptr};
    QCheckBox* m_indexCb{nullptr};
    QLabel* m_kindLabel{nullptr};
    QComboBox* m_kindCombo{nullptr};
    QWidget* m_chipBar{nullptr};
    QComboBox* m_sortCombo{nullptr};
    QLineEdit* m_refineEdit{nullptr};
    QCheckBox* m_liveCb{nullptr};
    QVector<QToolButton*> m_chipButtons;
    QLabel* m_filterSummaryLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QPushButton* m_saveSearchBtn{nullptr};
    QPushButton* m_clearBtn{nullptr};
};
