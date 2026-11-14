#include "QuickLookDialog.h"
#include "AppChooserDialog.h"
#include "ArchiveService.h"
#include "DesktopAppsManager.h"
#include "PreviewManager.h"
#include "TagManager.h"
#include <QApplication>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <limits>

#ifdef HAVE_QT_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QSlider>
#include <QVideoWidget>
#endif

QuickLookDialog::QuickLookDialog(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground, false);
    resize(720, 540);

    // Center on screen
    if (auto* screen = QGuiApplication::primaryScreen()) {
        move(screen->geometry().center() - rect().center());
    }

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Top header bar
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(8);

    m_prevBtn = new QPushButton(QStringLiteral("◀"), this);
    m_prevBtn->setFixedSize(28, 28);
    m_prevBtn->setFocusPolicy(Qt::NoFocus);
    m_nextBtn = new QPushButton(QStringLiteral("▶"), this);
    m_nextBtn->setFixedSize(28, 28);
    m_nextBtn->setFocusPolicy(Qt::NoFocus);

    auto* titleContainer = new QVBoxLayout();
    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(12);
    m_titleLabel->setFont(titleFont);

    m_subtitleLabel = new QLabel(this);
    QFont subFont = m_subtitleLabel->font();
    subFont.setPointSize(9);
    m_subtitleLabel->setFont(subFont);
    m_subtitleLabel->setStyleSheet(QStringLiteral("color: #888888;"));

    titleContainer->addWidget(m_titleLabel);
    titleContainer->addWidget(m_subtitleLabel);

    auto* closeBtn = new QPushButton(QStringLiteral("✕"), this);
    closeBtn->setFixedSize(28, 28);
    closeBtn->setFocusPolicy(Qt::NoFocus);

    headerLayout->addWidget(m_prevBtn);
    headerLayout->addWidget(m_nextBtn);
    headerLayout->addSpacing(8);
    headerLayout->addLayout(titleContainer);
    headerLayout->addStretch();

    m_prevPageBtn = new QPushButton(QStringLiteral("‹"), this);
    m_prevPageBtn->setFixedSize(26, 26);
    m_prevPageBtn->setFocusPolicy(Qt::NoFocus);
    m_pageLabel = new QLabel(this);
    m_pageLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 11px;"));
    m_nextPageBtn = new QPushButton(QStringLiteral("›"), this);
    m_nextPageBtn->setFixedSize(26, 26);
    m_nextPageBtn->setFocusPolicy(Qt::NoFocus);
    m_prevPageBtn->hide();
    m_pageLabel->hide();
    m_nextPageBtn->hide();
    headerLayout->addWidget(m_prevPageBtn);
    headerLayout->addWidget(m_pageLabel);
    headerLayout->addWidget(m_nextPageBtn);

    m_openWithBtn = new QPushButton(QStringLiteral("Open With..."), this);
    m_openWithBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_openWithBtn, &QPushButton::clicked, this, &QuickLookDialog::onOpenWith);
    headerLayout->addWidget(m_openWithBtn);
    headerLayout->addWidget(closeBtn);
    mainLayout->addLayout(headerLayout);

    m_pdfSearchBar = new QWidget(this);
    auto* pdfSearchLayout = new QHBoxLayout(m_pdfSearchBar);
    pdfSearchLayout->setContentsMargins(0, 0, 0, 0);
    pdfSearchLayout->setSpacing(6);
    m_pdfSearchEdit = new QLineEdit(m_pdfSearchBar);
    m_pdfSearchEdit->setPlaceholderText(QStringLiteral("Find in document"));
    auto* pdfFindBtn = new QPushButton(QStringLiteral("Find"), m_pdfSearchBar);
    pdfSearchLayout->addWidget(m_pdfSearchEdit, 1);
    pdfSearchLayout->addWidget(pdfFindBtn);
    m_pdfSearchBar->hide();
    mainLayout->addWidget(m_pdfSearchBar);
    connect(pdfFindBtn, &QPushButton::clicked, this, &QuickLookDialog::findPdfMatch);
    connect(m_pdfSearchEdit, &QLineEdit::returnPressed, this, &QuickLookDialog::findPdfMatch);

    // Image view
    m_imageScrollArea = new QScrollArea(this);
    m_imageScrollArea->setWidgetResizable(true);
    m_imageScrollArea->setAlignment(Qt::AlignCenter);
    m_imageLabel = new QLabel(m_imageScrollArea);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageScrollArea->setWidget(m_imageLabel);
    mainLayout->addWidget(m_imageScrollArea);

    // Text view
    m_textViewer = new QTextEdit(this);
    m_textViewer->setReadOnly(true);
    QFont monoFont(QStringLiteral("monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    m_textViewer->setFont(monoFont);
    mainLayout->addWidget(m_textViewer);

#ifdef HAVE_QT_MULTIMEDIA
    // Media playback view
    m_mediaWidget = new QWidget(this);
    auto* mediaLayout = new QVBoxLayout(m_mediaWidget);
    mediaLayout->setContentsMargins(0, 0, 0, 0);
    mediaLayout->setSpacing(8);

    m_audioIconLabel = new QLabel(m_mediaWidget);
    m_audioIconLabel->setAlignment(Qt::AlignCenter);
    m_audioIconLabel->setMinimumHeight(180);
    mediaLayout->addWidget(m_audioIconLabel, 1);

    m_videoWidget = new QVideoWidget(m_mediaWidget);
    m_videoWidget->setMinimumHeight(260);
    m_videoWidget->setStyleSheet(QStringLiteral("background-color: #000000;"));
    mediaLayout->addWidget(m_videoWidget, 1);

    auto* mediaControls = new QHBoxLayout();
    mediaControls->setSpacing(8);
    m_playPauseBtn = new QPushButton(QStringLiteral("▶"), m_mediaWidget);
    m_playPauseBtn->setFixedSize(32, 28);
    m_playPauseBtn->setFocusPolicy(Qt::NoFocus);
    m_positionSlider = new QSlider(Qt::Horizontal, m_mediaWidget);
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setFocusPolicy(Qt::NoFocus);
    m_positionLabel = new QLabel(QStringLiteral("0:00"), m_mediaWidget);
    m_durationLabel = new QLabel(QStringLiteral("0:00"), m_mediaWidget);
    mediaControls->addWidget(m_playPauseBtn);
    mediaControls->addWidget(m_positionSlider, 1);
    mediaControls->addWidget(m_positionLabel);
    mediaControls->addWidget(new QLabel(QStringLiteral("/"), m_mediaWidget));
    mediaControls->addWidget(m_durationLabel);
    mediaLayout->addLayout(mediaControls);
    mainLayout->addWidget(m_mediaWidget);
    m_mediaWidget->hide();

    m_audioOutput = new QAudioOutput(this);
    m_audioOutput->setVolume(1.0f);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoWidget);

    auto formatTime = [](qint64 milliseconds) {
        const qint64 totalSeconds = qMax<qint64>(0, milliseconds / 1000);
        const qint64 hours = totalSeconds / 3600;
        const qint64 minutes = (totalSeconds % 3600) / 60;
        const qint64 seconds = totalSeconds % 60;
        if (hours > 0) {
            return QStringLiteral("%1:%2:%3")
                .arg(hours)
                .arg(minutes, 2, 10, QLatin1Char('0'))
                .arg(seconds, 2, 10, QLatin1Char('0'));
        }
        return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
    };

    connect(m_playPauseBtn, &QPushButton::clicked, this, [this]() {
        if (!m_player)
            return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else {
            m_player->play();
        }
    });
    connect(m_positionSlider, &QSlider::sliderPressed, this, [this]() { m_seekDragging = true; });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this]() {
        m_seekDragging = false;
        if (m_player) {
            m_player->setPosition(m_positionSlider->value());
        }
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this, formatTime](qint64 position) {
        if (!m_seekDragging) {
            m_positionSlider->setValue(static_cast<int>(position));
        }
        m_positionLabel->setText(formatTime(position));
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this, formatTime](qint64 duration) {
        const int bounded =
            static_cast<int>(qMin<qint64>(duration, std::numeric_limits<int>::max()));
        m_positionSlider->setRange(0, bounded);
        m_durationLabel->setText(formatTime(duration));
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                m_playPauseBtn->setText(state == QMediaPlayer::PlayingState ? QStringLiteral("⏸")
                                                                            : QStringLiteral("▶"));
            });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error) {
        if (error == QMediaPlayer::NoError) {
            return;
        }
        const QString failedSource = m_player->source().toLocalFile();
        if (!m_files.isEmpty() && failedSource != m_files.value(m_currentIndex)) {
            return;
        }
        stopMediaPlayback();
        m_fallbackIconLabel->setPixmap(
            QIcon::fromTheme(QStringLiteral("dialog-error")).pixmap(96, 96));
        m_fallbackTextLabel->setText(QStringLiteral("Playback failed"));
        m_fallbackWidget->show();
    });
    connect(this, &QDialog::finished, this, [this]() { stopMediaPlayback(); });
