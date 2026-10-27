#pragma once

#include "FileItem.h"
#include <QDialog>
#include <QLabel>
#include <QTextEdit>
#include <QScrollArea>
#include <QPushButton>
#include <QKeyEvent>
#include <QStringList>

class QuickLookDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickLookDialog(QWidget* parent = nullptr);

    void setFiles(const QStringList& filePaths, int currentIndex = 0);
    void showFile(const QString& filePath);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void showNext();
    void showPrevious();

private:
    void updateContent();

    QStringList m_files;
    int m_currentIndex{0};

    QLabel* m_titleLabel{nullptr};
    QLabel* m_subtitleLabel{nullptr};
    QLabel* m_imageLabel{nullptr};
    QTextEdit* m_textViewer{nullptr};
    QScrollArea* m_imageScrollArea{nullptr};
    QWidget* m_fallbackWidget{nullptr};
    QLabel* m_fallbackIconLabel{nullptr};
    QLabel* m_fallbackTextLabel{nullptr};

    QPushButton* m_prevBtn{nullptr};
    QPushButton* m_nextBtn{nullptr};
};
