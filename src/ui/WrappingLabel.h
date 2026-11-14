#pragma once

#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTextLayout>
#include <QTextOption>
#include <cmath>

// Layouts size rows from a label's size hint, which ignores the width the label
// actually receives. Wrapped text then overruns the next row or is clipped.
// Keep the height in sync with heightForWidth() at the real width instead.
class WrappingLabel : public QLabel {
public:
    explicit WrappingLabel(const QString& text, QWidget* parent = nullptr)
        : QLabel(text, parent)
    {
        setWordWrap(true);
        setTextInteractionFlags(Qt::TextSelectableByMouse);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        const int wrappedHeight = heightForWidth(width());
        if (wrappedHeight > 0 && height() != wrappedHeight) {
            setFixedHeight(wrappedHeight);
        }
    }
};

// QLabel's word wrap breaks only at word boundaries, so a long filename with no
// spaces is clipped. This label lays the text out with WrapAnywhere instead.
class WrapAnywhereLabel : public QLabel {
public:
    explicit WrapAnywhereLabel(const QString& text = QString(), QWidget* parent = nullptr)
        : QLabel(text, parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }

    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override { return laidOutHeight(width); }

    [[nodiscard]] QSize sizeHint() const override
    {
        const int w = width() > 0 ? width() : 240;
        return QSize(QLabel::sizeHint().width(), laidOutHeight(w));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));

        QTextLayout layout(text(), font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAnywhere);
        option.setAlignment(Qt::AlignHCenter);
        layout.setTextOption(option);

        layout.beginLayout();
        qreal y = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(width());
            line.setPosition(QPointF(0, y));
            y += line.height();
        }
        layout.endLayout();

        layout.draw(&painter, QPointF(0, 0));
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        const int height = laidOutHeight(width());
        if (height > 0 && this->height() != height) {
            setFixedHeight(height);
        }
    }

private:
    [[nodiscard]] int laidOutHeight(int width) const
    {
        if (text().isEmpty()) return fontMetrics().height();

        QTextLayout layout(text(), font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAnywhere);
        layout.setTextOption(option);

        layout.beginLayout();
        qreal y = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(qMax(0, width));
            line.setPosition(QPointF(0, y));
            y += line.height();
        }
        layout.endLayout();

        return static_cast<int>(std::ceil(y));
    }
};
