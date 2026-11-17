#pragma once

#include <QFont>
#include <QPixmapCache>
#include <QStyledItemDelegate>

class FileItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit FileItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;

    enum class LabelPosition { Bottom, Right };

    void setIconSize(int size) {
        // No global QPixmapCache::clear() here. Every entry this delegate
        // inserts is keyed by the scaled size and device pixel ratio, so tiles
        // at the previous icon size can never be returned for the new size.
        // Clearing would instead evict every toolbar, sidebar and menu pixmap
        // in the process and force them all to be re-rendered; the stale
        // thumbnail entries are simply left for QPixmapCache's LRU to reclaim.
        m_iconSize = size;
    }
    [[nodiscard]] int iconSize() const noexcept {
        return m_iconSize;
    }
    void setTextSize(int size) {
        m_textSize = size;
    }
    [[nodiscard]] int textSize() const noexcept {
        return m_textSize;
    }
    void setLabelPosition(LabelPosition position) {
        m_labelPosition = position;
    }
    [[nodiscard]] LabelPosition labelPosition() const noexcept {
        return m_labelPosition;
    }
    void setShowItemInfo(bool show) {
        m_showItemInfo = show;
    }
    [[nodiscard]] bool showItemInfo() const noexcept {
        return m_showItemInfo;
    }
    void setShowIconPreview(bool show) {
        m_showIconPreview = show;
    }
    [[nodiscard]] bool showIconPreview() const noexcept {
        return m_showIconPreview;
    }
    [[nodiscard]] QSize iconGridSize() const;

    void setSpringHoverPath(const QString& path) {
        m_springHoverPath = path;
    }
    [[nodiscard]] QString springHoverPath() const noexcept {
        return m_springHoverPath;
    }

private:
    [[nodiscard]] QFont textFont(const QFont& base) const;

    int m_iconSize{64};
    int m_textSize{10};
    LabelPosition m_labelPosition{LabelPosition::Bottom};
    bool m_showItemInfo{false};
    bool m_showIconPreview{true};
    QString m_springHoverPath;
};
