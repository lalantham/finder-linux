#include "ColumnView.h"
#include "AppSettings.h"
#include "DesktopAppsManager.h"
#include "FileItem.h"
#include "PreviewManager.h"
#include "TagManager.h"
#include "ThemeManager.h"
#include "WrappingLabel.h"
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileIconProvider>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kColumnResizeMargin = 6;
constexpr int kMinColumnWidth = 200;
constexpr int kMaxColumnWidth = 1600;
} // namespace

// ColumnItemDelegate
ColumnItemDelegate::ColumnItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void ColumnItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    bool isSelected = option.state & QStyle::State_Selected;
    bool isHovered = option.state & QStyle::State_MouseOver;
    bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();
    bool isDark = ThemeManager::instance().isDark();
    QString itemPath = index.data(FileSystemModel::FilePathRole).toString();
    bool isSpringHover = (!m_springHoverPath.isEmpty() && m_springHoverPath == itemPath);

    // Row selection or hover background
    if (isSpringHover) {
        QColor acc = ThemeManager::instance().accentColor();
        painter->setPen(QPen(acc, 1.5));
        painter->setBrush(QColor(acc.red(), acc.green(), acc.blue(), 45));
        painter->drawRoundedRect(option.rect.adjusted(4, 1, -4, -1), 5, 5);
    } else if (isSelected) {
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
        icon = isDir ? QIcon::fromTheme(QStringLiteral("folder"),
                                        fallbackProv.icon(QFileIconProvider::Folder))
                     : QIcon::fromTheme(QStringLiteral("text-x-generic"),
                                        fallbackProv.icon(QFileIconProvider::File));
    }
    QRect iconRect(textLeft, option.rect.top() + (option.rect.height() - 18) / 2, 18, 18);
    icon.paint(painter, iconRect);
    textLeft += 24;

    // Text label & tag dots
    QStringList tags = index.data(FileSystemModel::FileTagsRole).toStringList();
    int dotSize = 7;
    int dotSpacing = 3;
    int numTags = static_cast<int>(tags.size());
    int dotsWidth = tags.isEmpty() ? 0 : (numTags * (dotSize + dotSpacing) + 4);

    int rightMargin = (isDir ? 24 : 8) + dotsWidth;
    QRect textRect(textLeft, option.rect.top(), option.rect.right() - textLeft - rightMargin,
                   option.rect.height());
    QString text = index.data(Qt::DisplayRole).toString();

    QFont font = option.font;
    font.setPointSize(m_textSize);
    painter->setFont(font);

    QFontMetrics fm(font);
    QString elidedText = fm.elidedText(text, Qt::ElideMiddle, textRect.width());

    painter->setPen(isSelected ? Qt::white : ThemeManager::instance().textColor());
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);

    if (!tags.isEmpty()) {
        int textActualW = qMin(fm.horizontalAdvance(elidedText), textRect.width());
        int dotX = textLeft + textActualW + 6;
        int dotY = option.rect.top() + (option.rect.height() - dotSize) / 2;
        for (int t = 0; t < numTags; ++t) {
            QColor c = TagManager::colorForTag(tags[t]);
            painter->setPen(c.darker(115));
            painter->setBrush(c);
            painter->drawEllipse(dotX + t * (dotSize + dotSpacing), dotY, dotSize, dotSize);
        }
    }

    // Draw the right chevron used for folders.
    if (isDir) {
        QRect chevronRect(option.rect.right() - 20, option.rect.top(), 14, option.rect.height());
        painter->setPen(isSelected ? Qt::white : ThemeManager::instance().subTextColor());
        QFont chFont = font;
        chFont.setBold(true);
        chFont.setPointSize(11);
        painter->setFont(chFont);
        painter->drawText(chevronRect, Qt::AlignCenter, QStringLiteral("›"));
    }

    painter->restore();
}

QSize ColumnItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& /*index*/) const {
    return QSize(option.rect.width(), 26);
}

QWidget* ColumnItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
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

void ColumnItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
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

void ColumnItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                              const QModelIndex& /*index*/) const {
    int textLeft = option.rect.left() + 34;
    QRect textRect(textLeft, option.rect.top() + (option.rect.height() - 24) / 2,
                   option.rect.width() - 40, 24);
    editor->setGeometry(textRect);
}