#endif

    // Fallback view
    m_fallbackWidget = new QWidget(this);
    auto* fbLayout = new QVBoxLayout(m_fallbackWidget);
    fbLayout->setAlignment(Qt::AlignCenter);
    fbLayout->setSpacing(12);
    m_fallbackIconLabel = new QLabel(m_fallbackWidget);
    m_fallbackIconLabel->setAlignment(Qt::AlignCenter);
    m_fallbackTextLabel = new QLabel(m_fallbackWidget);
    m_fallbackTextLabel->setAlignment(Qt::AlignCenter);
    fbLayout->addWidget(m_fallbackIconLabel);
    fbLayout->addWidget(m_fallbackTextLabel);
    mainLayout->addWidget(m_fallbackWidget);

    connect(m_prevBtn, &QPushButton::clicked, this, &QuickLookDialog::showPrevious);
    connect(m_nextBtn, &QPushButton::clicked, this, &QuickLookDialog::showNext);
    connect(&PreviewManager::instance(), &PreviewManager::officePreviewReady, this,
            &QuickLookDialog::onOfficePreviewReady);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Apply preview HUD styling
    setStyleSheet(QStringLiteral("QuickLookDialog {"
                                 "  background-color: #242528;"
                                 "  color: #f0f0f0;"
                                 "  border: 1px solid #3c3d40;"
                                 "  border-radius: 12px;"
                                 "}"
                                 "QLabel { color: #f0f0f0; }"
                                 "QPushButton {"
                                 "  background: rgba(255, 255, 255, 0.12);"
                                 "  color: #ffffff;"
                                 "  border: 1px solid rgba(255, 255, 255, 0.18);"
                                 "  border-radius: 6px;"
                                 "  font-size: 11px;"
                                 "}"
                                 "QPushButton:hover {"
                                 "  background: rgba(255, 255, 255, 0.22);"
                                 "}"
                                 "QTextEdit {"
                                 "  background-color: #1a1a1c;"
                                 "  color: #dcdcdc;"
                                 "  border: 1px solid #3c3d40;"
                                 "  border-radius: 8px;"
                                 "  padding: 8px;"
                                 "}"
                                 "QScrollArea {"
                                 "  border: none;"
                                 "  background-color: transparent;"
                                 "}"));
}

