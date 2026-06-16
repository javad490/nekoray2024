#pragma once

#include <QWidget>
#include <QList>
#include <QString>
#include <QRectF>

// A small self-painted stacked bar chart for the traffic dashboard. Each bar is
// one time bucket; download and upload are stacked so the bar height reads as the
// bucket total. Qt Charts is not a dependency of this project, so this is drawn
// directly with QPainter. Theme-aware (text/grid pull from the palette) with a
// hover tooltip showing the exact figures for a bucket.
class TrafficChartWidget : public QWidget {
    Q_OBJECT

public:
    struct Bar {
        long long bucketStart = 0; // epoch secs, for the hover tooltip's timestamp
        long long down = 0;
        long long up = 0;
        QString label; // x-axis label (already formatted for the bucket size)
    };

    explicit TrafficChartWidget(QWidget* parent = nullptr);

    // Replace the chart contents. labelStride controls how many bars share one
    // x-axis label (so dense series don't overlap their labels).
    void setData(const QList<Bar>& bars, int labelStride = 1);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QList<Bar> bars_;
    int labelStride_ = 1;
    // Bar geometry from the last paint, for hover hit-testing (parallel to bars_).
    QList<QRectF> barRects_;
    int hovered_ = -1;
};
