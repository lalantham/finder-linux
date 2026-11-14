#include "FileItemDelegate.h"
#include "FileSystemModel.h"
#include "PreviewManager.h"
#include "TagManager.h"
#include "ThemeManager.h"
#include <QPainter>
#include <QApplication>
#include <QPainterPath>
#include <QFileIconProvider>
#include <QLineEdit>
#include <QTimer>

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
    QString itemPath = index.data(FileSystemModel::FilePathRole).toString();
    bool isSpringHover = (!m_springHoverPath.isEmpty() && m_springHoverPath == itemPath);

    // Check if drawing in icon mode or table/tree mode
    if (option.decorationPosition == QStyleOptionViewItem::Top) {
        // Finder Icon Mode
        QRect itemRect = option.rect;

        // Hover or Spring-loaded hover highlight
        if (isSpringHover) {
            QColor acc = ThemeManager::instance().accentColor();
            painter->setPen(QPen(acc, 2.0));
            painter->setBrush(QColor(acc.red(), acc.green(), acc.blue(), 45));
            painter->drawRoundedRect(itemRect.adjusted(3, 3, -3, -3), 8, 8);
        } else if (isSelected) {
            QColor acc = ThemeManager::instance().accentColor();
            painter->setPen(QPen(acc, 1.5));
            painter->setBrush(QColor(acc.red(), acc.green(), acc.blue(), 40));
            painter->drawRoundedRect(itemRect.adjusted(3, 3, -3, -3), 8, 8);
        } else if (isHovered) {
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

        bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();
        QPixmap thumbnail = isDir ? QPixmap()
                                  : PreviewManager::instance().iconThumbnail(itemPath, m_iconSize);
        if (!thumbnail.isNull()) {
            const QSize scaled = thumbnail.size().scaled(iconRect.size(), Qt::KeepAspectRatio);
            const QRect target(iconRect.x() + (iconRect.width() - scaled.width()) / 2,
                               iconRect.y() + (iconRect.height() - scaled.height()) / 2,
                               scaled.width(), scaled.height());
            painter->drawPixmap(target, thumbnail.scaled(scaled, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            if (icon.isNull()) {
                static QFileIconProvider fallbackProv;
                icon = isDir ? QIcon::fromTheme(QStringLiteral("folder"), fallbackProv.icon(QFileIconProvider::Folder))
                             : QIcon::fromTheme(QStringLiteral("text-x-generic"), fallbackProv.icon(QFileIconProvider::File));
            }
            icon.paint(painter, iconRect, Qt::AlignCenter);
        }

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
        if (isSpringHover) {
            QColor acc = ThemeManager::instance().accentColor();
            painter->setPen(QPen(acc, 1.5));
            painter->setBrush(QColor(acc.red(), acc.green(), acc.blue(), 45));
            painter->drawRoundedRect(option.rect.adjusted(2, 1, -2, -1), 4, 4);
            painter->setPen(ThemeManager::instance().textColor());
        } else if (isSelected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(ThemeManager::instance().accentColor());
            if (index.column() == 0) {
                painter->drawRoundedRect(option.rect.adjusted(2, 1, 0, -1), 4, 4);
            } else if (index.column() == 3) {
                painter->drawRoundedRect(option.rect.adjusted(0, 1, -2, -1), 4, 4);
            } else {
                painter->drawRect(option.rect.adjusted(0, 1, 0, -1));
            }
            painter->setPen(Qt::white);
        } else if (isHovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(128, 128, 128, 25));
            if (index.column() == 0) {
                painter->drawRoundedRect(option.rect.adjusted(2, 1, 0, -1), 4, 4);
            } else if (index.column() == 3) {
                painter->drawRoundedRect(option.rect.adjusted(0, 1, -2, -1), 4, 4);
            } else {
                painter->drawRect(option.rect.adjusted(0, 1, 0, -1));
            }
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

QWidget* FileItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    auto* editor = new QLineEdit(parent);
    editor->setFrame(true);
    editor->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 4px; }")
        .arg(ThemeManager::instance().isDark() ? QStringLiteral("#2d2d2d") : QStringLiteral("#ffffff"))
        .arg(ThemeManager::instance().isDark() ? QStringLiteral("#ffffff") : QStringLiteral("#000000"))
        .arg(ThemeManager::instance().accentColor().name()));
    return editor;
}

void FileItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit) {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    QString name = index.data(Qt::DisplayRole).toString();
    lineEdit->setText(name);
    const bool isDirectory = index.data(FileSystemModel::FileIsDirRole).toBool();
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    const int selectionLen = (!isDirectory && dot > 0)
        ? static_cast<int>(dot)
        : static_cast<int>(name.size());
    lineEdit->setSelection(0, selectionLen);
    // QAbstractItemView selects the whole editor text after setEditorData().
    // Re-apply the stem selection once the view has finished opening the editor.
    QTimer::singleShot(0, lineEdit, [lineEdit, selectionLen]() {
        lineEdit->setSelection(0, selectionLen);
    });
}

void FileItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (option.decorationPosition == QStyleOptionViewItem::Top) {
        int iconAreaHeight = m_iconSize;
        QRect textRect(option.rect.left() + 2,
                       option.rect.top() + iconAreaHeight + 10,
                       option.rect.width() - 4,
                       24);
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