void QuickLookDialog::stopMediaPlayback() {
#ifdef HAVE_QT_MULTIMEDIA
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
    if (m_mediaWidget) {
        m_mediaWidget->hide();
    }
    if (m_videoWidget) {
        m_videoWidget->hide();
    }
    if (m_audioIconLabel) {
        m_audioIconLabel->hide();
    }
    m_seekDragging = false;
    if (m_positionSlider) {
        m_positionSlider->setRange(0, 0);
        m_positionSlider->setValue(0);
    }
    if (m_positionLabel) {
        m_positionLabel->setText(QStringLiteral("0:00"));
    }
    if (m_durationLabel) {
        m_durationLabel->setText(QStringLiteral("0:00"));
    }
#endif
}

void QuickLookDialog::stopSlideshow() {
    m_slideshow = false;
    if (m_slideshowTimer) {
        m_slideshowTimer->stop();
    }
}

void QuickLookDialog::resetPdfState() {
    m_pdfPath.clear();
    m_pdfPage = 0;
    m_pdfPageCount = 0;
    if (m_pageLabel)
        m_pageLabel->hide();
    if (m_prevPageBtn)
        m_prevPageBtn->hide();
    if (m_nextPageBtn)
        m_nextPageBtn->hide();
}

void QuickLookDialog::displayPdfPage() {
    if (m_pdfPath.isEmpty()) {
        return;
    }
    const QPixmap pix =
        PreviewManager::instance().getPdfPreview(m_pdfPath, QSize(680, 440), m_pdfPage);
    if (pix.isNull()) {
        m_fallbackIconLabel->setPixmap(
            QIcon::fromTheme(QStringLiteral("dialog-error")).pixmap(96, 96));
        m_fallbackTextLabel->setText(QStringLiteral("Could not render this document page."));
        m_fallbackWidget->show();
        m_imageScrollArea->hide();
        m_textViewer->hide();
        return;
    }

    m_imageBase = pix;
    m_zoom = 1.0;
    m_rotation = 0;
    m_fitImage = true;
    fitImageToView();
    QTimer::singleShot(0, this, &QuickLookDialog::fitImageToView);
    m_imageScrollArea->show();
    m_fallbackWidget->hide();
    m_textViewer->hide();

    const bool multipage = m_pdfPageCount > 1;
    m_pageLabel->setText(QStringLiteral("Page %1 of %2").arg(m_pdfPage + 1).arg(m_pdfPageCount));
    m_pageLabel->setVisible(multipage);
    m_prevPageBtn->setVisible(multipage);
    m_nextPageBtn->setVisible(multipage);
    if (m_pdfSearchBar)
        m_pdfSearchBar->show();
    m_prevPageBtn->setEnabled(m_pdfPage > 0);
    m_nextPageBtn->setEnabled(m_pdfPage + 1 < m_pdfPageCount);
}

