#include "ColumnView.h"
#include "PreviewManager.h"
#include "FileItem.h"
#include <QScrollBar>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>

// ColumnBrowserList
ColumnBrowserList::ColumnBrowserList(const QString& path, QWidget* parent)
    : QListView(parent)
    , m_path(path)
{
    m_model = new FileSystemModel(this);
    m_model->setRootPath(path);
    setModel(m_model);

    setFixedWidth(240);
    setUniformItemSizes(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

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
    QListView::keyPressEvent(event);
}

// ColumnPreviewPane
ColumnPreviewPane::ColumnPreviewPane(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(280);
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
    m_kindLabel->setStyleSheet(QStringLiteral("color: #777777; font-size: 11px;"));
    layout->addWidget(m_kindLabel);

    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setStyleSheet(QStringLiteral("color: #777777; font-size: 11px;"));
    layout->addWidget(m_sizeLabel);

    m_modifiedLabel = new QLabel(this);
    m_modifiedLabel->setStyleSheet(QStringLiteral("color: #777777; font-size: 11px;"));
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
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget(this);
    m_layout = new QHBoxLayout(m_container);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(1);

    m_previewPane = new ColumnPreviewPane(m_container);
    m_previewPane->hide();

    m_layout->addWidget(m_previewPane);
    m_layout->addStretch();

    setWidget(m_container);
}

void ColumnView::setRootPath(const QString& path)
{
    m_rootPath = path;
    // Clear existing columns
    for (auto* col : m_columns) {
        m_layout->removeWidget(col);
        col->deleteLater();
    }
    m_columns.clear();
    m_previewPane->hide();

    addColumn(path);
}

void ColumnView::addColumn(const QString& path)
{
    int colIndex = static_cast<int>(m_columns.size());
    auto* col = new ColumnBrowserList(path, m_container);

    // Insert column before the preview pane
    m_layout->insertWidget(colIndex, col);
    m_columns.append(col);

    connect(col, &ColumnBrowserList::itemSelected, this, [this, colIndex](const QString& itemPath, bool isDir) {
        onColumnItemSelected(colIndex, itemPath, isDir);
    });

    connect(col, &ColumnBrowserList::itemDoubleClicked, this, [this](const QString& itemPath, bool isDir) {
        if (isDir) {
            emit openPathRequested(itemPath);
        } else {
            QDesktopServices::openUrl(QUrl::fromLocalFile(itemPath));
        }
    });

    connect(col, &ColumnBrowserList::quickLookRequested, this, &ColumnView::quickLookRequested);
    connect(col, &ColumnBrowserList::getInfoRequested, this, &ColumnView::getInfoRequested);

    scrollToRight();
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
    truncateColumnsAfter(columnIndex);

    if (isDir) {
        m_previewPane->hide();
        addColumn(path);
    } else {
        m_previewPane->setItem(path);
        m_previewPane->show();
        scrollToRight();
    }
}

void ColumnView::scrollToRight()
{
    QScrollBar* hBar = horizontalScrollBar();
    if (hBar) {
        hBar->setValue(hBar->maximum());
    }
}
