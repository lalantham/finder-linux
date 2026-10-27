#include "ColumnView.h"
#include "AppSettings.h"
#include "DesktopAppsManager.h"
#include "PreviewManager.h"
#include "FileItem.h"
#include "ThemeManager.h"
#include <QScrollBar>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QPainter>
#include <QFileIconProvider>
#include <QPropertyAnimation>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>

// ColumnItemDelegate
ColumnItemDelegate::ColumnItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ColumnItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    bool isSelected = option.state & QStyle::State_Selected;
    bool isHovered = option.state & QStyle::State_MouseOver;
    bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();
    bool isDark = ThemeManager::instance().isDark();

    // Row selection or hover background
    if (isSelected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(ThemeManager::instance().accentColor());
        painter->drawRoundedRect(option.rect.adjusted(4, 1, -4, -1), 5, 5);
    } else if (isHovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(isDark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 14));
        painter->drawRoundedRect(option.rect.adjusted(4, 1, -4, -1), 5, 5);
    }

    // Draw Icon (18x18)
    int textLeft = option.rect.left() + 10;
    QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (icon.isNull()) {
        static QFileIconProvider fallbackProv;
        icon = isDir ? QIcon::fromTheme(QStringLiteral("folder"), fallbackProv.icon(QFileIconProvider::Folder))
                     : QIcon::fromTheme(QStringLiteral("text-x-generic"), fallbackProv.icon(QFileIconProvider::File));
    }
    QRect iconRect(textLeft, option.rect.top() + (option.rect.height() - 18) / 2, 18, 18);
    icon.paint(painter, iconRect);
    textLeft += 24;

    // Text label
    int rightMargin = isDir ? 24 : 8;
    QRect textRect(textLeft, option.rect.top(), option.rect.right() - textLeft - rightMargin, option.rect.height());
    QString text = index.data(Qt::DisplayRole).toString();

    QFont font = option.font;
    font.setPointSize(10);
    painter->setFont(font);

    QFontMetrics fm(font);
    QString elidedText = fm.elidedText(text, Qt::ElideMiddle, textRect.width());

    painter->setPen(isSelected ? Qt::white : (isDark ? QColor(0xf5, 0xf5, 0xf7) : QColor(0x1d, 0x1d, 0x1f)));
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);

    // Draw iconic macOS right chevron for folders
    if (isDir) {
        QRect chevronRect(option.rect.right() - 20, option.rect.top(), 14, option.rect.height());
        painter->setPen(isSelected ? Qt::white : (isDark ? QColor(0x8e, 0x8e, 0x93) : QColor(0x99, 0x99, 0x9e)));
        QFont chFont = font;
        chFont.setBold(true);
        chFont.setPointSize(11);
        painter->setFont(chFont);
        painter->drawText(chevronRect, Qt::AlignCenter, QStringLiteral("›"));
    }

    painter->restore();
}

QSize ColumnItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& /*index*/) const
{
    return QSize(option.rect.width(), 26);
}

// ColumnBrowserList
ColumnBrowserList::ColumnBrowserList(const QString& path, QWidget* parent)
    : QListView(parent)
    , m_path(path)
{
    m_model = new FileSystemModel(this);
    m_model->setShowHidden(AppSettings::instance().showHiddenFiles());
    m_model->setRootPath(path);
    setModel(m_model);

    setFixedWidth(240);
    setUniformItemSizes(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setItemDelegate(new ColumnItemDelegate(this));
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::MoveAction);

    setStyleSheet(QStringLiteral(
        "QListView {"
        "  background: transparent;"
        "  border: none;"
        "  border-right: 1px solid rgba(128, 128, 128, 0.22);"
        "  outline: none;"
        "}"
    ));

    connect(m_model, &FileSystemModel::fileDropRequested, this, &ColumnBrowserList::fileDropRequested);
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, &ColumnBrowserList::onSelectionChanged);
    connect(this, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        QString itemPath = index.data(FileSystemModel::FilePathRole).toString();
        bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();
        emit itemDoubleClicked(itemPath, isDir);
    });
}

void ColumnBrowserList::onSelectionChanged()
{
    QModelIndex idx = currentIndex();
    if (!idx.isValid()) return;
    QString itemPath = idx.data(FileSystemModel::FilePathRole).toString();
    bool isDir = idx.data(FileSystemModel::FileIsDirRole).toBool();
    emit itemSelected(itemPath, isDir);
}

