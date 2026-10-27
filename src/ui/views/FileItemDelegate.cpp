#include "FileItemDelegate.h"
#include "FileSystemModel.h"
#include "TagManager.h"
#include "ThemeManager.h"
#include <QPainter>
#include <QApplication>
#include <QPainterPath>
#include <QFileIconProvider>

FileItemDelegate::FileItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void FileItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    bool isSelected = option.state & QStyle::State_Selected;
    bool isHovered = option.state & QStyle::State_MouseOver;

    // Check if drawing in icon mode or table/tree mode
    if (option.decorationPosition == QStyleOptionViewItem::Top) {
        // Finder Icon Mode
        QRect itemRect = option.rect;

        // Hover highlight
        if (isHovered && !isSelected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(128, 128, 128, 30));
            painter->drawRoundedRect(itemRect.adjusted(4, 4, -4, -4), 8, 8);
        }

        // Draw icon centered at top
        int iconAreaHeight = m_iconSize;
        QRect iconRect(itemRect.left() + (itemRect.width() - m_iconSize) / 2,
                       itemRect.top() + 6,
                       m_iconSize,
                       iconAreaHeight);

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (icon.isNull()) {
            static QFileIconProvider fallbackProv;
            bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();
            icon = isDir ? QIcon::fromTheme(QStringLiteral("folder"), fallbackProv.icon(QFileIconProvider::Folder))
                         : QIcon::fromTheme(QStringLiteral("text-x-generic"), fallbackProv.icon(QFileIconProvider::File));
        }
        icon.paint(painter, iconRect, Qt::AlignCenter);

        // Draw text below icon
        QRect textRect(itemRect.left() + 4,
                       iconRect.bottom() + 6,
                       itemRect.width() - 8,
                       itemRect.height() - iconAreaHeight - 12);

        QString text = index.data(Qt::DisplayRole).toString();

        QFont font = option.font;
        font.setPointSize(10);
        painter->setFont(font);

        QFontMetrics fm(font);
        QString elidedText = fm.elidedText(text, Qt::ElideMiddle, textRect.width() * 2);

        if (isSelected) {
            // Selection pill around label using system accent color
            QRect labelBound = fm.boundingRect(textRect, Qt::AlignHCenter | Qt::TextWordWrap, elidedText);
            labelBound.adjust(-6, -2, 6, 2);

            painter->setPen(Qt::NoPen);
            painter->setBrush(ThemeManager::instance().accentColor());
            painter->drawRoundedRect(labelBound, 5, 5);

            painter->setPen(Qt::white);
        } else {
            painter->setPen(ThemeManager::instance().textColor());
        }

        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, elidedText);

        // Draw tag dots below icon
        QStringList tags = index.data(FileSystemModel::FileTagsRole).toStringList();
        if (!tags.isEmpty()) {
            int dotSize = 7;
            int dotSpacing = 3;
            int numTags = static_cast<int>(tags.size());
            int totalDotsW = numTags * dotSize + (numTags - 1) * dotSpacing;
            int startX = itemRect.left() + (itemRect.width() - totalDotsW) / 2;
            int dotY = iconRect.bottom() + 1;

            for (int t = 0; t < numTags; ++t) {
                QColor c = TagManager::colorForTag(tags[t]);
                painter->setPen(c.darker(115));
                painter->setBrush(c);
                painter->drawEllipse(startX + t * (dotSize + dotSpacing), dotY, dotSize, dotSize);
            }
        }

    } else {
        // List / Table Mode
        if (isSelected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(ThemeManager::instance().accentColor());
            painter->drawRoundedRect(option.rect.adjusted(2, 1, -2, -1), 4, 4);
            painter->setPen(Qt::white);
        } else if (isHovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(128, 128, 128, 25));
            painter->drawRoundedRect(option.rect.adjusted(2, 1, -2, -1), 4, 4);
            painter->setPen(ThemeManager::instance().textColor());
        } else {
            painter->setPen(ThemeManager::instance().textColor());
        }

        // Draw Icon if column 0
        int textLeft = option.rect.left() + 6;
        if (index.column() == 0) {
            QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            if (icon.isNull()) {
                static QFileIconProvider fallbackProv;
                bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();
                icon = isDir ? QIcon::fromTheme(QStringLiteral("folder"), fallbackProv.icon(QFileIconProvider::Folder))
                             : QIcon::fromTheme(QStringLiteral("text-x-generic"), fallbackProv.icon(QFileIconProvider::File));
            }
            QRect iconRect(textLeft, option.rect.top() + (option.rect.height() - 18) / 2, 18, 18);
            icon.paint(painter, iconRect);
            textLeft += 24;

            QStringList tags = index.data(FileSystemModel::FileTagsRole).toStringList();
            int dotSize = 8;
            int dotSpacing = 3;
            int numTags = static_cast<int>(tags.size());
            int dotsWidth = tags.isEmpty() ? 0 : (numTags * (dotSize + dotSpacing) + 6);

            QRect textRect(textLeft, option.rect.top(), option.rect.right() - textLeft - dotsWidth - 4, option.rect.height());
            QString text = index.data(Qt::DisplayRole).toString();

            QFont font = option.font;
            font.setPointSize(10);
            painter->setFont(font);

            QFontMetrics fm(font);
            QString elidedText = fm.elidedText(text, Qt::ElideMiddle, textRect.width());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);

            if (!tags.isEmpty()) {
                int textActualW = qMin(fm.horizontalAdvance(elidedText), textRect.width());
                int dotX = textLeft + textActualW + 8;
                int dotY = option.rect.top() + (option.rect.height() - dotSize) / 2;
                for (int t = 0; t < numTags; ++t) {
                    QColor c = TagManager::colorForTag(tags[t]);
                    painter->setPen(c.darker(115));
                    painter->setBrush(c);
                    painter->drawEllipse(dotX + t * (dotSize + dotSpacing), dotY, dotSize, dotSize);
                }
            }
        } else {
            QRect textRect(textLeft, option.rect.top(), option.rect.right() - textLeft - 4, option.rect.height());
            QString text = index.data(Qt::DisplayRole).toString();

            QFont font = option.font;
            font.setPointSize(10);
            painter->setFont(font);

            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        }
    }

    painter->restore();
}

QSize FileItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (option.decorationPosition == QStyleOptionViewItem::Top) {
        // Icon View size hint: icon size + margin + 2-3 lines of text
        int w = qMax(m_iconSize + 36, 96);
        int h = m_iconSize + 48;
        return QSize(w, h);
    }
    return QSize(option.rect.width(), 24);
}