void QuickLookDialog::applyImageTransform() {
    if (m_imageBase.isNull()) {
        return;
    }
    QPixmap pix = m_imageBase;
    if (m_rotation != 0) {
        pix = pix.transformed(QTransform().rotate(m_rotation), Qt::SmoothTransformation);
    }
    if (!qFuzzyCompare(m_zoom, 1.0)) {
        pix = pix.scaled(pix.size() * m_zoom, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    m_imageLabel->setPixmap(pix);
}

void QuickLookDialog::fitImageToView() {
    if (m_imageBase.isNull() || !m_imageScrollArea) {
        return;
    }
    QSize baseSize = m_imageBase.size();
    if (m_rotation == 90 || m_rotation == 270) {
        baseSize.transpose();
    }
    const QSize viewport = m_imageScrollArea->viewport()->size();
    if (baseSize.isEmpty() || viewport.width() <= 8 || viewport.height() <= 8) {
        return;
    }

    const qreal widthScale =
        static_cast<qreal>(viewport.width() - 8) / static_cast<qreal>(baseSize.width());
    const qreal heightScale =
        static_cast<qreal>(viewport.height() - 8) / static_cast<qreal>(baseSize.height());
    // Fit large images down to the window, but do not upscale small images.
    m_zoom = qBound<qreal>(0.1, qMin<qreal>(1.0, qMin(widthScale, heightScale)), 4.0);
    applyImageTransform();
}

void QuickLookDialog::showNextPage() {
    if (m_pdfPath.isEmpty() || m_pdfPage + 1 >= m_pdfPageCount)
        return;
    ++m_pdfPage;
    displayPdfPage();
}

void QuickLookDialog::showPreviousPage() {
    if (m_pdfPath.isEmpty() || m_pdfPage <= 0)
        return;
    --m_pdfPage;
    displayPdfPage();
}

void QuickLookDialog::toggleSlideshow() {
    if (m_slideshow) {
        stopSlideshow();
        return;
    }
    if (m_files.size() < 2)
        return;
    m_slideshow = true;
    m_slideshowTimer->start();
}

void QuickLookDialog::setFiles(const QStringList& filePaths, int currentIndex) {
    m_files = filePaths;
    m_currentIndex = (currentIndex >= 0 && currentIndex < m_files.size()) ? currentIndex : 0;
    updateContent();
}

void QuickLookDialog::showFile(const QString& filePath) {
    m_files = QStringList{filePath};
    m_currentIndex = 0;
    updateContent();
}

void QuickLookDialog::showNext() {
    if (m_files.isEmpty())
        return;
    m_currentIndex = static_cast<int>((m_currentIndex + 1) % m_files.size());
    updateContent();
}

void QuickLookDialog::showPrevious() {
    if (m_files.isEmpty())
        return;
    int sz = static_cast<int>(m_files.size());
    m_currentIndex = (m_currentIndex - 1 + sz) % sz;
    updateContent();
}

void QuickLookDialog::updateContent() {
    stopMediaPlayback();

    if (m_files.isEmpty() || m_currentIndex < 0 || m_currentIndex >= m_files.size()) {
        m_titleLabel->setText(QStringLiteral("No item selected"));
        m_subtitleLabel->clear();
        m_imageScrollArea->hide();
        m_textViewer->hide();
        m_fallbackWidget->hide();
        return;
    }

    QString currentPath = m_files.at(m_currentIndex);
    FileItem item(currentPath);

    m_titleLabel->setText(item.fileName());
    QString sub = QStringLiteral("%1 — %2 — Modified %3")
                      .arg(item.kind(), item.formattedSize(),
                           item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
    if (!item.tags().isEmpty()) {
        sub += QStringLiteral(" — Tags: %1").arg(item.tags().join(QStringLiteral(", ")));
    }
    m_subtitleLabel->setText(sub);

    m_prevBtn->setEnabled(m_files.size() > 1);
    m_nextBtn->setEnabled(m_files.size() > 1);

    auto& pm = PreviewManager::instance();

    if (pm.isImage(currentPath)) {
        m_imageBase = pm.getThumbnail(currentPath, QSize(1200, 900));
        m_zoom = 1.0;
        m_rotation = 0;
        m_fitImage = true;
        fitImageToView();
        QTimer::singleShot(0, this, &QuickLookDialog::fitImageToView);
        m_imageScrollArea->show();
        m_textViewer->hide();
        m_fallbackWidget->hide();
    } else if (pm.isPdf(currentPath)) {
        m_pdfPath = currentPath;
        m_pdfPage = 0;
        m_pdfPageCount = pm.pdfPageCount(currentPath);
        if (m_pdfPageCount > 0) {
            displayPdfPage();
        } else {
            m_fallbackIconLabel->setPixmap(item.icon().pixmap(96, 96));
            m_fallbackTextLabel->setText(QStringLiteral("PDF Preview unavailable"));
            m_fallbackWidget->show();
            m_imageScrollArea->hide();
            m_textViewer->hide();
        }
    } else if (pm.isOffice(currentPath)) {
        m_fallbackIconLabel->setPixmap(item.icon().pixmap(96, 96));
        m_fallbackTextLabel->setText(
            QStringLiteral("<b>%1</b><br>Converting document…").arg(item.fileName()));
        m_fallbackWidget->show();
        m_imageScrollArea->hide();
        m_textViewer->hide();
        pm.requestOfficePreview(currentPath);
    } else if (pm.isVideo(currentPath)) {
#ifdef HAVE_QT_MULTIMEDIA
        if (m_player && m_videoWidget) {
            m_audioIconLabel->hide();
            m_videoWidget->show();
            m_mediaWidget->show();
            m_imageScrollArea->hide();
            m_textViewer->hide();
            m_fallbackWidget->hide();
            m_player->setSource(QUrl::fromLocalFile(currentPath));
            m_player->play();
        } else
#endif
        {
            QPixmap pix = pm.getThumbnail(currentPath, QSize(1200, 900));
            if (!pix.isNull()) {
                m_imageBase = pix;
                m_zoom = 1.0;
                m_rotation = 0;
                m_fitImage = true;
                fitImageToView();
                QTimer::singleShot(0, this, &QuickLookDialog::fitImageToView);
                m_imageScrollArea->show();
                m_textViewer->hide();
                m_fallbackWidget->hide();
            } else {
                m_fallbackIconLabel->setPixmap(item.icon().pixmap(96, 96));
                m_fallbackTextLabel->setText(
                    QStringLiteral("<b>%1</b><br>%2<br>%3")
                        .arg(item.fileName(), item.kind(), item.formattedSize()));
                m_fallbackWidget->show();
                m_imageScrollArea->hide();
                m_textViewer->hide();
            }
        }
    } else if (pm.isAudio(currentPath)) {
#ifdef HAVE_QT_MULTIMEDIA
        if (m_player && m_audioIconLabel) {
            m_videoWidget->hide();
            m_audioIconLabel->setPixmap(item.icon().pixmap(160, 160));
            m_audioIconLabel->show();
            m_mediaWidget->show();
            m_imageScrollArea->hide();
            m_textViewer->hide();
            m_fallbackWidget->hide();
            m_player->setSource(QUrl::fromLocalFile(currentPath));
            m_player->play();
        } else
#endif
        {
            m_fallbackIconLabel->setPixmap(item.icon().pixmap(96, 96));
            m_fallbackTextLabel->setText(
                QStringLiteral("<b>%1</b><br>%2<br>%3")
                    .arg(item.fileName(), item.kind(), item.formattedSize()));
            m_fallbackWidget->show();
            m_imageScrollArea->hide();
            m_textViewer->hide();
        }
    } else if (pm.isFont(currentPath)) {
        const int fontId = QFontDatabase::addApplicationFont(currentPath);
        const QStringList families =
            fontId >= 0 ? QFontDatabase::applicationFontFamilies(fontId) : QStringList();
        if (!families.isEmpty()) {
            m_textViewer->setFont(QFont(families.first(), 22));
            m_textViewer->setPlainText(QStringLiteral(
                "AaBbCcDdEeFfGg\n0123456789\nThe quick brown fox jumps over the lazy dog."));
            m_textViewer->show();
        } else {
            setTextViewerMode(false);
            m_textViewer->setPlainText(QStringLiteral("Font preview unavailable."));
            m_textViewer->show();
        }
        m_imageScrollArea->hide();
        m_fallbackWidget->hide();
    } else if (QFileInfo(currentPath).suffix().compare(QStringLiteral("md"), Qt::CaseInsensitive) ==
                   0 ||
               QFileInfo(currentPath)
                       .suffix()
                       .compare(QStringLiteral("markdown"), Qt::CaseInsensitive) == 0) {
        setTextViewerMode(true);
        m_textViewer->setMarkdown(pm.getTextPreview(currentPath, 256 * 1024));
        m_textViewer->show();
        m_imageScrollArea->hide();
        m_fallbackWidget->hide();
    } else if (QFileInfo(currentPath)
                       .suffix()
                       .compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0 ||
               QFileInfo(currentPath)
                       .suffix()
                       .compare(QStringLiteral("htm"), Qt::CaseInsensitive) == 0) {
        setTextViewerMode(true);
        m_textViewer->setHtml(pm.getTextPreview(currentPath, 256 * 1024));
        m_textViewer->show();
        m_imageScrollArea->hide();
        m_fallbackWidget->hide();
    } else if (ArchiveService::isArchive(currentPath)) {
        QVector<ArchiveEntryInfo> entries;
        QString error;
        setTextViewerMode(false);
        if (ArchiveService::listEntries(currentPath, &entries, 500, &error)) {
            QStringList lines;
            lines.append(
                QStringLiteral("%1 (%2 entries)").arg(item.fileName()).arg(entries.size()));
            lines.append(QString());
            for (const ArchiveEntryInfo& entry : entries) {
                lines.append(QStringLiteral("%1 %2  %3")
                                 .arg(entry.isDir ? QStringLiteral("[dir]")
                                                  : FileItem::formatSize(entry.size),
                                      QStringLiteral(" "), entry.path));
            }
            m_textViewer->setPlainText(lines.join(QLatin1Char('\n')));
        } else {
            m_textViewer->setPlainText(
                QStringLiteral("Archive preview unavailable: %1").arg(error));
        }
        m_textViewer->show();
        m_imageScrollArea->hide();
        m_fallbackWidget->hide();
    } else if (pm.isText(currentPath)) {
        setTextViewerMode(false);
        m_textViewer->setPlainText(pm.getTextPreview(currentPath));
        m_textViewer->show();
        m_imageScrollArea->hide();
        m_fallbackWidget->hide();
    } else {
        m_fallbackIconLabel->setPixmap(item.icon().pixmap(96, 96));
        m_fallbackTextLabel->setText(QStringLiteral("<b>%1</b><br>%2<br>%3")
                                         .arg(item.fileName(), item.kind(), item.formattedSize()));
        m_fallbackWidget->show();
        m_imageScrollArea->hide();
        m_textViewer->hide();
    }
}

void QuickLookDialog::onOfficePreviewReady(const QString& sourcePath, const QString& pdfPath,
                                           const QString& error) {
    if (m_files.isEmpty() || m_currentIndex < 0 || m_currentIndex >= m_files.size()) {
        return;
    }
    if (m_files.at(m_currentIndex) != sourcePath) {
        return;
    }

    if (pdfPath.isEmpty() || !error.isEmpty()) {
        m_fallbackIconLabel->setPixmap(
            QIcon::fromTheme(QStringLiteral("dialog-error")).pixmap(96, 96));
        m_fallbackTextLabel->setText(
            QStringLiteral("<b>Office preview unavailable</b><br>%1")
                .arg(error.isEmpty() ? QStringLiteral("Could not convert this document.")
                                     : error.toHtmlEscaped()));
        m_fallbackWidget->show();
        m_imageScrollArea->hide();
        m_textViewer->hide();
        return;
    }

    m_pdfPath = pdfPath;
    m_pdfPage = 0;
    m_pdfPageCount = PreviewManager::instance().pdfPageCount(pdfPath);
    displayPdfPage();
}

void QuickLookDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (m_fitImage && !m_imageBase.isNull()) {
        QTimer::singleShot(0, this, &QuickLookDialog::fitImageToView);
    }
}

void QuickLookDialog::findPdfMatch() {
    if (!m_pdfSearchEdit || m_pdfPath.isEmpty())
        return;
    const int page = PreviewManager::instance().findPdfPage(m_pdfPath, m_pdfSearchEdit->text());
    if (page >= 0) {
        m_pdfPage = page;
        displayPdfPage();
    } else {
        m_pdfSearchEdit->setStyleSheet(QStringLiteral("QLineEdit { color: #ff7777; }"));
        QTimer::singleShot(1000, this, [this]() {
            if (m_pdfSearchEdit)
                m_pdfSearchEdit->setStyleSheet(QString());
        });
    }
}

void QuickLookDialog::onOpenWith() {
    if (m_files.isEmpty() || m_currentIndex < 0 || m_currentIndex >= m_files.size())
        return;
    const QString path = m_files.at(m_currentIndex);
    QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(path, QMimeDatabase::MatchDefault).name();
    AppChooserDialog dialog(mime, QFileInfo(path).fileName(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!dialog.customCommand().isEmpty()) {
        DesktopAppsManager::launchCustomCommand(dialog.customCommand(), path);
    } else if (!dialog.selectedAppId().isEmpty()) {
        DesktopAppsManager::launchApp(dialog.selectedAppId(), path);
    }
    accept();
}

void QuickLookDialog::setTextViewerMode(bool rendered) {
    if (!m_textViewer)
        return;
    QFont font = rendered ? QApplication::font() : QFont(QStringLiteral("monospace"), 10);
    if (!rendered)
        font.setStyleHint(QFont::Monospace);
    m_textViewer->setFont(font);
}

void QuickLookDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Escape) {
        accept();
        return;
    }
    if (event->key() == Qt::Key_F11) {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
        return;
    }
    if (event->key() == Qt::Key_Left) {
        showPrevious();
        return;
    }
    if (event->key() == Qt::Key_Right) {
        showNext();
        return;
    }
    if (event->key() == Qt::Key_PageUp) {
        showPreviousPage();
        return;
    }
    if (event->key() == Qt::Key_PageDown) {
        showNextPage();
        return;
    }
    if (event->key() == Qt::Key_S) {
        toggleSlideshow();
        return;
    }
    if (event->key() == Qt::Key_R) {
        m_rotation = (m_rotation + 90) % 360;
        if (m_fitImage) {
            fitImageToView();
        } else {
            applyImageTransform();
        }
        return;
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
        m_fitImage = false;
        m_zoom = qMin<qreal>(4.0, m_zoom * 1.2);
        applyImageTransform();
        return;
    }
    if (event->key() == Qt::Key_Minus) {
        m_fitImage = false;
        m_zoom = qMax<qreal>(0.1, m_zoom / 1.2);
        applyImageTransform();
        return;
    }
    if (event->key() == Qt::Key_0) {
        m_rotation = 0;
        m_fitImage = true;
        fitImageToView();
        return;
    }
    QDialog::keyPressEvent(event);
}

void QuickLookDialog::wheelEvent(QWheelEvent* event) {
    if (!m_imageBase.isNull() && (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        const qreal factor = event->angleDelta().y() > 0 ? 1.1 : 0.9;
        m_fitImage = false;
        m_zoom = qBound<qreal>(0.1, m_zoom * factor, 4.0);
        applyImageTransform();
        event->accept();
        return;
    }
    QDialog::wheelEvent(event);
}
