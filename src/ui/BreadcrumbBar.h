#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QStackedLayout>

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

private slots:
    void onSegmentClicked(const QString& path);
    void onPathEditFinished();

private:
    void rebuildBreadcrumbs();

    QString m_currentPath;
    QHBoxLayout* m_breadcrumbLayout{nullptr};
    QWidget* m_breadcrumbContainer{nullptr};
    QLineEdit* m_pathEdit{nullptr};
    QStackedLayout* m_stackLayout{nullptr};
};
