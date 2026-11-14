#pragma once

#include "FileItem.h"
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTextEdit>

class QSlider;
class QTimer;
class QWheelEvent;
class QLineEdit;
#ifdef HAVE_QT_MULTIMEDIA
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
#endif

class QuickLookDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickLookDialog(QWidget* parent = nullptr);

    void setFiles(const QStringList& filePaths, int currentIndex = 0);
    void showFile(const QString& filePath);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void showNext();
    void showPrevious();
    void showNextPage();
    void showPreviousPage();
    void toggleSlideshow();
    void findPdfMatch();
    void onOpenWith();
    void onOfficePreviewReady(const QString& sourcePath, const QString& pdfPath,
                              const QString& error);

private:
    void updateContent();
    void stopMediaPlayback();
    void stopSlideshow();
    void displayPdfPage();
    void applyImageTransform();
    void fitImageToView();
    void resetPdfState();
    void setTextViewerMode(bool rendered);

    QStringList m_files;
    int m_currentIndex{0};

    QLabel* m_titleLabel{nullptr};
    QLabel* m_subtitleLabel{nullptr};
    QLabel* m_pageLabel{nullptr};
    QPushButton* m_prevPageBtn{nullptr};
    QPushButton* m_nextPageBtn{nullptr};
    QWidget* m_pdfSearchBar{nullptr};
    QLineEdit* m_pdfSearchEdit{nullptr};
    QPushButton* m_openWithBtn{nullptr};
    QTimer* m_slideshowTimer{nullptr};
    bool m_slideshow{false};

    QLabel* m_imageLabel{nullptr};
    QPixmap m_imageBase;
    qreal m_zoom{1.0};
    int m_rotation{0};
    bool m_fitImage{true};
    QString m_pdfPath;
    int m_pdfPage{0};
    int m_pdfPageCount{0};
    QTextEdit* m_textViewer{nullptr};
    QScrollArea* m_imageScrollArea{nullptr};
    QWidget* m_fallbackWidget{nullptr};
    QLabel* m_fallbackIconLabel{nullptr};
    QLabel* m_fallbackTextLabel{nullptr};

    QWidget* m_mediaWidget{nullptr};
    QPushButton* m_playPauseBtn{nullptr};
    QSlider* m_positionSlider{nullptr};
    QLabel* m_positionLabel{nullptr};
    QLabel* m_durationLabel{nullptr};
    QLabel* m_audioIconLabel{nullptr};
    bool m_seekDragging{false};

#ifdef HAVE_QT_MULTIMEDIA
    QMediaPlayer* m_player{nullptr};
    QAudioOutput* m_audioOutput{nullptr};
    QVideoWidget* m_videoWidget{nullptr};
#endif

    QPushButton* m_prevBtn{nullptr};
    QPushButton* m_nextBtn{nullptr};
};
