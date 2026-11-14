#include "QuickLookDialog.h"
#include "PreviewManager.h"
#include "TagManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QScreen>
#include <QGuiApplication>
#include <QUrl>
#include <QIcon>
#include <limits>

#ifdef HAVE_QT_MULTIMEDIA
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QSlider>
#endif

QuickLookDialog::QuickLookDialog(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::FramelessWindowHint)
{
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
    headerLayout->addWidget(closeBtn);
    mainLayout->addLayout(headerLayout);

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
        return QStringLiteral("%1:%2")
            .arg(minutes)
            .arg(seconds, 2, 10, QLatin1Char('0'));
    };

    connect(m_playPauseBtn, &QPushButton::clicked, this, [this]() {
        if (!m_player) return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else {
            m_player->play();
        }
    });
    connect(m_positionSlider, &QSlider::sliderPressed, this, [this]() {
        m_seekDragging = true;
    });
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
        const int bounded = static_cast<int>(qMin<qint64>(duration,
            std::numeric_limits<int>::max()));
        m_positionSlider->setRange(0, bounded);
        m_durationLabel->setText(formatTime(duration));
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
        [this](QMediaPlayer::PlaybackState state) {
            m_playPauseBtn->setText(
                state == QMediaPlayer::PlayingState ? QStringLiteral("⏸")
                                                    : QStringLiteral("▶"));
        });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error error) {
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
    connect(this, &QDialog::finished, this, [this]() {
        stopMediaPlayback();
    });
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
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Apply preview HUD styling
    setStyleSheet(QStringLiteral(
        "QuickLookDialog {"
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
        "}"
    ));
}

void QuickLookDialog::stopMediaPlayback()
{
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

void QuickLookDialog::setFiles(const QStringList& filePaths, int currentIndex)
{
    m_files = filePaths;
    m_currentIndex = (currentIndex >= 0 && currentIndex < m_files.size()) ? currentIndex : 0;
    updateContent();
}

void QuickLookDialog::showFile(const QString& filePath)
{
    m_files = QStringList{filePath};
    m_currentIndex = 0;
    updateContent();
}

void QuickLookDialog::showNext()
{
    if (m_files.isEmpty()) return;
    m_currentIndex = static_cast<int>((m_currentIndex + 1) % m_files.size());
    updateContent();
}

void QuickLookDialog::showPrevious()
{
    if (m_files.isEmpty()) return;
    int sz = static_cast<int>(m_files.size());
    m_currentIndex = (m_currentIndex - 1 + sz) % sz;
    updateContent();
}

void QuickLookDialog::updateContent()
{
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
        .arg(item.kind(), item.formattedSize(), item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
    if (!item.tags().isEmpty()) {
        sub += QStringLiteral(" — Tags: %1").arg(item.tags().join(QStringLiteral(", ")));
    }
    m_subtitleLabel->setText(sub);

    m_prevBtn->setEnabled(m_files.size() > 1);
    m_nextBtn->setEnabled(m_files.size() > 1);

    auto& pm = PreviewManager::instance();

    if (pm.isImage(currentPath)) {
        QPixmap pix = pm.getThumbnail(currentPath, QSize(680, 440));
        m_imageLabel->setPixmap(pix);
        m_imageScrollArea->show();
        m_textViewer->hide();
        m_fallbackWidget->hide();
    } else if (pm.isPdf(currentPath)) {
        QPixmap pix = pm.getPdfPreview(currentPath, QSize(680, 440));
        if (!pix.isNull()) {
            m_imageLabel->setPixmap(pix);
            m_imageScrollArea->show();
            m_textViewer->hide();
            m_fallbackWidget->hide();
        } else {
            m_fallbackIconLabel->setPixmap(item.icon().pixmap(96, 96));
            m_fallbackTextLabel->setText(QStringLiteral("PDF Preview unavailable"));
            m_fallbackWidget->show();
            m_imageScrollArea->hide();
            m_textViewer->hide();
        }
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
            QPixmap pix = pm.getThumbnail(currentPath, QSize(680, 440));
            if (!pix.isNull()) {
                m_imageLabel->setPixmap(pix);
                m_imageScrollArea->show();
                m_textViewer->hide();
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
            m_fallbackTextLabel->setText(QStringLiteral("<b>%1</b><br>%2<br>%3")
                .arg(item.fileName(), item.kind(), item.formattedSize()));
            m_fallbackWidget->show();
            m_imageScrollArea->hide();
            m_textViewer->hide();
        }
    } else if (pm.isText(currentPath)) {
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

void QuickLookDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Escape) {
        accept();
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
    QDialog::keyPressEvent(event);
}
