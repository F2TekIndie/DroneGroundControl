
#ifndef DRONE_H
#define DRONE_H

#include <QString>




//
struct DronePosition{
    double _lang;
    double _lat;
};

//
struct Drone{
    quint32 _droneId;
    quint32 _clientId;
    QString  _name;
    quint32 _state;
    bool    _online;
    DronePosition _pos;
};

#endif // DRONE_H