// ColumnBrowserList
ColumnBrowserList::ColumnBrowserList(const QString& path, QWidget* parent)
    : QListView(parent), m_path(path) {
    m_model = new FileSystemModel(this);
    m_model->setShowHidden(AppSettings::instance().showHiddenFiles());
    m_model->setRootPath(path);
    setModel(m_model);

    const int savedWidth = AppSettings::instance().columnViewWidth(path);
    if (savedWidth > 0) {
        m_preferredWidth = qBound(kMinColumnWidth, savedWidth, kMaxColumnWidth);
        m_userResized = true;
        setFixedWidth(m_preferredWidth);
    } else {
        setFixedWidth(240);
    }
    setUniformItemSizes(true);
    viewport()->setMouseTracking(true);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setMovement(QListView::Static);
    m_delegate = new ColumnItemDelegate(this);
    setItemDelegate(m_delegate);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::MoveAction);

    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    connect(m_springTimer, &QTimer::timeout, this, &ColumnBrowserList::onSpringTimeout);

    setStyleSheet(QStringLiteral("QListView {"
                                 "  background: transparent;"
                                 "  border: none;"
                                 "  border-right: 1px solid rgba(128, 128, 128, 0.22);"
                                 "  outline: none;"
                                 "}"));

    connect(m_model, &FileSystemModel::fileDropRequested, this,
            &ColumnBrowserList::fileDropRequested);
    connect(m_model, &FileSystemModel::directoryLoaded, this, [this]() {
        applyPendingSelection(true);
        updateColumnWidth();
    });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            &ColumnBrowserList::onSelectionChanged);
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &ColumnBrowserList::onSelectionChanged);
    connect(this, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid())
            return;
        QString itemPath = index.data(FileSystemModel::FilePathRole).toString();
        bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();
        emit itemDoubleClicked(itemPath, isDir);
    });
}

void ColumnBrowserList::setItemDelegateTextSize(int size) {
    if (m_delegate) {
        m_delegate->setTextSize(size);
        viewport()->update();
    }
}

QStringList ColumnBrowserList::selectedPaths() const {
    QStringList paths;
    if (!selectionModel())
        return paths;
    for (const auto& idx : selectionModel()->selectedIndexes()) {
        QString p = idx.data(FileSystemModel::FilePathRole).toString();
        if (!p.isEmpty() && !paths.contains(p)) {
            paths.append(p);
        }
    }
    return paths;
}

void ColumnBrowserList::updateColumnWidth() {
    if (!m_model || m_userResized)
        return;

    QFont measurementFont = font();
    measurementFont.setPointSize(10);
    const QFontMetrics metrics(measurementFont);

    // Icon area (10 left + 18 icon + 24 text gap), chevron area, tag dots
    // allowance and a little padding.
    const int textWidth = metrics.horizontalAdvance(m_model->longestFileName());
    m_preferredWidth = qBound(240, textWidth + 120, 640);

    if (!m_widthAnimating) {
        setFixedWidth(m_preferredWidth);
        emit preferredWidthChanged();
    }
}

void ColumnBrowserList::setWidthAnimating(bool animating) {
    m_widthAnimating = animating;
    if (!m_widthAnimating) {
        setFixedWidth(m_preferredWidth);
        emit preferredWidthChanged();
    }
}

void ColumnBrowserList::selectPathWhenLoaded(const QString& path, bool notifyPassive,
                                             bool applyNow) {
    m_pendingSelectPath = path;
    m_pendingNotifyPassive = notifyPassive;
    if (applyNow) {
        applyPendingSelection(false);
    }
}

void ColumnBrowserList::applyPendingSelection(bool afterLoad) {
    if (m_pendingSelectPath.isEmpty()) {
        return;
    }

    QModelIndex match;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex idx = m_model->index(row, 0);
        if (idx.data(FileSystemModel::FilePathRole).toString() == m_pendingSelectPath) {
            match = idx;
            break;
        }
    }

    if (!match.isValid()) {
        if (afterLoad) {
            m_pendingSelectPath.clear();
            m_pendingNotifyPassive = false;
        }
        return;
    }

    const QString selectedPath = m_pendingSelectPath;
    const bool notify = m_pendingNotifyPassive;
    m_pendingSelectPath.clear();
    m_pendingNotifyPassive = false;

    {
        // Passive selection (selection restored after a refresh/load) must not
        // create another column via onSelectionChanged.
        const QSignalBlocker blocker(selectionModel());
        setCurrentIndex(match);
        scrollTo(match);
    }

    if (notify) {
        emit passivePathSelected(selectedPath);
    }
}

