#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QTabWidget;
class QTableWidget;
class TrafficChartWidget;

// The traffic-statistics dashboard. Reads the per-config and per-app time series
// from TrafficStatsRepo and presents, for a selectable period: a stacked bar
// chart of usage over time, a download/upload/total summary, and breakdown
// tables by profile and by app. The chart and summary follow the active tab's
// dimension so each tab is a self-consistent view.
class DialogTrafficStats : public QDialog {
    Q_OBJECT

public:
    explicit DialogTrafficStats(QWidget* parent = nullptr);
    ~DialogTrafficStats() override;

private:
    void refresh();
    void populateProfileTable(long long fromSecs, long long toSecs);
    void populateAppTable(long long fromSecs, long long toSecs);

    // Window length and chart bucket size for the currently selected period.
    long long selectedWindowSecs() const;
    long long selectedBucketSecs() const;

    QComboBox* periodCombo_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    TrafficChartWidget* chart_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* profileTable_ = nullptr;
    QTableWidget* appTable_ = nullptr;
};
