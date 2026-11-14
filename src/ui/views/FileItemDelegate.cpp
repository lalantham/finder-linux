#include "FileItemDelegate.h"
#include "FileSystemModel.h"
#include "PreviewManager.h"
#include "TagManager.h"
#include "ThemeManager.h"
#include <QApplication>
#include <QFileIconProvider>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

FileItemDelegate::FileItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QFont FileItemDelegate::textFont(const QFont& base) const {
    QFont font = base;
    font.setPointSize(m_textSize);
    return font;
}

QSize FileItemDelegate::iconGridSize() const {
    const QFont font = textFont(QFont());
    const QFontMetrics fm(font);
    const int nameHeight = fm.height() * 2; // Two-line labels are supported.
    const int infoHeight = m_showItemInfo ? fm.height() + 2 : 0;

    if (m_labelPosition == LabelPosition::Right) {
        const int width = qMax(m_iconSize + 24, m_iconSize + 24 + 170);
        const int height = qMax(m_iconSize + 16, nameHeight + infoHeight + 12);
        return QSize(width, height);
    }

    const int width = qMax(m_iconSize + 36, 96);
    const int height = m_iconSize + 12 + nameHeight + infoHeight + 10;
    return QSize(width, height);
}

void FileItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const bool isSelected = option.state & QStyle::State_Selected;
    const bool isHovered = option.state & QStyle::State_MouseOver;
    const QString itemPath = index.data(FileSystemModel::FilePathRole).toString();
    const bool isSpringHover = (!m_springHoverPath.isEmpty() && m_springHoverPath == itemPath);
    const bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();

    if (option.decorationPosition == QStyleOptionViewItem::Top) {
        const QRect itemRect = option.rect;
        const QRect highlightRect = itemRect.adjusted(3, 3, -3, -3);

        if (isSpringHover || isSelected) {
            const QColor acc = ThemeManager::instance().accentColor();
            painter->setPen(QPen(acc, isSpringHover ? 2.0 : 1.5));
            painter->setBrush(QColor(acc.red(), acc.green(), acc.blue(), isSpringHover ? 45 : 40));
            painter->drawRoundedRect(highlightRect, 8, 8);
        } else if (isHovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(128, 128, 128, 30));
            painter->drawRoundedRect(itemRect.adjusted(4, 4, -4, -4), 8, 8);
        }

        const int iconSize = m_iconSize;
        QRect iconRect;
        if (m_labelPosition == LabelPosition::Right) {
            iconRect =
                QRect(itemRect.left() + 8, itemRect.top() + (itemRect.height() - iconSize) / 2,
                      iconSize, iconSize);
        } else {
            iconRect = QRect(itemRect.left() + (itemRect.width() - iconSize) / 2,
                             itemRect.top() + 6, iconSize, iconSize);
        }

        QPixmap thumbnail;
        if (m_showIconPreview && !isDir) {
            thumbnail = PreviewManager::instance().iconThumbnail(itemPath, iconSize);
        }
        if (!thumbnail.isNull()) {
            const QSize scaled = thumbnail.size().scaled(iconRect.size(), Qt::KeepAspectRatio);
            const QRect target(iconRect.x() + (iconRect.width() - scaled.width()) / 2,
                               iconRect.y() + (iconRect.height() - scaled.height()) / 2,
                               scaled.width(), scaled.height());
            painter->drawPixmap(
                target, thumbnail.scaled(scaled, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            if (icon.isNull()) {
                static QFileIconProvider fallbackProv;
                icon = isDir ? QIcon::fromTheme(QStringLiteral("folder"),
                                                fallbackProv.icon(QFileIconProvider::Folder))
                             : QIcon::fromTheme(QStringLiteral("text-x-generic"),
                                                fallbackProv.icon(QFileIconProvider::File));
            }
            icon.paint(painter, iconRect, Qt::AlignCenter);
        }

        const QString text = index.data(Qt::DisplayRole).toString();
        QString itemInfo = index.data(FileSystemModel::FileFormattedSizeRole).toString();
        if (itemInfo.isEmpty() || itemInfo == QStringLiteral("--")) {
            itemInfo = index.data(FileSystemModel::FileKindRole).toString();
        }

        const QFont nameFont = textFont(option.font);
        const QFontMetrics nameMetrics(nameFont);
        painter->setFont(nameFont);

        QRect textRect;
        QRect infoRect;
        if (m_labelPosition == LabelPosition::Right) {
            const int textLeft = iconRect.right() + 10;
            textRect = QRect(textLeft, itemRect.top() + 4, itemRect.right() - textLeft - 6,
                             nameMetrics.height());
            infoRect = QRect(textLeft, textRect.bottom() + 2, itemRect.right() - textLeft - 6,
                             nameMetrics.height());
        } else {
            textRect = QRect(itemRect.left() + 4, iconRect.bottom() + 6, itemRect.width() - 8,
                             nameMetrics.height() * 2);
            infoRect = QRect(itemRect.left() + 4, textRect.top() + nameMetrics.height(),
                             itemRect.width() - 8, nameMetrics.height());
        }

        const QString elidedName = nameMetrics.elidedText(text, Qt::ElideMiddle, textRect.width());
        if (isSelected) {
            const QRect labelBound =
                m_labelPosition == LabelPosition::Right
                    ? QRect(textRect.left() - 4, textRect.top() - 2, textRect.width() + 8,
                            nameMetrics.height() + (m_showItemInfo ? nameMetrics.height() + 4 : 0) +
                                4)
                    : QRect(itemRect.left() + 4, textRect.top() - 2, itemRect.width() - 8,
                            nameMetrics.height() * (m_showItemInfo ? 2 : 1) + 4);
            painter->setPen(Qt::NoPen);
            painter->setBrush(ThemeManager::instance().accentColor());
            painter->drawRoundedRect(labelBound, 5, 5);
            painter->setPen(Qt::white);
        } else {
            painter->setPen(ThemeManager::instance().textColor());
        }

        if (m_labelPosition == LabelPosition::Right) {
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);
        } else {
            painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                              elidedName);
        }

        if (m_showItemInfo) {
            QFont infoFont = nameFont;
            infoFont.setPointSize(qMax(7, m_textSize - 2));
            painter->setFont(infoFont);
            painter->setPen(isSelected ? QColor(255, 255, 255, 220)
                                       : ThemeManager::instance().subTextColor());
            if (m_labelPosition == LabelPosition::Right) {
                painter->drawText(infoRect, Qt::AlignLeft | Qt::AlignVCenter, itemInfo);
            } else {
                painter->drawText(infoRect, Qt::AlignHCenter | Qt::AlignTop, itemInfo);
            }
        }

        const QStringList tags = index.data(FileSystemModel::FileTagsRole).toStringList();
        if (!tags.isEmpty()) {
            const int dotSize = 7;
            const int dotSpacing = 3;
            const int totalDotsW = static_cast<int>(tags.size()) * dotSize +
                                   (static_cast<int>(tags.size()) - 1) * dotSpacing;
            const int startX = (m_labelPosition == LabelPosition::Right)
                                   ? itemRect.right() - totalDotsW - 8
                                   : itemRect.left() + (itemRect.width() - totalDotsW) / 2;
            const int dotY = (m_labelPosition == LabelPosition::Right)
                                 ? itemRect.bottom() - dotSize - 6
                                 : iconRect.bottom() + 1;
            for (int t = 0; t < tags.size(); ++t) {
                const QColor c = TagManager::colorForTag(tags[t]);
                painter->setPen(c.darker(115));
                painter->setBrush(c);
                painter->drawEllipse(startX + t * (dotSize + dotSpacing), dotY, dotSize, dotSize);
            }
        }
    } else {
        if (isSpringHover) {
            const QColor acc = ThemeManager::instance().accentColor();
            painter->setPen(QPen(acc, 1.5));
            painter->setBrush(QColor(acc.red(), acc.green(), acc.blue(), 45));
            painter->drawRoundedRect(option.rect.adjusted(2, 1, -2, -1), 4, 4);
            painter->setPen(ThemeManager::instance().textColor());
        } else if (isSelected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(ThemeManager::instance().accentColor());
            if (index.column() == 0) {
                painter->drawRoundedRect(option.rect.adjusted(2, 1, 0, -1), 4, 4);
            } else {
                painter->drawRect(option.rect.adjusted(0, 1, 0, -1));
            }
            painter->setPen(Qt::white);
        } else if (isHovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(128, 128, 128, 25));
            if (index.column() == 0) {
                painter->drawRoundedRect(option.rect.adjusted(2, 1, 0, -1), 4, 4);
            } else {
                painter->drawRect(option.rect.adjusted(0, 1, 0, -1));
            }
            painter->setPen(ThemeManager::instance().textColor());
        } else {
            painter->setPen(ThemeManager::instance().textColor());
        }

        int textLeft = option.rect.left() + 6;
        if (index.column() == 0) {
            QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            if (icon.isNull()) {
                static QFileIconProvider fallbackProv;
                icon = isDir ? QIcon::fromTheme(QStringLiteral("folder"),
                                                fallbackProv.icon(QFileIconProvider::Folder))
                             : QIcon::fromTheme(QStringLiteral("text-x-generic"),
                                                fallbackProv.icon(QFileIconProvider::File));
            }
            const QRect iconRect(textLeft, option.rect.top() + (option.rect.height() - 18) / 2, 18,
                                 18);
            icon.paint(painter, iconRect);
            textLeft += 24;

            const QStringList tags = index.data(FileSystemModel::FileTagsRole).toStringList();
            const int dotSize = 8;
            const int dotSpacing = 3;
            const int dotsWidth =
                tags.isEmpty() ? 0 : static_cast<int>(tags.size()) * (dotSize + dotSpacing) + 6;

            const QRect textRect(textLeft, option.rect.top(),
                                 option.rect.right() - textLeft - dotsWidth - 4,
                                 option.rect.height());
            const QString text = index.data(Qt::DisplayRole).toString();
            const QFont font = textFont(option.font);
            painter->setFont(font);

            const QFontMetrics fm(font);
            const QString elidedText = fm.elidedText(text, Qt::ElideMiddle, textRect.width());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);

            if (!tags.isEmpty()) {
                const int textActualW = qMin(fm.horizontalAdvance(elidedText), textRect.width());
                const int dotX = textLeft + textActualW + 8;
                const int dotY = option.rect.top() + (option.rect.height() - dotSize) / 2;
                for (int t = 0; t < tags.size(); ++t) {
                    const QColor c = TagManager::colorForTag(tags[t]);
                    painter->setPen(c.darker(115));
                    painter->setBrush(c);
                    painter->drawEllipse(dotX + t * (dotSize + dotSpacing), dotY, dotSize, dotSize);
                }
            }
        } else {
            const QRect textRect(textLeft, option.rect.top(), option.rect.right() - textLeft - 4,
                                 option.rect.height());
            const QString text = index.data(Qt::DisplayRole).toString();
            painter->setFont(textFont(option.font));
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        }
    }

    painter->restore();
}

