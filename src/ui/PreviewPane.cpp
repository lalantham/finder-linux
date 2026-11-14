#include "PreviewPane.h"
#include "FileItem.h"
#include "PreviewManager.h"
#include "TagManager.h"
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("previewPane"));
    setMinimumWidth(220);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_stack = new QStackedWidget(this);

    m_imageScrollArea = new QScrollArea(m_stack);
    m_imageScrollArea->setWidgetResizable(true);
    m_imageScrollArea->setAlignment(Qt::AlignCenter);
    m_imageLabel = new QLabel(m_imageScrollArea);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageScrollArea->setWidget(m_imageLabel);
    m_stack->addWidget(m_imageScrollArea);

    m_textView = new QTextEdit(m_stack);
    m_textView->setReadOnly(true);
    m_textView->setFont(QFont(QStringLiteral("monospace"), 9));
    m_stack->addWidget(m_textView);

    m_fallbackPage = new QWidget(m_stack);
    auto* fallbackLayout = new QVBoxLayout(m_fallbackPage);
    fallbackLayout->setAlignment(Qt::AlignCenter);
    m_fallbackIcon = new QLabel(m_fallbackPage);
    m_fallbackIcon->setAlignment(Qt::AlignCenter);
    m_fallbackText = new QLabel(m_fallbackPage);
    m_fallbackText->setAlignment(Qt::AlignCenter);
    m_fallbackText->setWordWrap(true);
    m_fallbackText->setStyleSheet(QStringLiteral("color: #888888;"));
    fallbackLayout->addWidget(m_fallbackIcon);
    fallbackLayout->addWidget(m_fallbackText);
    m_stack->addWidget(m_fallbackPage);

    layout->addWidget(m_stack, 1);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(3);
    m_nameLabel = new QLabel(this);
    m_nameLabel->setWordWrap(true);
    QFont nameFont = m_nameLabel->font();
    nameFont.setBold(true);
    m_nameLabel->setFont(nameFont);
    m_kindLabel = new QLabel(this);
    m_sizeLabel = new QLabel(this);
    m_modifiedLabel = new QLabel(this);
    m_pathLabel = new QLabel(this);
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_tagsLabel = new QLabel(this);
    m_tagsLabel->setWordWrap(true);
    form->addRow(QStringLiteral("Name:"), m_nameLabel);
    form->addRow(QStringLiteral("Kind:"), m_kindLabel);
    form->addRow(QStringLiteral("Size:"), m_sizeLabel);
    form->addRow(QStringLiteral("Modified:"), m_modifiedLabel);
    form->addRow(QStringLiteral("Where:"), m_pathLabel);
    form->addRow(QStringLiteral("Tags:"), m_tagsLabel);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_previewBtn = new QPushButton(QStringLiteral("Quick Look"), this);
    m_propertiesBtn = new QPushButton(QStringLiteral("Get Info"), this);
    buttons->addWidget(m_previewBtn);
    buttons->addWidget(m_propertiesBtn);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(m_previewBtn, &QPushButton::clicked, this, [this]() {
        if (!m_path.isEmpty())
            emit previewRequested(m_path);
    });
    connect(m_propertiesBtn, &QPushButton::clicked, this, [this]() {
        if (!m_path.isEmpty())
            emit propertiesRequested(m_path);
    });
    connect(&PreviewManager::instance(), &PreviewManager::officePreviewReady, this,
            &PreviewPane::onOfficePreviewReady);

    clear();
}

void PreviewPane::clear() {
    m_path.clear();
    m_officePdf.clear();
    m_nameLabel->clear();
    m_kindLabel->clear();
    m_sizeLabel->clear();
    m_modifiedLabel->clear();
    m_pathLabel->clear();
    m_tagsLabel->clear();
    m_previewBtn->setEnabled(false);
    m_propertiesBtn->setEnabled(false);
    m_imageLabel->clear();
    m_textView->clear();
    m_stack->setCurrentWidget(m_fallbackPage);
    m_fallbackIcon->setPixmap(QIcon::fromTheme(QStringLiteral("document-open")).pixmap(64, 64));
    m_fallbackText->setText(QStringLiteral("Select an item to preview."));
}

void PreviewPane::setFile(const QString& path) {
    if (path.isEmpty()) {
        clear();
        return;
    }
    if (m_path == path) {
        return;
    }
    m_path = path;
    m_officePdf.clear();
    updateContent();
}

void PreviewPane::showImage(const QPixmap& pixmap) {
    m_imageLabel->setPixmap(pixmap);
    m_stack->setCurrentWidget(m_imageScrollArea);
}

void PreviewPane::showText(const QString& text) {
    m_textView->setPlainText(text);
    m_stack->setCurrentWidget(m_textView);
}

void PreviewPane::showFallback(const QIcon& icon, const QString& message) {
    m_fallbackIcon->setPixmap(icon.pixmap(64, 64));
    m_fallbackText->setText(message);
    m_stack->setCurrentWidget(m_fallbackPage);
}

void PreviewPane::showPdf(const QString& pdfPath) {
    const QPixmap pixmap = PreviewManager::instance().getPdfPreview(pdfPath, QSize(280, 220));
    if (!pixmap.isNull()) {
        showImage(pixmap);
    } else {
        showFallback(QIcon::fromTheme(QStringLiteral("application-pdf")),
                     QStringLiteral("PDF preview unavailable."));
    }
}

void PreviewPane::updateContent() {
    FileItem item(m_path);
    m_nameLabel->setText(item.fileName());
    m_kindLabel->setText(item.kind());
    m_sizeLabel->setText(item.formattedSize());
    m_modifiedLabel->setText(item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
    m_pathLabel->setText(item.path());
    m_tagsLabel->setText(item.tags().join(QStringLiteral(", ")));
    m_previewBtn->setEnabled(true);
    m_propertiesBtn->setEnabled(true);

    auto& previews = PreviewManager::instance();

    if (item.isDir()) {
        showFallback(item.icon(), QStringLiteral("Folder"));
    } else if (previews.isImage(m_path)) {
        showImage(previews.getThumbnail(m_path, QSize(280, 220)));
    } else if (previews.isPdf(m_path)) {
        showPdf(m_path);
    } else if (previews.isOffice(m_path)) {
        showFallback(QIcon::fromTheme(QStringLiteral("x-office-document")),
                     QStringLiteral("Converting document…"));
        previews.requestOfficePreview(m_path);
    } else if (previews.isText(m_path)) {
        showText(previews.getTextPreview(m_path, 16 * 1024));
    } else if (previews.isVideo(m_path)) {
        const QPixmap pixmap = previews.getThumbnail(m_path, QSize(280, 220));
        if (!pixmap.isNull()) {
            showImage(pixmap);
        } else {
            showFallback(item.icon(), QStringLiteral("Video"));
        }
    } else if (previews.isAudio(m_path)) {
        showFallback(item.icon(), QStringLiteral("Audio"));
    } else {
        showFallback(item.icon(), item.kind());
    }
}

void PreviewPane::onOfficePreviewReady(const QString& sourcePath, const QString& pdfPath,
                                       const QString& error) {
    if (sourcePath != m_path) {
        return;
    }
    if (pdfPath.isEmpty() || !error.isEmpty()) {
        showFallback(QIcon::fromTheme(QStringLiteral("dialog-error")),
                     error.isEmpty() ? QStringLiteral("Document preview unavailable.") : error);
        return;
    }
    m_officePdf = pdfPath;
    showPdf(pdfPath);
}
