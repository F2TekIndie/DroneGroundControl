
#include "grpcclient.h"



//
gRPCClient::gRPCClient()
{
    _channel = grpc::CreateChannel("127.0.0.1:5600", grpc::InsecureChannelCredentials());
    //
    _deviceStub     = dronegroundcontrol::DeviceFunctions::NewStub(     _channel);
    _attributeStub  = dronegroundcontrol::AttributeFunctions::NewStub(  _channel);
    _timeseriesStub = dronegroundcontrol::TimeseriesFunctions::NewStub( _channel);
}

//
bool gRPCClient::createDrone( uint32_t clientId ) const {
    assert( _channel );
    assert( _deviceStub );
    //
    dronegroundcontrol::Client cl;
    cl.set_id( clientId );
    //
    dronegroundcontrol::Device device;
    grpc::ClientContext ctx;
    //
    auto status = _deviceStub->CreateDevice( &ctx, cl, &device );
    return status.ok();
}
bool gRPCClient::deleteDrone( uint32_t droneId ) const {
    assert( _channel );
    assert( _deviceStub );
    //
    dronegroundcontrol::Device device;
    device.set_id( droneId );
    //
    dronegroundcontrol::DeviceStatusReply reply;
    grpc::ClientContext ctx;
    //
    auto status = _deviceStub->DeletDevice( &ctx, device, &reply );
    return status.ok();
}
bool gRPCClient::getDrones( uint32_t clientId ) const {
    assert( _channel );
    assert( _deviceStub );
    //
    dronegroundcontrol::Client cl;
    cl.set_id( clientId );
    dronegroundcontrol::Devices devices;
    grpc::ClientContext ctx;
    //
    auto status = _deviceStub->ListDevices( &ctx, cl, &devices );
    return status.ok();
}
//
bool gRPCClient::updateAttributes( uint32_t deviceId ) const {
    assert( _channel );
    assert( _attributeStub );
    //
    dronegroundcontrol::AttributeRequest cl;
    ;
    dronegroundcontrol::AttributeStatusReply reply;
    grpc::ClientContext ctx;
    //
    auto status = _attributeStub->UpdateAttribute( &ctx, cl, &reply );
    return status.ok();
}
bool gRPCClient::getAttribute( uint32_t deviceId ) const {
    assert( _channel );
    assert( _attributeStub );
    //
    dronegroundcontrol::Device cl;
    cl.set_id( deviceId );
    dronegroundcontrol::Attribute reply;
    grpc::ClientContext ctx;
    //
    auto status = _attributeStub->GetAttribute( &ctx, cl, &reply );
    return status.ok();
}
bool gRPCClient::getAttributes( uint32_t deviceId ) const {
    assert( _channel );
    assert( _attributeStub );
    //
    dronegroundcontrol::Device cl;
    cl.set_id( deviceId );
    dronegroundcontrol::Attributes reply;
    grpc::ClientContext ctx;
    //
    auto status = _attributeStub->GetAttributes( &ctx, cl, &reply );
    return status.ok();
}
bool gRPCClient::updatePosition( uint32_t deviceId ) const {
    assert( _channel );
    assert( _attributeStub );
    //
    dronegroundcontrol::PositionRequest cl;
    ;
    dronegroundcontrol::AttributeStatusReply reply;
    grpc::ClientContext ctx;
    //
    auto status = _attributeStub->UpdatePosition( &ctx, cl, &reply );
    return status.ok();
}
bool gRPCClient::getPosition( uint32_t deviceId ) const {
    assert( _channel );
    assert( _attributeStub );
    //
    dronegroundcontrol::Device cl;
    cl.set_id( deviceId );
    dronegroundcontrol::Position reply;
    grpc::ClientContext ctx;
    //
    auto status = _attributeStub->GetPosition( &ctx, cl, &reply );
    return status.ok();
}
//
bool gRPCClient::addTimeseries( uint32_t droneId ) const {
    assert( _channel );
    assert( _timeseriesStub );
    //
    dronegroundcontrol::TimeseriesRequest device;
    ;
    grpc::ClientContext ctx;
    dronegroundcontrol::TimeseriesStatusReply timeSeries;
    //
    auto status = _timeseriesStub->AddTimeseries( &ctx, device, &timeSeries );
    return status.ok();
}
bool gRPCClient::getTimeseries( uint32_t droneId ) const {
    assert( _channel );
    assert( _timeseriesStub );
    //
    dronegroundcontrol::Device device;
    device.set_id( droneId );
    grpc::ClientContext ctx;
    dronegroundcontrol::TimeSerie timeSeries;
    //
    auto status = _timeseriesStub->GetTimeseries( &ctx, device, &timeSeries );
    return status.ok();
}
bool gRPCClient::getTimeseriesRange( uint32_t droneId ) const {
    assert( _channel );
    assert( _timeseriesStub );
    //
    dronegroundcontrol::Device device;
    device.set_id( droneId );
    grpc::ClientContext ctx;
    dronegroundcontrol::TimeSeries timeSeries;
    //
    auto status = _timeseriesStub->GetTimeseriesRange( &ctx, device, &timeSeries );
    return status.ok();
}
