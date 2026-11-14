#pragma once

#include <QStyledItemDelegate>

class FileItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit FileItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void setIconSize(int size) { m_iconSize = size; }
    [[nodiscard]] int iconSize() const noexcept { return m_iconSize; }

    void setSpringHoverPath(const QString& path) { m_springHoverPath = path; }
    [[nodiscard]] QString springHoverPath() const noexcept { return m_springHoverPath; }

private:
    int m_iconSize{64};
    QString m_springHoverPath;
};
