#include "include/ui/stats/dialog_traffic_stats.h"

#include "include/ui/stats/TrafficChartWidget.h"

#include "include/database/DatabaseManager.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/TrafficStatsRepo.h"
#include "include/database/entities/Profile.h"
#include "include/stats/traffic/TrafficStatsManager.hpp"
#include "include/global/Utils.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
    // Cap on named rows in each breakdown table; usage past this is collapsed into
    // a single "Other" row so the table stays a readable top-N.
    constexpr int kMaxBreakdownRows = 9;

    // Table cell that sorts on its raw byte value rather than the formatted text,
    // so "1.00 GiB" ranks above "900 MiB". Right-aligned, like a figure column.
    class TrafficStatsSizeItem : public QTableWidgetItem {
    public:
        TrafficStatsSizeItem(const QString& text, long long value) : QTableWidgetItem(text) {
            QTableWidgetItem::setData(Qt::UserRole, QVariant::fromValue<qlonglong>(value));
            setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
        bool operator<(const QTableWidgetItem& other) const override {
            return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
        }
    };

    QTableWidget* makeStatsTable(const QStringList& headers) {
        auto* t = new QTableWidget();
        t->setColumnCount(headers.size());
        t->setHorizontalHeaderLabels(headers);
        t->verticalHeader()->setVisible(false);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setAlternatingRowColors(true);
        t->setShowGrid(false);
        t->setSortingEnabled(true);
        t->horizontalHeader()->setHighlightSections(false);
        return t;
    }
}

DialogTrafficStats::DialogTrafficStats(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Traffic Statistics"));
    resize(900, 620);

    auto* root = new QVBoxLayout(this);

    // --- top controls -----------------------------------------------------
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Period:")));
    periodCombo_ = new QComboBox(this);
    periodCombo_->addItem(tr("Last 24 hours"));
    periodCombo_->addItem(tr("Last 7 days"));
    periodCombo_->addItem(tr("Last 30 days"));
    periodCombo_->addItem(tr("Last 90 days"));
    top->addWidget(periodCombo_);
    top->addStretch(1);
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setStyleSheet("font-weight: 600;");
    top->addWidget(summaryLabel_);
    top->addStretch(1);
    auto* refreshBtn = new QPushButton(tr("Refresh"), this);
    top->addWidget(refreshBtn);
    root->addLayout(top);

    // --- chart ------------------------------------------------------------
    chart_ = new TrafficChartWidget(this);
    root->addWidget(chart_, 2);

    // --- breakdown tables -------------------------------------------------
    tabs_ = new QTabWidget(this);
    tabs_->setStyleSheet(R"(
        QTabWidget::tab-bar {
            left: 2px;
        }

        QTabBar {
            background: transparent;
            qproperty-drawBase: 0;
        }

        QTabBar::tab {
            border: 1px solid #777777;
            border-radius: 4px;
            padding: 2px 4px;
            margin-right: 1px;
        }

        QTabBar::tab:selected {
            border: 1px solid palette(highlight);
        }
    )");
    profileTable_ = makeStatsTable({tr("Profile"), tr("Group"), tr("Download"), tr("Upload"), tr("Total")});
    profileTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    profileTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c <= 4; ++c)
        profileTable_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);

    appTable_ = makeStatsTable({tr("App"), tr("Download"), tr("Upload"), tr("Total")});
    appTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c <= 3; ++c)
        appTable_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);

    tabs_->addTab(profileTable_, tr("By Profile"));
    tabs_->addTab(appTable_, tr("By App"));
    root->addWidget(tabs_, 3);

    // The chart and summary reflect the active tab's dimension, so refresh when
    // switching tabs, changing the period, or on explicit request.
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    connect(periodCombo_, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { refresh(); });

    refresh();
}

DialogTrafficStats::~DialogTrafficStats() = default;

long long DialogTrafficStats::selectedWindowSecs() const {
    switch (periodCombo_->currentIndex()) {
        case 1: return 7LL * 86400LL;
        case 2: return 30LL * 86400LL;
        case 3: return 90LL * 86400LL;
        case 0:
        default: return 24LL * 3600LL;
    }
}