void ColumnBrowserList::onSelectionChanged() {
    QStringList paths = selectedPaths();

    // currentChanged and selectionChanged both fire for the same user action;
    // without this guard the next column is created twice per click.
    const QString signature = paths.join(QLatin1Char('\n'));
    if (signature == m_lastSelectionSignature) {
        return;
    }
    m_lastSelectionSignature = signature;

    if (paths.isEmpty()) {
        emit selectionCleared();
        return;
    }
    if (paths.size() == 1) {
        QModelIndex idx = currentIndex();
        if (idx.isValid()) {
            QString itemPath = idx.data(FileSystemModel::FilePathRole).toString();
            bool isDir = idx.data(FileSystemModel::FileIsDirRole).toBool();
            emit itemSelected(itemPath, isDir);
        } else {
            QString itemPath = paths.first();
            emit itemSelected(itemPath, QFileInfo(itemPath).isDir());
        }
    } else {
        emit multipleItemsSelected(paths);
    }
}

namespace {
bool isOnColumnEdge(const QWidget* widget, const QPoint& pos) {
    return pos.x() >= widget->width() - kColumnResizeMargin;
}
} // namespace

void ColumnBrowserList::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton &&
        isOnColumnEdge(viewport(), event->position().toPoint())) {
        m_resizing = true;
        m_resizeStartWidth = width();
        m_resizeStartGlobalX = event->globalPosition().toPoint().x();
        viewport()->setCursor(Qt::SizeHorCursor);
        event->accept();
        return;
    }
    QListView::mousePressEvent(event);
}

void ColumnBrowserList::mouseMoveEvent(QMouseEvent* event) {
    if (m_resizing) {
        const int delta = event->globalPosition().toPoint().x() - m_resizeStartGlobalX;
        if (delta != 0) {
            m_userResized = true;
        }
        m_preferredWidth = qBound(kMinColumnWidth, m_resizeStartWidth + delta, kMaxColumnWidth);
        setFixedWidth(m_preferredWidth);
        event->accept();
        return;
    }

    if (isOnColumnEdge(viewport(), event->position().toPoint())) {
        viewport()->setCursor(Qt::SizeHorCursor);
    } else {
        viewport()->unsetCursor();
    }
    QListView::mouseMoveEvent(event);
}

void ColumnBrowserList::mouseReleaseEvent(QMouseEvent* event) {
    if (m_resizing) {
        m_resizing = false;
        if (!isOnColumnEdge(viewport(), event->position().toPoint())) {
            viewport()->unsetCursor();
        }
        AppSettings::instance().setColumnViewWidth(m_path, m_preferredWidth);
        emit preferredWidthChanged();
        event->accept();
        return;
    }
    QListView::mouseReleaseEvent(event);
}

void ColumnBrowserList::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton &&
        isOnColumnEdge(viewport(), event->position().toPoint())) {
        AppSettings::instance().removeColumnViewWidth(m_path);
        m_userResized = false;
        updateColumnWidth();
        emit preferredWidthChanged();
        event->accept();
        return;
    }
    QListView::mouseDoubleClickEvent(event);
}

bool ColumnBrowserList::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Leave) {
        viewport()->unsetCursor();
    }
    return QListView::viewportEvent(event);
}

void ColumnBrowserList::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QListView::dragEnterEvent(event);
    }
}

void ColumnBrowserList::dragMoveEvent(QDragMoveEvent* event) {
    if (!AppSettings::instance().springLoadedFoldersEnabled() || !event->mimeData()->hasUrls()) {
        resetSpringTimer();
        QListView::dragMoveEvent(event);
        return;
    }

    QModelIndex idx = indexAt(event->position().toPoint());
    if (idx.isValid() && idx.data(FileSystemModel::FileIsDirRole).toBool()) {
        QString targetPath = idx.data(FileSystemModel::FilePathRole).toString();

        bool safe = true;
        for (const auto& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                QString src = url.toLocalFile();
                if (targetPath == src || targetPath.startsWith(src + QLatin1Char('/'))) {
                    safe = false;
                    break;
                }
            }
        }

        if (safe) {
            if (m_springHoverPath != targetPath) {
                m_springHoverPath = targetPath;
                m_delegate->setSpringHoverPath(targetPath);
                viewport()->update();
                m_springTimer->start(AppSettings::instance().springLoadDelayMs());
            }
            event->acceptProposedAction();
            return;
        }
    }

    resetSpringTimer();
    QListView::dragMoveEvent(event);
}