void ColumnBrowserList::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        QModelIndex idx = currentIndex();
        if (idx.isValid()) {
            emit quickLookRequested(idx.data(FileSystemModel::FilePathRole).toString());
        }
        return;
    }
    if (event->key() == Qt::Key_Left) {
        emit navigateLeftRequested();
        return;
    }
    if (event->key() == Qt::Key_Right) {
        QModelIndex idx = currentIndex();
        if (idx.isValid() && idx.data(FileSystemModel::FileIsDirRole).toBool()) {
            emit navigateRightRequested();
        }
        return;
    }
    QListView::keyPressEvent(event);
}

void ColumnBrowserList::contextMenuEvent(QContextMenuEvent* event)
{
    QModelIndex idx = indexAt(event->pos());
    QStringList paths;
    if (idx.isValid()) {
        setCurrentIndex(idx);
        paths.append(idx.data(FileSystemModel::FilePathRole).toString());
    }
    emit contextMenuRequested(event->globalPos(), paths);
}

// ColumnPreviewPane
ColumnPreviewPane::ColumnPreviewPane(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(280);
    setStyleSheet(QStringLiteral(
        "ColumnPreviewPane {"
        "  background: transparent;"
        "  border-left: 1px solid rgba(128, 128, 128, 0.22);"
        "}"
    ));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 20, 16, 20);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignTop);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedHeight(140);
    layout->addWidget(m_iconLabel);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    QFont nf = m_nameLabel->font();
    nf.setBold(true);
    nf.setPointSize(11);
    m_nameLabel->setFont(nf);
    m_nameLabel->setWordWrap(true);
    layout->addWidget(m_nameLabel);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    m_kindLabel = new QLabel(this);
    m_kindLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(m_kindLabel);

    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(m_sizeLabel);

    m_modifiedLabel = new QLabel(this);
    m_modifiedLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(m_modifiedLabel);

    layout->addSpacing(12);

    auto* qlBtn = new QPushButton(QStringLiteral("Quick Look"), this);
    connect(qlBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentPath.isEmpty()) emit quickLookRequested(m_currentPath);
    });
    layout->addWidget(qlBtn);

    layout->addStretch();
}

