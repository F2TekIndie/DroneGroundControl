
#ifndef GRPCCLIENT_H
#define GRPCCLIENT_H

#include <cstdint>
#include <memory>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/support/status.h>
#include "devices.grpc.pb.h"
#include "attributes.grpc.pb.h"
#include "timeseries.grpc.pb.h"


//
struct ioClient;

//
class gRPCClient
{
public:
    gRPCClient();
    ~gRPCClient() = default;

    //
    bool createDrone( uint32_t clientId ) const;
    bool deleteDrone( uint32_t droneId ) const;
    bool getDrones( uint32_t clientId ) const;
    //
    bool updateAttributes( uint32_t deviceId ) const;
    bool getAttribute( uint32_t deviceId ) const;
    bool getAttributes( uint32_t deviceId ) const;
    bool updatePosition( uint32_t deviceId ) const;
    bool getPosition( uint32_t devicdeviceIdeId ) const;
    //
    bool addTimeseries( uint32_t droneId ) const;
    bool getTimeseries( uint32_t droneId ) const;
    bool getTimeseriesRange( uint32_t droneId ) const;

private:
    std::shared_ptr<grpc::Channel>  _channel{nullptr};
    std::unique_ptr<dronegroundcontrol::DeviceFunctions::Stub>      _deviceStub{nullptr};
    std::unique_ptr<dronegroundcontrol::AttributeFunctions::Stub>   _attributeStub{nullptr};
    std::unique_ptr<dronegroundcontrol::TimeseriesFunctions::Stub>  _timeseriesStub{nullptr};
};

#endif // GRPCCLIENT_H