void ColumnBrowserList::dragLeaveEvent(QDragLeaveEvent* event) {
    resetSpringTimer();
    QListView::dragLeaveEvent(event);
}

void ColumnBrowserList::dropEvent(QDropEvent* event) {
    resetSpringTimer();
    QListView::dropEvent(event);
}

void ColumnBrowserList::resetSpringTimer() {
    if (m_springTimer && m_springTimer->isActive()) {
        m_springTimer->stop();
    }
    if (!m_springHoverPath.isEmpty()) {
        m_springHoverPath.clear();
        m_delegate->setSpringHoverPath(QString());
        viewport()->update();
    }
}

void ColumnBrowserList::onSpringTimeout() {
    if (!m_springHoverPath.isEmpty()) {
        QString target = m_springHoverPath;
        resetSpringTimer();
        for (int row = 0; row < m_model->rowCount(); ++row) {
            QModelIndex idx = m_model->index(row, 0);
            if (idx.data(FileSystemModel::FilePathRole).toString() == target) {
                setCurrentIndex(idx);
                scrollTo(idx);
                break;
            }
        }
    }
}

void ColumnBrowserList::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F2) {
        QModelIndex idx = currentIndex();
        if (idx.isValid()) {
            edit(idx);
        }
        return;
    }
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

void ColumnBrowserList::contextMenuEvent(QContextMenuEvent* event) {
    QModelIndex idx = indexAt(event->pos());
    QStringList paths;
    if (idx.isValid()) {
        if (!selectionModel()->isSelected(idx)) {
            setCurrentIndex(idx);
            paths.append(idx.data(FileSystemModel::FilePathRole).toString());
        } else {
            paths = selectedPaths();
            if (paths.isEmpty()) {
                paths.append(idx.data(FileSystemModel::FilePathRole).toString());
            }
        }
    }
    emit contextMenuRequested(event->globalPos(), paths, m_path);
}

// ColumnPreviewPane
ColumnPreviewPane::ColumnPreviewPane(QWidget* parent) : QWidget(parent) {
    setFixedWidth(280);
    setStyleSheet(QStringLiteral("ColumnPreviewPane {"
                                 "  background: transparent;"
                                 "  border-left: 1px solid rgba(128, 128, 128, 0.22);"
                                 "}"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 20, 16, 20);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignTop);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedHeight(140);
    layout->addWidget(m_iconLabel);

    m_nameLabel = new WrapAnywhereLabel(QString(), this);
    QFont nf = m_nameLabel->font();
    nf.setBold(true);
    nf.setPointSize(11);
    m_nameLabel->setFont(nf);
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

    m_tagsContainer = new QWidget(this);
    m_tagsLayout = new QHBoxLayout(m_tagsContainer);
    m_tagsLayout->setContentsMargins(0, 4, 0, 4);
    m_tagsLayout->setSpacing(6);
    layout->addWidget(m_tagsContainer);

    layout->addSpacing(12);

    m_qlBtn = new QPushButton(QStringLiteral("Quick Look"), this);
    connect(m_qlBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentPath.isEmpty())
            emit quickLookRequested(m_currentPath);
    });
    layout->addWidget(m_qlBtn);

    connect(&PreviewManager::instance(), &PreviewManager::thumbnailReady, this,
            [this](const QString& path) {
                if (path != m_currentPath)
                    return;
                auto& pm = PreviewManager::instance();
                if (!pm.isImage(path))
                    return;
                const QPixmap pix = pm.iconThumbnail(path, 256);
                if (!pix.isNull())
                    m_iconLabel->setPixmap(pix);
            });

    layout->addStretch();
}