void ColumnPreviewPane::setItem(const QString& path)
{
    m_currentPath = path;
    FileItem item(path);

    m_nameLabel->setText(item.fileName());
    m_kindLabel->setText(QStringLiteral("Kind: %1").arg(item.kind()));
    m_sizeLabel->setText(QStringLiteral("Size: %1").arg(item.formattedSize()));
    m_modifiedLabel->setText(QStringLiteral("Modified: %1").arg(item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm"))));

    auto& pm = PreviewManager::instance();
    if (pm.isImage(path)) {
        QPixmap pix = pm.getThumbnail(path, QSize(220, 140));
        m_iconLabel->setPixmap(pix);
    } else {
        m_iconLabel->setPixmap(item.icon().pixmap(96, 96));
    }
}

// ColumnView
ColumnView::ColumnView(QWidget* parent)
    : QScrollArea(parent)
    , m_showHidden(AppSettings::instance().showHiddenFiles())
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget(this);
    m_layout = new QHBoxLayout(m_container);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_previewPane = new ColumnPreviewPane(m_container);
    m_previewPane->hide();
    connect(m_previewPane, &ColumnPreviewPane::quickLookRequested, this, &ColumnView::quickLookRequested);
    connect(m_previewPane, &ColumnPreviewPane::getInfoRequested, this, &ColumnView::getInfoRequested);

    m_layout->addWidget(m_previewPane);
    m_layout->addStretch();

    setWidget(m_container);
}

void ColumnView::setRootPath(const QString& path)
{
    m_rootPath = path;
    m_selectedPath.clear();
    for (auto* col : m_columns) {
        m_layout->removeWidget(col);
        col->deleteLater();
    }
    m_columns.clear();
    m_previewPane->hide();

    addColumn(path, false);
}

void ColumnView::refresh()
{
    QString selected = m_selectedPath;

    for (int i = 0; i < m_columns.size(); ++i) {
        auto* col = m_columns[i];
        if (!QDir(col->path()).exists()) {
            truncateColumnsAfter(i - 1);
            break;
        }

        // Remember which item was selected in this column
        QString colSelectedPath;
        QModelIndex currIdx = col->currentIndex();
        if (currIdx.isValid()) {
            colSelectedPath = currIdx.data(FileSystemModel::FilePathRole).toString();
        }

        col->model()->refresh();

        // Restore selection if it still exists
        if (!colSelectedPath.isEmpty()) {
            bool found = false;
            for (int r = 0; r < col->model()->rowCount(); ++r) {
                QModelIndex idx = col->model()->index(r, 0);
                if (idx.data(FileSystemModel::FilePathRole).toString() == colSelectedPath) {
                    col->setCurrentIndex(idx);
                    found = true;
                    break;
                }
            }
            if (!found && i == m_columns.size() - 1 && !selected.isEmpty() && !QFileInfo::exists(selected)) {
                // The selected item in the last column was deleted
                m_selectedPath.clear();
                m_previewPane->hide();
            }
        }
    }

    if (!m_selectedPath.isEmpty()) {
        if (!QFileInfo::exists(m_selectedPath)) {
            m_selectedPath.clear();
            m_previewPane->hide();
        } else if (m_previewPane->isVisible()) {
            m_previewPane->setItem(m_selectedPath);
        }
    }
}

void ColumnView::setShowHidden(bool show)
{
    m_showHidden = show;
    for (auto* col : m_columns) {
        col->model()->setShowHidden(show);
    }
}

void ColumnView::addColumn(const QString& path, bool animate)
{
    int colIndex = static_cast<int>(m_columns.size());
    auto* col = new ColumnBrowserList(path, m_container);
    col->model()->setShowHidden(m_showHidden);

    m_layout->insertWidget(colIndex, col);
    m_columns.append(col);

    connect(col, &ColumnBrowserList::itemSelected, this, [this, colIndex](const QString& itemPath, bool isDir) {
        onColumnItemSelected(colIndex, itemPath, isDir);
    });

    connect(col, &ColumnBrowserList::itemDoubleClicked, this, [this](const QString& itemPath, bool isDir) {
        if (isDir) {
            emit openPathRequested(itemPath);
        } else {
            DesktopAppsManager::launchFile(itemPath);
        }
    });

    connect(col, &ColumnBrowserList::quickLookRequested, this, &ColumnView::quickLookRequested);
    connect(col, &ColumnBrowserList::getInfoRequested, this, &ColumnView::getInfoRequested);
    connect(col, &ColumnBrowserList::contextMenuRequested, this, &ColumnView::contextMenuRequested);
    connect(col, &ColumnBrowserList::fileDropRequested, this, &ColumnView::fileDropRequested);

    // Keyboard navigation across columns
    connect(col, &ColumnBrowserList::navigateLeftRequested, this, [this, colIndex]() {
        if (colIndex > 0 && colIndex - 1 < m_columns.size()) {
            m_columns[colIndex - 1]->setFocus();
        }
    });

    connect(col, &ColumnBrowserList::navigateRightRequested, this, [this, colIndex]() {
        if (colIndex + 1 < m_columns.size()) {
            auto* nextCol = m_columns[colIndex + 1];
            nextCol->setFocus();
            if (nextCol->model()->rowCount() > 0 && !nextCol->currentIndex().isValid()) {
                nextCol->setCurrentIndex(nextCol->model()->index(0, 0));
            }
        }
    });

    // Animate column slide-in width if traversing
    if (animate) {
        col->setMaximumWidth(0);
        auto* anim = new QPropertyAnimation(col, "maximumWidth", this);
        anim->setDuration(200);
        anim->setStartValue(0);
        anim->setEndValue(240);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, this, [col]() {
            col->setMaximumWidth(QWIDGETSIZE_MAX);
            col->setFixedWidth(240);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    scrollToRightAnimated();
}

void ColumnView::truncateColumnsAfter(int columnIndex)
{
    while (m_columns.size() > columnIndex + 1) {
        auto* col = m_columns.takeLast();
        m_layout->removeWidget(col);
        col->deleteLater();
    }
}

void ColumnView::onColumnItemSelected(int columnIndex, const QString& path, bool isDir)
{
    m_selectedPath = path;
    truncateColumnsAfter(columnIndex);

    if (isDir) {
        m_previewPane->hide();
        addColumn(path, true);
    } else {
        m_previewPane->setItem(path);
        m_previewPane->show();
        scrollToRightAnimated();
    }
}

void ColumnView::scrollToRightAnimated()
{
    QScrollBar* hBar = horizontalScrollBar();
    if (!hBar) return;

    if (m_container) {
        m_container->adjustSize();
    }

    QTimer::singleShot(40, this, [this, hBar]() {
        int endVal = hBar->maximum();
        if (endVal <= 0 || hBar->value() >= endVal) return;

        auto* anim = new QPropertyAnimation(hBar, "value", this);
        anim->setDuration(250);
        anim->setStartValue(hBar->value());
        anim->setEndValue(endVal);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    });
}