QSize FileItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& /*index*/) const {
    if (option.decorationPosition == QStyleOptionViewItem::Top) {
        return iconGridSize();
    }
    const QFontMetrics fm(textFont(option.font));
    return QSize(option.rect.width(), qMax(24, fm.height() + 8));
}

QWidget* FileItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    auto* editor = new QLineEdit(parent);
    editor->setFrame(true);
    editor->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid "
                                         "%3; border-radius: 4px; padding: 2px 4px; }")
                              .arg(ThemeManager::instance().isDark() ? QStringLiteral("#2d2d2d")
                                                                     : QStringLiteral("#ffffff"))
                              .arg(ThemeManager::instance().isDark() ? QStringLiteral("#ffffff")
                                                                     : QStringLiteral("#000000"))
                              .arg(ThemeManager::instance().accentColor().name()));
    return editor;
}

void FileItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    auto* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit) {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    QString name = index.data(Qt::DisplayRole).toString();
    lineEdit->setText(name);
    const bool isDirectory = index.data(FileSystemModel::FileIsDirRole).toBool();
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    const int selectionLen =
        (!isDirectory && dot > 0) ? static_cast<int>(dot) : static_cast<int>(name.size());
    lineEdit->setSelection(0, selectionLen);
    // QAbstractItemView selects the whole editor text after setEditorData().
    // Re-apply the stem selection once the view has finished opening the editor.
    QTimer::singleShot(0, lineEdit,
                       [lineEdit, selectionLen]() { lineEdit->setSelection(0, selectionLen); });
}

void FileItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                            const QModelIndex& index) const {
    if (option.decorationPosition == QStyleOptionViewItem::Top) {
        const QRect textRect =
            (m_labelPosition == LabelPosition::Right)
                ? QRect(option.rect.left() + m_iconSize + 18,
                        option.rect.top() + (option.rect.height() - 24) / 2,
                        option.rect.width() - m_iconSize - 24, 24)
                : QRect(option.rect.left() + 2, option.rect.top() + m_iconSize + 8,
                        option.rect.width() - 4, 24);
        editor->setGeometry(textRect);
    } else {
        if (index.column() == 0) {
            int textLeft = option.rect.left() + 30;
            QRect textRect(textLeft, option.rect.top() + (option.rect.height() - 24) / 2,
                           option.rect.width() - 34, 24);
            editor->setGeometry(textRect);
        } else {
            QStyledItemDelegate::updateEditorGeometry(editor, option, index);
        }
    }
}
