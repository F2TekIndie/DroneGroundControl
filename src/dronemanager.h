#ifndef DRONEMANAGER_H
#define DRONEMANAGER_H


#include <QObject>
#include <QQmlEngine>



//
class DroneManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    DroneManager( QObject* parent = nullptr );

public slots:

signals:

private:

};

#endif // DRONEMANAGER_H
