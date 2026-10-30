#pragma once

#include <QString>



//
struct DronePosition{
    double latitude{};
    double longitude{};
};

//
struct Drone{
    QString _droneId;
    quint32 _clientId{};
    QString _name;
    QString _type;
};

//
enum DroneState{
    DS_INIT     = 0,
    DS_OFFLINE  = 1,
    DS_ONLINE   = 2,
    DS_GROUNDED = 3,
    DS_STATIONARY   = 4,
    DS_TRAVELING    = 5
};

//
struct DroneAttributes{
    quint32       _state{};
    bool          _online{};
    DronePosition _pos{};
    bool          _hasPosition{};
};