void ColumnPreviewPane::setItem(const QString& path) {
    m_currentPath = path;
    if (m_qlBtn)
        m_qlBtn->setEnabled(true);
    FileItem item(path);

    m_nameLabel->setText(item.fileName());
    m_kindLabel->setText(QStringLiteral("Kind: %1").arg(item.kind()));
    m_sizeLabel->setText(QStringLiteral("Size: %1").arg(item.formattedSize()));
    m_modifiedLabel->setText(
        QStringLiteral("Modified: %1")
            .arg(item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm"))));

    // Clear and re-populate tag badges
    QLayoutItem* child;
    while ((child = m_tagsLayout->takeAt(0)) != nullptr) {
        if (child->widget())
            delete child->widget();
        delete child;
    }

    QStringList tags = item.tags();
    if (!tags.isEmpty()) {
        auto* tagHeader = new QLabel(QStringLiteral("Tags:"), m_tagsContainer);
        tagHeader->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
        m_tagsLayout->addWidget(tagHeader);

        for (const auto& t : tags) {
            auto* badge = new QLabel(t, m_tagsContainer);
            QColor c = TagManager::colorForTag(t);
            badge->setStyleSheet(QStringLiteral("QLabel {"
                                                "  background: rgba(%1, %2, %3, 0.2);"
                                                "  color: rgb(%1, %2, %3);"
                                                "  border: 1px solid rgba(%1, %2, %3, 0.5);"
                                                "  border-radius: 4px;"
                                                "  padding: 1px 6px;"
                                                "  font-size: 10px;"
                                                "  font-weight: bold;"
                                                "}")
                                     .arg(c.red())
                                     .arg(c.green())
                                     .arg(c.blue()));
            m_tagsLayout->addWidget(badge);
        }
        m_tagsLayout->addStretch();
        m_tagsContainer->show();
    } else {
        m_tagsContainer->hide();
    }

    auto& pm = PreviewManager::instance();
    if (pm.isImage(path)) {
        const QPixmap pix = pm.iconThumbnail(path, 256);
        m_iconLabel->setPixmap(pix.isNull() ? item.icon().pixmap(96, 96) : pix);
    } else {
        m_iconLabel->setPixmap(item.icon().pixmap(96, 96));
    }
}

void ColumnPreviewPane::setMultipleItems(const QStringList& paths) {
    m_currentPath.clear();
    QFileIconProvider iconProv;
    m_iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("document-multiple"),
                                            iconProv.icon(QFileIconProvider::Folder))
                               .pixmap(64, 64));
    m_nameLabel->setText(QStringLiteral("%1 items selected").arg(paths.size()));
    m_kindLabel->setText(QStringLiteral("Multiple Items"));

    qint64 totalBytes = 0;
    int dirCount = 0;
    int fileCount = 0;
    for (const auto& p : paths) {
        QFileInfo fi(p);
        if (fi.isDir()) {
            dirCount++;
        } else {
            fileCount++;
            totalBytes += fi.size();
        }
    }
    QString countStr;
    if (dirCount > 0 && fileCount > 0) {
        countStr = QStringLiteral("%1 folders, %2 files").arg(dirCount).arg(fileCount);
    } else if (dirCount > 0) {
        countStr = QStringLiteral("%1 folders").arg(dirCount);
    } else {
        countStr = QStringLiteral("%1 files").arg(fileCount);
    }
    m_sizeLabel->setText(
        QStringLiteral("Size: %1 (%2)").arg(FileItem::formatSize(totalBytes), countStr));
    m_modifiedLabel->clear();

    QLayoutItem* child;
    while ((child = m_tagsLayout->takeAt(0)) != nullptr) {
        if (child->widget())
            delete child->widget();
        delete child;
    }
    m_tagsContainer->hide();
    if (m_qlBtn) {
        m_qlBtn->setEnabled(false);
    }
}

// ColumnView
ColumnView::ColumnView(QWidget* parent)
    : QScrollArea(parent), m_showHidden(AppSettings::instance().showHiddenFiles()) {
    m_sortColumn = AppSettings::instance().sortColumn();
    m_sortOrder = AppSettings::instance().sortOrder();
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget(this);
    m_layout = new QHBoxLayout(m_container);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_previewPane = new ColumnPreviewPane(m_container);
    m_previewPane->hide();
    connect(m_previewPane, &ColumnPreviewPane::quickLookRequested, this,
            &ColumnView::quickLookRequested);
    connect(m_previewPane, &ColumnPreviewPane::getInfoRequested, this,
            &ColumnView::getInfoRequested);

    m_layout->addWidget(m_previewPane);
    m_layout->addStretch();

    setWidget(m_container);
}