long long DialogTrafficStats::selectedBucketSecs() const {
    // Hourly buckets for the 24h view, daily buckets for the longer ranges.
    return periodCombo_->currentIndex() == 0 ? 3600LL : 86400LL;
}

void DialogTrafficStats::refresh() {
    auto* repo = Configs::dataManager ? Configs::dataManager->trafficStatsRepo.get() : nullptr;
    if (!repo) return;

    // Persist the in-progress minute so the dashboard reflects up-to-the-moment
    // usage rather than only what has already rolled over to disk.
    Stats::trafficStatsManager->Flush();

    const long long now = QDateTime::currentSecsSinceEpoch();
    const long long window = selectedWindowSecs();
    const long long bucket = selectedBucketSecs();
    const long long from = now - window;
    // Buckets are stored on UTC boundaries; align them to the viewer's local
    // calendar so an "hour"/"day" bar starts on the local clock, not UTC's.
    const long long tzOffset = QDateTime::currentDateTime().offsetFromUtc();

    populateProfileTable(from, now);
    populateAppTable(from, now);

    const bool byApp = tabs_->currentIndex() == 1;
    const auto series = byApp ? repo->QueryAppSeries(from, now, bucket, tzOffset)
                              : repo->QueryConfigSeries(from, now, bucket, tzOffset);

    QHash<long long, Configs::TrafficSeriesPoint> byBucket;
    byBucket.reserve(series.size());
    long long totalUp = 0, totalDown = 0;
    for (const auto& pt : series) {
        byBucket.insert(pt.bucket_start, pt);
        totalUp += pt.up;
        totalDown += pt.down;
    }

    // Build a contiguous bar list (gaps filled with zeros) so the time axis is
    // continuous even when some buckets saw no traffic. Align to the local boundary
    // with the same offset the query used, so each bar's key matches a series point.
    const long long alignedFrom = ((from + tzOffset) / bucket) * bucket - tzOffset;
    QList<TrafficChartWidget::Bar> bars;
    for (long long b = alignedFrom; b < now; b += bucket) {
        TrafficChartWidget::Bar bar;
        bar.bucketStart = b;
        if (const auto it = byBucket.constFind(b); it != byBucket.constEnd()) {
            bar.down = it->down;
            bar.up = it->up;
        }
        // Label marks the bucket's start; the chart's tooltip shows the full range.
        bar.label = bucket >= 86400LL ? QDateTime::fromSecsSinceEpoch(b).toString("MM/dd")
                                      : QDateTime::fromSecsSinceEpoch(b).toString("HH:mm");
        bars.append(bar);
    }
    const int stride = qMax(1, (static_cast<int>(bars.size()) + 7) / 8);
    chart_->setData(bars, stride, bucket);

    summaryLabel_->setText(tr("Download: %1     Upload: %2     Total: %3")
                               .arg(ReadableSize(totalDown), ReadableSize(totalUp),
                                    ReadableSize(totalDown + totalUp)));
}

