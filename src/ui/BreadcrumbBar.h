#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QStackedLayout>
#include <QCompleter>
#include <QFileSystemModel>

class BreadcrumbBar : public QWidget {
    Q_OBJECT

public:
    explicit BreadcrumbBar(QWidget* parent = nullptr);

    void setPath(const QString& path);
    [[nodiscard]] QString currentPath() const noexcept { return m_currentPath; }
    void showPathEditor();
    void updateThemeStyles(bool isDark);

signals:
    void pathSelected(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onSegmentClicked(const QString& path);
    void onPathEditFinished();
    void onSpringTimeout();

private:
    void resetSpringTimer();
    void rebuildBreadcrumbs();
    void showContextMenu(const QPoint& globalPos);

    QString m_currentPath;
    QHBoxLayout* m_breadcrumbLayout{nullptr};
    QWidget* m_breadcrumbContainer{nullptr};
    QLineEdit* m_pathEdit{nullptr};
    QCompleter* m_completer{nullptr};
    QFileSystemModel* m_completerModel{nullptr};
    QStackedLayout* m_stackLayout{nullptr};
    QTimer* m_springTimer{nullptr};
    QString m_springHoverPath;
    QWidget* m_springHoverWidget{nullptr};
};
