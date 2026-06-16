#pragma once
#include <QMutex>
#include <QString>
#include <QHash>
#include <QSet>
#include <QPair>

namespace Stats
{
    constexpr int IDKEY = 242315;

    enum ConnectionSort
    {
        Default,
        ByDownload,
        ByUpload,
        ByProcess,
        ByTraffic,
        ByOutbound,
        ByProtocol
    };

    class ConnectionMetadata
    {
        public:
        QString id;
        long long createdAtMs;
        long long upload;
        long long download;
        QString outbound;
        QString network;
        QString dest;
        QString protocol;
        QString domain;
        QString process;     // basename, e.g. chrome.exe
        QString processPath; // full path (icon lookup etc.)
        long long closedAtMs = 0; // 0 while live
    };

    class ConnectionLister
    {
    public:
        ConnectionLister();

        bool suspend = true;

        void Loop();

        void ForceUpdate();

        void stopLoop();

        void setSort(ConnectionSort newSort);

    private:
        void update();

        QMutex mu;

        bool stop = false;

        std::shared_ptr<QSet<QString>> state;

        ConnectionSort sort = Default;

        bool asc = false;

        // Per-app traffic diffing: last seen cumulative (up, down) per live
        // connection id, and the set of closed-connection ids already counted
        // (the closed ring is non-draining, so we dedup by id). Both self-prune
        // each poll, so they stay bounded and survive core restarts cleanly.
        QHash<QString, QPair<qint64, qint64>> lastBytes_;
        QSet<QString> accountedClosed_;
    };

    extern ConnectionLister* connection_lister;
}
