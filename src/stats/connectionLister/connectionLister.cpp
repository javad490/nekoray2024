#include <QThread>
#include <core/server/gen/libcore.pb.h>
#include <include/api/RPC.h>
#include "include/ui/mainwindow_interface.h"
#include <include/stats/connections/connectionLister.hpp>
#include "include/stats/traffic/TrafficStatsManager.hpp"



namespace Stats
{
    ConnectionLister* connection_lister = new ConnectionLister();

    ConnectionLister::ConnectionLister()
    {
        state = std::make_shared<QSet<QString>>();
    }

    void ConnectionLister::ForceUpdate()
    {
        mu.lock();
        update();
        mu.unlock();
    }


    void ConnectionLister::Loop()
    {
        while (true)
        {
            if (stop) return;
            QThread::msleep(1000);

            if (suspend || !Configs::dataManager->settingsRepo->enable_stats) continue;

            mu.lock();
            update();
            mu.unlock();
        }
    }

    // Map one wire connection into the in-memory metadata used by the UI table.
    static ConnectionMetadata metaFromProto(const libcore::ConnectionMetaData& conn)
    {
        ConnectionMetadata c;
        c.id = QString::fromStdString(conn.id.value());
        c.createdAtMs = conn.created_at.value();
        c.dest = QString::fromStdString(conn.dest.value());
        c.upload = conn.upload.value();
        c.download = conn.download.value();
        c.domain = QString::fromStdString(conn.domain.value());
        c.network = QString::fromStdString(conn.network.value());
        c.outbound = QString::fromStdString(conn.outbound.value());
        c.process = QString::fromStdString(conn.process.value());
        c.processPath = QString::fromStdString(conn.process_path.value());
        c.protocol = QString::fromStdString(conn.protocol.value());
        c.closedAtMs = conn.closed_at.value();
        return c;
    }

    void ConnectionLister::update()
    {
        libcore::QueryConnectionsResp resp = API::defaultClient->QueryConnections();

        QMap<QString, ConnectionMetadata> toUpdate;
        QMap<QString, ConnectionMetadata> toAdd;
        QSet<QString> newState;
        QList<ConnectionMetadata> sorted;
        for (const auto& conn : resp.active)
        {
            auto c = metaFromProto(conn);
            if (sort == Default)
            {
                if (state->contains(c.id))
                {
                    toUpdate[c.id] = c;
                } else
                {
                    toAdd[c.id] = c;
                }
            } else
            {
                sorted.append(c);
            }
            newState.insert(c.id);
        }

        state->clear();
        for (const auto& id : newState) state->insert(id);

        // One enriched poll, two consumers: the connection table above, and the
        // per-app traffic module here. Diff each connection's cumulative byte
        // counters across the live set plus the recently-closed ring (deduped by
        // id), so a connection that opened and closed between polls is still
        // counted. Gated by the traffic-stats toggle; the lister itself already
        // requires connection stats (enable_stats) to run.
        if (!Configs::dataManager->settingsRepo->disable_traffic_stats)
        {
            QHash<QString, QPair<qint64, qint64>> newLast;
            QSet<QString> currentClosed;

            auto credit = [&](const libcore::ConnectionMetaData& cm, qint64 curUp, qint64 curDown)
            {
                const QString id = QString::fromStdString(cm.id.value());
                qint64 baseUp = 0, baseDown = 0;
                if (const auto it = lastBytes_.constFind(id); it != lastBytes_.constEnd())
                {
                    baseUp = it->first;
                    baseDown = it->second;
                }
                qint64 dUp = curUp - baseUp;
                qint64 dDown = curDown - baseDown;
                if (dUp < 0) dUp = 0; // counters only grow; guard against any reset
                if (dDown < 0) dDown = 0;
                if (dUp == 0 && dDown == 0) return;
                QString name = QString::fromStdString(cm.process.value());
                if (name.isEmpty()) name = "Unknown";
                trafficStatsManager->AddAppDelta(name, QString::fromStdString(cm.process_path.value()), dUp, dDown);
            };

            for (const auto& cm : resp.active)
            {
                const qint64 up = cm.upload.value();
                const qint64 down = cm.download.value();
                credit(cm, up, down);
                newLast.insert(QString::fromStdString(cm.id.value()), {up, down});
            }
            for (const auto& cm : resp.closed)
            {
                const QString id = QString::fromStdString(cm.id.value());
                currentClosed.insert(id);
                if (accountedClosed_.contains(id)) continue;
                credit(cm, cm.upload.value(), cm.download.value());
            }
            lastBytes_ = newLast;             // drop evicted / now-closed live ids
            accountedClosed_ = currentClosed; // everything in the ring is accounted
        }

        if (sort == Default)
        {
            runOnUiThread([=,this] {
                auto m = GetMainWindow();
                m->UpdateConnectionList(toUpdate, toAdd);
            });
        } else
        {
            if (sort == ByDownload)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                {
                    if (a.download == b.download) return asc ? a.id > b.id : a.id < b.id;
                    return asc ? a.download < b.download : a.download > b.download;
                });
            }
            if (sort == ByUpload)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                {
                   if (a.upload == b.upload) return asc ? a.id > b.id : a.id < b.id;
                   return asc ? a.upload < b.upload : a.upload > b.upload;
                });
            }
            if (sort == ByProcess)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                {
                    if (a.process == b.process) return asc ? a.id > b.id : a.id < b.id;
                    return asc ? a.process > b.process : a.process < b.process;
                });
            }
            if (sort == ByOutbound)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                    {
                        if (a.outbound == b.outbound) return asc ? a.id > b.id : a.id < b.id;
                        return asc ? a.outbound > b.outbound : a.outbound < b.outbound;
                    });
            }
            if (sort == ByProtocol)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                    {
                        if (a.protocol == b.protocol) return asc ? a.id > b.id : a.id < b.id;
                        return asc ? a.protocol > b.protocol : a.protocol < b.protocol;
                    });
            }
            runOnUiThread([=,this] {
                auto m = GetMainWindow();
                m->UpdateConnectionListWithRecreate(sorted);
            });
        }
    }

    void ConnectionLister::stopLoop()
    {
        stop = true;
    }

    void ConnectionLister::setSort(const ConnectionSort newSort)
    {
        if (newSort == ByTraffic)
        {
            if (sort == ByDownload && asc)
            {
                sort = ByUpload;
                asc = false;
                return;
            }
            if (sort == ByUpload && asc)
            {
                sort = ByDownload;
                asc = false;
                return;
            }
            if (sort == ByDownload)
            {
                asc = true;
                return;
            }
            if (sort == ByUpload)
            {
                asc = true;
                return;
            }
            sort = ByDownload;
            asc = false;
            return;
        }
        if (sort == newSort) asc = !asc;
        else
        {
            sort = newSort;
            asc = false;
        }
    }

}
