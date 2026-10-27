#include "GalleryView.h"
#include "PreviewManager.h"
#include "FileItem.h"
#include "FileItemDelegate.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QKeyEvent>

GalleryView::GalleryView(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(1);

    // Hero Preview Top
    m_heroContainer = new QWidget(splitter);
    auto* heroLayout = new QVBoxLayout(m_heroContainer);
    heroLayout->setContentsMargins(16, 16, 16, 16);
    heroLayout->setSpacing(8);

    m_heroImageLabel = new QLabel(m_heroContainer);
    m_heroImageLabel->setAlignment(Qt::AlignCenter);
    m_heroImageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    heroLayout->addWidget(m_heroImageLabel);

    m_heroTextEdit = new QTextEdit(m_heroContainer);
    m_heroTextEdit->setReadOnly(true);
    QFont monoFont(QStringLiteral("monospace"));
    monoFont.setPointSize(10);
    m_heroTextEdit->setFont(monoFont);
    m_heroTextEdit->hide();
    heroLayout->addWidget(m_heroTextEdit);

    m_heroInfoLabel = new QLabel(m_heroContainer);
    m_heroInfoLabel->setAlignment(Qt::AlignCenter);
    m_heroInfoLabel->setStyleSheet(QStringLiteral("color: #777777; font-size: 11px;"));
    heroLayout->addWidget(m_heroInfoLabel);

    // Bottom Filmstrip
    m_filmstrip = new QListView(splitter);
    m_filmstrip->setViewMode(QListView::IconMode);
    m_filmstrip->setFlow(QListView::LeftToRight);
    m_filmstrip->setWrapping(false);
    m_filmstrip->setResizeMode(QListView::Adjust);
    m_filmstrip->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_filmstrip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_filmstrip->setFixedHeight(110);
    m_filmstrip->setSpacing(8);

    auto* delegate = new FileItemDelegate(m_filmstrip);
    delegate->setIconSize(48);
    m_filmstrip->setItemDelegate(delegate);
    m_filmstrip->setGridSize(QSize(72, 90));

    splitter->addWidget(m_heroContainer);
    splitter->addWidget(m_filmstrip);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);

    connect(m_filmstrip, &QListView::doubleClicked, this, &GalleryView::onItemDoubleClicked);
}

void GalleryView::setFileSystemModel(FileSystemModel* model)
{
    m_model = model;
    m_filmstrip->setModel(model);

    connect(m_filmstrip->selectionModel(), &QItemSelectionModel::currentChanged, this, &GalleryView::onSelectionChanged);

    if (model && model->rowCount() > 0) {
        QModelIndex firstIdx = model->index(0, 0);
        m_filmstrip->setCurrentIndex(firstIdx);
        onSelectionChanged();
    }
}

void GalleryView::onSelectionChanged()
{
    QModelIndex idx = m_filmstrip->currentIndex();
    if (!idx.isValid()) {
        m_heroImageLabel->clear();
        m_heroInfoLabel->clear();
        return;
    }

    m_selectedPath = idx.data(FileSystemModel::FilePathRole).toString();
    updateHeroPreview(m_selectedPath);
}

void GalleryView::updateHeroPreview(const QString& path)
{
    FileItem item(path);
    m_heroInfoLabel->setText(QStringLiteral("<b>%1</b> &nbsp;|&nbsp; %2 &nbsp;|&nbsp; %3 &nbsp;|&nbsp; Modified %4")
        .arg(item.fileName(), item.kind(), item.formattedSize(), item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm"))));

    auto& pm = PreviewManager::instance();

    if (pm.isImage(path)) {
        m_heroTextEdit->hide();
        m_heroImageLabel->show();
        QPixmap pix = pm.getThumbnail(path, QSize(600, 380));
        m_heroImageLabel->setPixmap(pix);
    } else if (pm.isText(path)) {
        m_heroImageLabel->hide();
        m_heroTextEdit->show();
        m_heroTextEdit->setPlainText(pm.getTextPreview(path));
    } else {
        m_heroTextEdit->hide();
        m_heroImageLabel->show();
        m_heroImageLabel->setPixmap(item.icon().pixmap(128, 128));
    }
}

void GalleryView::onItemDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    QString path = index.data(FileSystemModel::FilePathRole).toString();
    bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();

    if (isDir) {
        emit openPathRequested(path);
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void GalleryView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        if (!m_selectedPath.isEmpty()) {
            emit quickLookRequested(m_selectedPath);
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
