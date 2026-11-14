#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTextEdit;

class PreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit PreviewPane(QWidget* parent = nullptr);

    void setFile(const QString& path);
    void clear();
    [[nodiscard]] QString currentPath() const noexcept {
        return m_path;
    }

signals:
    void previewRequested(const QString& path);
    void propertiesRequested(const QString& path);

private slots:
    void onOfficePreviewReady(const QString& sourcePath, const QString& pdfPath,
                              const QString& error);

private:
    void updateContent();
    void showImage(const QPixmap& pixmap);
    void showText(const QString& text);
    void showFallback(const QIcon& icon, const QString& message);
    void showPdf(const QString& pdfPath);

    QString m_path;
    QString m_officePdf;

    QStackedWidget* m_stack{nullptr};
    QLabel* m_imageLabel{nullptr};
    QScrollArea* m_imageScrollArea{nullptr};
    QTextEdit* m_textView{nullptr};
    QWidget* m_fallbackPage{nullptr};
    QLabel* m_fallbackIcon{nullptr};
    QLabel* m_fallbackText{nullptr};

    QLabel* m_nameLabel{nullptr};
    QLabel* m_kindLabel{nullptr};
    QLabel* m_sizeLabel{nullptr};
    QLabel* m_modifiedLabel{nullptr};
    QLabel* m_pathLabel{nullptr};
    QLabel* m_tagsLabel{nullptr};

    QPushButton* m_previewBtn{nullptr};
    QPushButton* m_propertiesBtn{nullptr};
};
