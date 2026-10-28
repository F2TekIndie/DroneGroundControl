#ifndef DRONEMANAGER_H
#define DRONEMANAGER_H


#include <QObject>
#include <QQmlEngine>
#include "drone.h"
#include "grpcclient.h"



//
class DroneManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    DroneManager( QObject* parent = nullptr );
    ~DroneManager() = default;

public slots:

signals:

private:
    gRPCClient  _client = {};
    std::map<quint32, Drone> _drones = {};
};

#endif // DRONEMANAGER_H