void DialogTrafficStats::populateProfileTable(long long fromSecs, long long toSecs) {
    auto* repo = Configs::dataManager->trafficStatsRepo.get();
    auto usage = repo->QueryConfigUsage(fromSecs, toSecs);
    QHash<int, Configs::ConfigMetaRow> meta;
    for (const auto& m : repo->GetAllConfigMeta()) meta.insert(m.profile_id, m);

    // Rank by total, show the busiest few, and fold any remainder into one "Other"
    // row. The data is sorted here (not just left to the table) so the cut is by
    // total even if the user later re-sorts the visible rows by another column.
    std::sort(usage.begin(), usage.end(), [](const Configs::ConfigUsage& a, const Configs::ConfigUsage& b) {
        return (a.down + a.up) > (b.down + b.up);
    });
    const int count = static_cast<int>(usage.size());
    const int shown = qMin(count, kMaxBreakdownRows);
    const bool hasOther = count > kMaxBreakdownRows;
    long long otherDown = 0, otherUp = 0;
    for (int i = shown; i < count; ++i) {
        otherDown += usage[i].down;
        otherUp += usage[i].up;
    }

    profileTable_->setSortingEnabled(false);
    profileTable_->setRowCount(shown + (hasOther ? 1 : 0));
    for (int i = 0; i < shown; ++i) {
        const auto& u = usage[i];
        QString name, group;
        if (const auto it = meta.constFind(u.profile_id); it != meta.constEnd()) {
            name = it->name;
            group = it->group_name;
        }
        if (name.isEmpty()) {
            if (u.profile_id == Stats::DIRECT_STAT_PROFILE_ID) {
                name = tr("Direct");
            } else if (const auto prof = Configs::dataManager->profilesRepo->GetProfile(u.profile_id)) {
                name = prof->name;
            } else {
                name = tr("Profile #%1 (deleted)").arg(u.profile_id);
            }
        }
        profileTable_->setItem(i, 0, new QTableWidgetItem(name));
        profileTable_->setItem(i, 1, new QTableWidgetItem(group));
        profileTable_->setItem(i, 2, new TrafficStatsSizeItem(ReadableSize(u.down), u.down));
        profileTable_->setItem(i, 3, new TrafficStatsSizeItem(ReadableSize(u.up), u.up));
        profileTable_->setItem(i, 4, new TrafficStatsSizeItem(ReadableSize(u.down + u.up), u.down + u.up));
    }
    if (hasOther) {
        profileTable_->setItem(shown, 0, new QTableWidgetItem(tr("Other")));
        profileTable_->setItem(shown, 1, new QTableWidgetItem(QString()));
        profileTable_->setItem(shown, 2, new TrafficStatsSizeItem(ReadableSize(otherDown), otherDown));
        profileTable_->setItem(shown, 3, new TrafficStatsSizeItem(ReadableSize(otherUp), otherUp));
        profileTable_->setItem(shown, 4, new TrafficStatsSizeItem(ReadableSize(otherDown + otherUp), otherDown + otherUp));
    }
    profileTable_->setSortingEnabled(true);
    profileTable_->sortItems(4, Qt::DescendingOrder);
}

void DialogTrafficStats::populateAppTable(long long fromSecs, long long toSecs) {
    auto* repo = Configs::dataManager->trafficStatsRepo.get();
    auto usage = repo->QueryAppUsage(fromSecs, toSecs);

    // Same top-N + "Other" treatment as the profile table.
    std::sort(usage.begin(), usage.end(), [](const Configs::AppUsage& a, const Configs::AppUsage& b) {
        return (a.down + a.up) > (b.down + b.up);
    });
    const int count = static_cast<int>(usage.size());
    const int shown = qMin(count, kMaxBreakdownRows);
    const bool hasOther = count > kMaxBreakdownRows;
    long long otherDown = 0, otherUp = 0;
    for (int i = shown; i < count; ++i) {
        otherDown += usage[i].down;
        otherUp += usage[i].up;
    }

    appTable_->setSortingEnabled(false);
    appTable_->setRowCount(shown + (hasOther ? 1 : 0));
    for (int i = 0; i < shown; ++i) {
        const auto& u = usage[i];
        QString name = u.process_name.isEmpty() ? tr("Unknown") : u.process_name;
        appTable_->setItem(i, 0, new QTableWidgetItem(name));
        appTable_->setItem(i, 1, new TrafficStatsSizeItem(ReadableSize(u.down), u.down));
        appTable_->setItem(i, 2, new TrafficStatsSizeItem(ReadableSize(u.up), u.up));
        appTable_->setItem(i, 3, new TrafficStatsSizeItem(ReadableSize(u.down + u.up), u.down + u.up));
    }
    if (hasOther) {
        appTable_->setItem(shown, 0, new QTableWidgetItem(tr("Other")));
        appTable_->setItem(shown, 1, new TrafficStatsSizeItem(ReadableSize(otherDown), otherDown));
        appTable_->setItem(shown, 2, new TrafficStatsSizeItem(ReadableSize(otherUp), otherUp));
        appTable_->setItem(shown, 3, new TrafficStatsSizeItem(ReadableSize(otherDown + otherUp), otherDown + otherUp));
    }
    appTable_->setSortingEnabled(true);
    appTable_->sortItems(3, Qt::DescendingOrder);
}