void ColumnView::setRootPath(const QString& path) {
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

void ColumnView::navigateToPath(const QString& targetPath) {
    QString clean = QDir::cleanPath(targetPath);
    if (!QDir(clean).exists())
        return;

    bool oldRefreshing = m_isRefreshing;
    m_isRefreshing = true;

    // 1. Check if clean is already one of the open columns
    for (int i = 0; i < m_columns.size(); ++i) {
        if (m_columns[i]->path() == clean) {
            truncateColumnsAfter(i);
            m_columns[i]->clearSelection();
            m_previewPane->hide();
            m_selectedPath.clear();
            m_selectedPaths.clear();
            m_activeColumnIndex = i;
            scrollToRightAnimated();
            m_isRefreshing = oldRefreshing;
            return;
        }
    }

    // 2. If clean is a direct child of the last column, select it and add column
    if (!m_columns.isEmpty()) {
        auto* lastCol = m_columns.last();
        if (clean.startsWith(lastCol->path() + QLatin1Char('/'))) {
            QString subName = clean.mid(lastCol->path().length() + 1);
            if (!subName.contains(QLatin1Char('/'))) {
                // The parent column may still be scanning; select once loaded.
                lastCol->selectPathWhenLoaded(clean, false);
                m_previewPane->hide();
                addColumn(clean, true);
                m_activeColumnIndex = static_cast<int>(m_columns.size()) - 1;
                m_isRefreshing = oldRefreshing;
                return;
            }
        }
    }

    // 3. Otherwise (e.g. sidebar clicked, or non-adjacent path), reset root to clean
    m_isRefreshing = oldRefreshing;
    setRootPath(clean);
}

QString ColumnView::activeDirectory() const {
    if (m_columns.isEmpty()) {
        return m_rootPath;
    }
    return m_columns.last()->path();
}

int ColumnView::activeColumnRowCount() const {
    if (m_columns.isEmpty() || !m_columns.last()->model()) {
        return 0;
    }
    return m_columns.last()->model()->rowCount();
}

void ColumnView::refresh(const QString& targetToSelect) {
    if (m_isRefreshing)
        return;
    m_isRefreshing = true;

    QString selected = !targetToSelect.isEmpty() ? targetToSelect : m_selectedPath;

    for (int i = 0; i < m_columns.size(); ++i) {
        auto* col = m_columns[i];
        if (!QDir(col->path()).exists()) {
            truncateColumnsAfter(i - 1);
            break;
        }

        // Determine which item was / should be selected in this column:
        QString colSelectedPath;
        if (i + 1 < m_columns.size()) {
            colSelectedPath = m_columns[i + 1]->path();
        } else {
            if (!targetToSelect.isEmpty() &&
                QFileInfo(targetToSelect).absolutePath() == col->path()) {
                colSelectedPath = targetToSelect;
            } else {
                QModelIndex currIdx = col->currentIndex();
                if (currIdx.isValid()) {
                    colSelectedPath = currIdx.data(FileSystemModel::FilePathRole).toString();
                } else if (!selected.isEmpty() &&
                           QFileInfo(selected).absolutePath() == col->path()) {
                    colSelectedPath = selected;
                }
            }
        }

        col->blockSignals(true);
        if (col->selectionModel()) {
            col->selectionModel()->blockSignals(true);
        }

        // Arm the selection before the (possibly asynchronous) reload; it is
        // applied when directoryLoaded arrives.
        col->selectPathWhenLoaded(colSelectedPath, i == m_columns.size() - 1, false);
        col->model()->refresh();

        if (col->selectionModel()) {
            col->selectionModel()->blockSignals(false);
        }
        col->blockSignals(false);
    }

    if (!m_selectedPath.isEmpty() && !QFileInfo::exists(m_selectedPath)) {
        // The selected item in the last column was deleted.
        m_selectedPath.clear();
        m_previewPane->hide();
    }

    m_isRefreshing = false;
    scrollToRightAnimated();
}

void ColumnView::setShowHidden(bool show) {
    m_showHidden = show;
    for (auto* col : m_columns) {
        col->model()->setShowHidden(show);
    }
}

void ColumnView::setTextSize(int size) {
    m_textSize = qBound(8, size, 24);
    for (auto* col : m_columns) {
        col->setItemDelegateTextSize(m_textSize);
    }
}

void ColumnView::applySort(int column, Qt::SortOrder order) {
    m_sortColumn = column;
    m_sortOrder = order;
    for (auto* col : m_columns) {
        col->model()->sort(column, order);
    }
}

void ColumnView::addColumn(const QString& path, bool animate) {
    int colIndex = static_cast<int>(m_columns.size());
    auto* col = new ColumnBrowserList(path, m_container);
    col->setItemDelegateTextSize(m_textSize);
    col->model()->setShowHidden(m_showHidden);
    col->model()->setDirectoriesFirst(AppSettings::instance().directoriesFirst());
    col->model()->sort(m_sortColumn, m_sortOrder);

    m_layout->insertWidget(colIndex, col);
    m_columns.append(col);
    col->updateColumnWidth();

    connect(col, &ColumnBrowserList::preferredWidthChanged, this, [this, col]() {
        if (!m_columns.isEmpty() && m_columns.last() == col) {
            scrollToRightAnimated();
        }
    });

    connect(col->model(), &FileSystemModel::directoryLoaded, this,
            [this, col](const QString&, int) {
                if (!m_columns.isEmpty() && m_columns.last() == col) {
                    emit contentLoaded();
                }
            });
    connect(col->model(), &FileSystemModel::itemRenamed, this, &ColumnView::itemRenamed);
    connect(col, &ColumnBrowserList::passivePathSelected, this, [this, col](const QString& path) {
        if (!m_columns.isEmpty() && m_columns.last() == col) {
            m_selectedPath = path;
            if (m_previewPane->isVisible() && !QFileInfo(path).isDir()) {
                m_previewPane->setItem(path);
            }
        }
    });

    connect(col, &ColumnBrowserList::itemSelected, this,
            [this, col](const QString& itemPath, bool isDir) {
                if (m_isRefreshing)
                    return;
                int idx = static_cast<int>(m_columns.indexOf(col));
                if (idx >= 0) {
                    onColumnItemSelected(idx, itemPath, isDir);
                }
            });

    connect(col, &ColumnBrowserList::multipleItemsSelected, this,
            [this, col](const QStringList& paths) {
                if (m_isRefreshing)
                    return;
                int idx = static_cast<int>(m_columns.indexOf(col));
                if (idx >= 0) {
                    onColumnMultipleItemsSelected(idx, paths);
                }
            });

    connect(col, &ColumnBrowserList::selectionCleared, this, [this, col]() {
        if (m_isRefreshing)
            return;
        int idx = static_cast<int>(m_columns.indexOf(col));
        if (idx >= 0) {
            onColumnSelectionCleared(idx);
        }
    });

    connect(col, &ColumnBrowserList::itemDoubleClicked, this,
            [this](const QString& itemPath, bool isDir) {
                if (isDir) {
                    emit openPathRequested(itemPath);
                } else {
                    DesktopAppsManager::launchFile(itemPath);
                }
            });

    connect(col, &ColumnBrowserList::quickLookRequested, this, &ColumnView::quickLookRequested);
    connect(col, &ColumnBrowserList::getInfoRequested, this, &ColumnView::getInfoRequested);
    connect(col, &ColumnBrowserList::contextMenuRequested, this,
            [this](const QPoint& globalPos, const QStringList& selectedPaths,
                   const QString& currentDir) {
                emit contextMenuRequested(globalPos, selectedPaths, currentDir);
            });
    connect(col, &ColumnBrowserList::fileDropRequested, this, &ColumnView::fileDropRequested);

    // Keyboard navigation across columns
    connect(col, &ColumnBrowserList::navigateLeftRequested, this, [this, col]() {
        int idx = static_cast<int>(m_columns.indexOf(col));
        if (idx > 0 && idx - 1 < m_columns.size()) {
            m_columns[idx - 1]->setFocus();
        }
    });

    connect(col, &ColumnBrowserList::navigateRightRequested, this, [this, col]() {
        int idx = static_cast<int>(m_columns.indexOf(col));
        if (idx >= 0 && idx + 1 < m_columns.size()) {
            auto* nextCol = m_columns[idx + 1];
            nextCol->setFocus();
            if (nextCol->model()->rowCount() > 0 && !nextCol->currentIndex().isValid()) {
                nextCol->setCurrentIndex(nextCol->model()->index(0, 0));
            }
        }
    });

    // Animate column slide-in width if traversing
    if (animate) {
        col->setWidthAnimating(true);
        col->setMaximumWidth(0);
        auto* anim = new QPropertyAnimation(col, "maximumWidth", this);
        anim->setDuration(200);
        anim->setStartValue(0);
        anim->setEndValue(col->preferredWidth());
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, this, [col]() {
            col->setMaximumWidth(QWIDGETSIZE_MAX);
            col->setWidthAnimating(false);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    scrollToRightAnimated();
}

void ColumnView::truncateColumnsAfter(int columnIndex) {
    while (m_columns.size() > columnIndex + 1) {
        auto* col = m_columns.takeLast();
        m_layout->removeWidget(col);
        col->deleteLater();
    }
}

void ColumnView::onColumnItemSelected(int columnIndex, const QString& path, bool isDir) {
    if (m_isRefreshing)
        return;
    m_activeColumnIndex = columnIndex;
    m_selectedPath = path;
    m_selectedPaths = {path};
    truncateColumnsAfter(columnIndex);

    if (isDir) {
        m_previewPane->hide();
        addColumn(path, true);
        emit activeDirectoryChanged(path);
    } else {
        m_previewPane->setItem(path);
        m_previewPane->show();
        scrollToRightAnimated();
        QString colDir = (columnIndex >= 0 && columnIndex < m_columns.size())
                             ? m_columns[columnIndex]->path()
                             : m_rootPath;
        emit activeDirectoryChanged(colDir);
    }
}

void ColumnView::onColumnMultipleItemsSelected(int columnIndex, const QStringList& paths) {
    if (m_isRefreshing)
        return;
    m_activeColumnIndex = columnIndex;
    m_selectedPaths = paths;
    m_selectedPath.clear();
    truncateColumnsAfter(columnIndex);

    m_previewPane->setMultipleItems(paths);
    m_previewPane->show();
    scrollToRightAnimated();

    QString colDir = (columnIndex >= 0 && columnIndex < m_columns.size())
                         ? m_columns[columnIndex]->path()
                         : m_rootPath;
    emit activeDirectoryChanged(colDir);
}

void ColumnView::onColumnSelectionCleared(int columnIndex) {
    if (m_isRefreshing)
        return;
    if (m_activeColumnIndex == columnIndex) {
        m_selectedPath.clear();
        m_selectedPaths.clear();
        m_previewPane->hide();
        truncateColumnsAfter(columnIndex);

        QString colDir = (columnIndex >= 0 && columnIndex < m_columns.size())
                             ? m_columns[columnIndex]->path()
                             : m_rootPath;
        emit activeDirectoryChanged(colDir);
    }
}

QStringList ColumnView::selectedPaths() const {
    int numCols = static_cast<int>(m_columns.size());
    if (m_activeColumnIndex >= 0 && m_activeColumnIndex < numCols) {
        QStringList paths = m_columns[m_activeColumnIndex]->selectedPaths();
        if (!paths.isEmpty())
            return paths;
    }
    for (int i = numCols - 1; i >= 0; --i) {
        QStringList paths = m_columns[i]->selectedPaths();
        if (!paths.isEmpty())
            return paths;
    }
    return m_selectedPaths.isEmpty()
               ? (!m_selectedPath.isEmpty() ? QStringList{m_selectedPath} : QStringList{})
               : m_selectedPaths;
}

QListView* ColumnView::activeView() const {
    if (m_activeColumnIndex >= 0 && m_activeColumnIndex < m_columns.size()) {
        return m_columns.at(m_activeColumnIndex);
    }
    return nullptr;
}

void ColumnView::selectAll() {
    int numCols = static_cast<int>(m_columns.size());
    int colIdx = (m_activeColumnIndex >= 0 && m_activeColumnIndex < numCols) ? m_activeColumnIndex
                                                                             : (numCols - 1);
    if (colIdx >= 0 && colIdx < numCols) {
        m_columns[colIdx]->selectAll();
    }
}

bool ColumnView::triggerRename() {
    int numCols = static_cast<int>(m_columns.size());
    int colIdx = (m_activeColumnIndex >= 0 && m_activeColumnIndex < numCols) ? m_activeColumnIndex
                                                                             : (numCols - 1);
    if (colIdx >= 0 && colIdx < numCols) {
        auto* col = m_columns[colIdx];
        QModelIndex idx = col->currentIndex();
        if (idx.isValid()) {
            col->edit(idx);
            return true;
        }
    }
    return false;
}

void ColumnView::scrollToRightAnimated() {
    QScrollBar* hBar = horizontalScrollBar();
    if (!hBar)
        return;

    if (m_container) {
        m_container->adjustSize();
    }

    QTimer::singleShot(40, this, [this, hBar]() {
        int endVal = hBar->maximum();
        if (endVal <= 0 || hBar->value() >= endVal)
            return;

        auto* anim = new QPropertyAnimation(hBar, "value", this);
        anim->setDuration(250);
        anim->setStartValue(hBar->value());
        anim->setEndValue(endVal);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    });
}
