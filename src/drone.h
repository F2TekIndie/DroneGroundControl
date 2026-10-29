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
struct DroneAttributes{
    quint32       _state{};
    bool          _online{};
    DronePosition _pos{};
    bool          _hasPosition{};
};
