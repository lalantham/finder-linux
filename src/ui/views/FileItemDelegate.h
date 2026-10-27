#pragma once

#include <QStyledItemDelegate>

class FileItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit FileItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void setIconSize(int size) { m_iconSize = size; }
    [[nodiscard]] int iconSize() const noexcept { return m_iconSize; }

private:
    int m_iconSize{64};
};
