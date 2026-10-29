#include "grpcclient.h"
#include "devices.grpc.pb.h"
#include "attributes.grpc.pb.h"
#include "timeseries.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <QUuid>
#include <QSet>
#include <charconv>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>



//
namespace proto = dronegroundcontrol;

//
namespace{
    auto Invalid( QString message ){
        return std::unexpected( RpcError{ RpcErrorCode::InvalidRequest, std::move( message ) } );
    }
    auto BadReply( QString message ){
        return std::unexpected( RpcError{ RpcErrorCode::InvalidResponse, std::move( message ) } );
    }
    bool ValidId( QString const& id ){
        const QUuid uuid( id );
        return !uuid.isNull() && uuid.toString( QUuid::WithoutBraces ) == id;
    }
    bool ValidRef( DroneRef const& drone ){
        return ValidId( drone.id ) && drone.clientId != 0;
    }
    bool ValidPosition( DronePosition const& position ){
        return std::isfinite( position.latitude ) && std::abs( position.latitude ) <= 90
               && std::isfinite( position.longitude ) && std::abs( position.longitude ) <= 180;
    }
    bool IsAttribute( QString const& name ){
        return name == "online" || name == "state" || name == "position";
    }
    bool IsSeries( QString const& name ){
        return name == "latitude" || name == "longitude";
    }
    void FillDevice( DroneRef const& drone, proto::Device* output ){
        output->set_id( drone.id.toStdString() );
        output->mutable_client_id()->set_id( drone.clientId );
    }
    void FillPosition( DronePosition const& position, proto::Position* output ){
        output->set_lat( QString::number( position.latitude, 'g', 17 ).toStdString() );
        output->set_long_( QString::number( position.longitude, 'g', 17 ).toStdString() );
    }
    RpcResult<DronePosition> ReadPosition( proto::Position const& input ){
        bool          latOk{}, lonOk{};
        DronePosition position{ QString::fromStdString( input.lat() ).toDouble( &latOk ),
                                QString::fromStdString( input.long_() ).toDouble( &lonOk ) };
        if ( !latOk || !lonOk || !ValidPosition( position ) )
            return BadReply( "Invalid position returned by server" );
        return position;
    }
    RpcResult<Drone> ReadDevice( proto::Device const& input, quint32 clientId ){
        Drone drone;
        drone._droneId  = QString::fromStdString( input.id() );
        drone._clientId = input.client_id().id();
        drone._name     = QString::fromStdString( input.name() );
        drone._type     = QString::fromStdString( input.type() );
        if ( !ValidId( drone._droneId ) || drone._clientId != clientId || drone._name.trimmed().isEmpty()
             || drone._name.contains( QChar::Null ) )
            return BadReply( "Invalid device returned by server" );
        return drone;
    }
    RpcResult<DroneAttributeChanges> ReadAttribute( proto::Attribute const& input ){
        DroneAttributeChanges changes;
        const auto name = QString::fromStdString( input.name() );
        if ( name == "online" && input.has_bvalue() )
            changes.online = input.bvalue();
        else if ( name == "state" && input.has_ivalue() )
            changes.state = input.ivalue();
        else if ( name == "position" && input.has_pos() ){
            auto position = ReadPosition( input.pos() );
            if ( !position )
                return std::unexpected( position.error() );
            changes.position = *position;
        }
        else
            return BadReply( "Unsupported attribute or wrong value type returned by server" );
        return changes;
    }
    RpcResult<DroneSample> ReadSample( proto::TimeSerie const& input ){
        DroneSample sample;
        sample.name        = QString::fromStdString( input.name() );
        auto const& stamp  = input.timestamp();
        const auto  parsed = std::from_chars( stamp.data(), stamp.data() + stamp.size(), sample.timestampMs );
        bool        coordinateOk{};
        sample.coordinate  = QString::fromStdString( input.svalue() ).toDouble( &coordinateOk );
        const double limit = sample.name == "latitude" ? 90 : 180;
        if ( !IsSeries( sample.name ) || !input.has_svalue() || !coordinateOk || !std::isfinite( sample.coordinate )
             || std::abs( sample.coordinate ) > limit || parsed.ec != std::errc{}
             || parsed.ptr != stamp.data() + stamp.size() )
            return BadReply( "Invalid historical sample returned by server" );
        return sample;
    }
    RpcError TransportError( grpc::Status const& status ){
        RpcErrorCode code;
        switch ( status.error_code() ){
        case grpc::StatusCode::CANCELLED:
            code = RpcErrorCode::Cancelled;
            break;
        case grpc::StatusCode::DEADLINE_EXCEEDED:
            code = RpcErrorCode::Timeout;
            break;
        case grpc::StatusCode::UNAVAILABLE:
            code = RpcErrorCode::Unavailable;
            break;
        case grpc::StatusCode::NOT_FOUND:
            code = RpcErrorCode::NotFound;
            break;
        case grpc::StatusCode::INVALID_ARGUMENT:
            code = RpcErrorCode::InvalidRequest;
            break;
        case grpc::StatusCode::RESOURCE_EXHAUSTED:
            code = RpcErrorCode::ResourceExhausted;
            break;
        default:
            code = RpcErrorCode::Internal;
            break;
        }
        return { code, QString::fromStdString( status.error_message() ) };
    }
    template <class Reply> RpcResult<void> WriteResult( RpcResult<Reply> const& reply ){
        if ( !reply )
            return std::unexpected( reply.error() );
        if ( !reply->success() )
            return std::unexpected( RpcError{ RpcErrorCode::Rejected, "Server rejected the update" } );
        return {};
    }
}

//
struct gRPCClient::Impl{
    explicit Impl( QString const& endpoint, std::chrono::milliseconds deadline )
        :channel( grpc::CreateChannel( endpoint.toStdString(), grpc::InsecureChannelCredentials() ) )
        ,devices( proto::DeviceFunctions::NewStub( channel ) )
        ,attributes( proto::AttributeFunctions::NewStub( channel ) )
        ,timeseries( proto::TimeseriesFunctions::NewStub( channel ) )
        ,timeout( deadline ){
    }

    template <class Reply, class Function> RpcResult<Reply> Call( Function&& function ){
        grpc::ClientContext context;
        context.set_deadline( std::chrono::system_clock::now() + timeout );
        {
            std::lock_guard lock( mutex );
            if ( cancelled )
                return std::unexpected( RpcError{ RpcErrorCode::Cancelled, "Request cancelled" } );
            active.insert( &context );
        }
        // The context remains alive until it is removed under the same lock used by cancelAll().
        struct Registration{
            Impl&                owner;
            grpc::ClientContext* context;
            ~Registration(){
                std::lock_guard lock( owner.mutex );
                owner.active.erase( context );
            }
        } registration{ *this, &context };
        Reply      reply;
        const auto status = function( context, reply );
        if ( !status.ok() )
            return std::unexpected( TransportError( status ) );
        return reply;
    }

    std::shared_ptr<grpc::Channel>                    channel;
    std::unique_ptr<proto::DeviceFunctions::Stub>     devices;
    std::unique_ptr<proto::AttributeFunctions::Stub>  attributes;
    std::unique_ptr<proto::TimeseriesFunctions::Stub> timeseries;
    std::chrono::milliseconds                         timeout;
    std::mutex                                        mutex;
    std::set<grpc::ClientContext*>                    active;
    bool                                              cancelled{};
};

//
gRPCClient::gRPCClient( QString endpoint, std::chrono::milliseconds timeout ){
    if ( endpoint.trimmed().isEmpty() || timeout.count() <= 0 )
        throw std::invalid_argument( "Endpoint and a positive timeout are required" );
    _impl = std::make_unique<Impl>( endpoint, timeout );
}

//
gRPCClient::~gRPCClient() = default;

//
void gRPCClient::cancelAll(){
    std::lock_guard lock( _impl->mutex );
    _impl->cancelled = true;
    for ( auto* context: _impl->active )
        context->TryCancel();
}

//
RpcResult<Drone> gRPCClient::createDrone( quint32 clientId ) const{
    if ( !clientId )
        return Invalid( "Client ID must be nonzero" );
    proto::Client request;
    request.set_id( clientId );
    auto reply = _impl->Call<proto::Device>( [&]( auto& context, auto& output ){
        return _impl->devices->CreateDevice( &context, request, &output );
    } );
    if ( !reply )
        return std::unexpected( reply.error() );
    return ReadDevice( *reply, clientId );
}

//
RpcResult<void> gRPCClient::deleteDrone( DroneRef const& drone ) const{
    if ( !ValidRef( drone ) )
        return Invalid( "A UUID and nonzero client ID are required" );
    proto::Device request;
    FillDevice( drone, &request );
    return WriteResult( _impl->Call<proto::DeviceStatusReply>( [&]( auto& context, auto& output ){
        return _impl->devices->DeletDevice( &context, request, &output );
    } ) );
}

//
RpcResult<QVector<Drone>> gRPCClient::getDrones( quint32 clientId ) const{
    if ( !clientId )
        return Invalid( "Client ID must be nonzero" );
    proto::Client request;
    request.set_id( clientId );
    auto reply = _impl->Call<proto::Devices>( [&]( auto& context, auto& output ){
        return _impl->devices->ListDevices( &context, request, &output );
    } );
    if ( !reply )
        return std::unexpected( reply.error() );
    QVector<Drone> drones;
    QSet<QString>  ids;
    for ( auto const& value: reply->devicelist() ){
        auto drone = ReadDevice( value, clientId );
        if ( !drone )
            return std::unexpected( drone.error() );
        if ( ids.contains( drone->_droneId ) )
            return BadReply( "Duplicate device UUID returned by server" );
        ids.insert( drone->_droneId );
        drones.push_back( std::move( *drone ) );
    }
    return drones;
}

//
RpcResult<void> gRPCClient::updateAttributes( DroneRef const& drone, DroneAttributeChanges const& values ) const{
    if ( !ValidRef( drone ) || ( !values.state && !values.online && !values.position ) )
        return Invalid( "Device identity and a nonempty attribute update are required" );
    if ( values.position && !ValidPosition( *values.position ) )
        return Invalid( "Invalid position coordinate" );
    proto::AttributeRequest request;
    FillDevice( drone, request.mutable_device() );
    if ( values.state ){
        auto* entry = request.mutable_attributes()->add_timeseries();
        entry->set_name( "state" );
        entry->set_ivalue( *values.state );
    }
    if ( values.online ){
        auto* entry = request.mutable_attributes()->add_timeseries();
        entry->set_name( "online" );
        entry->set_bvalue( *values.online );
    }
    if ( values.position ){
        auto* entry = request.mutable_attributes()->add_timeseries();
        entry->set_name( "position" );
        FillPosition( *values.position, entry->mutable_pos() );
    }
    return WriteResult( _impl->Call<proto::AttributeStatusReply>( [&]( auto& context, auto& output ){
        return _impl->attributes->UpdateAttribute( &context, request, &output );
    } ) );
}

//
RpcResult<DroneAttributeChanges> gRPCClient::getAttribute( DroneRef const& drone, QString const& name ) const{
    if ( !ValidRef( drone ) || !IsAttribute( name ) )
        return Invalid( "Device identity and a supported attribute name are required" );
    proto::AttributeQuery request;
    FillDevice( drone, request.mutable_device() );
    request.set_name( name.toStdString() );
    auto reply = _impl->Call<proto::Attribute>( [&]( auto& context, auto& output ){
        return _impl->attributes->GetAttribute( &context, request, &output );
    } );
    if ( !reply )
        return std::unexpected( reply.error() );
    if ( QString::fromStdString( reply->name() ) != name )
        return BadReply( "Server returned a different attribute" );
    return ReadAttribute( *reply );
}

//
RpcResult<DroneAttributes> gRPCClient::getAttributes( DroneRef const& drone ) const{
    if ( !ValidRef( drone ) )
        return Invalid( "A UUID and nonzero client ID are required" );
    proto::Device request;
    FillDevice( drone, &request );
    auto reply = _impl->Call<proto::Attributes>( [&]( auto& context, auto& output ){
        return _impl->attributes->GetAttributes( &context, request, &output );
    } );
    if ( !reply )
        return std::unexpected( reply.error() );
    DroneAttributes attributes;
    QSet<QString>   names;
    for ( auto const& value: reply->timeseries() ){
        auto attribute = ReadAttribute( value );
        if ( !attribute )
            return std::unexpected( attribute.error() );
        const auto name = QString::fromStdString( value.name() );
        if ( names.contains( name ) )
            return BadReply( "Duplicate attribute returned by server" );
        names.insert( name );
        if ( attribute->state )
            attributes._state = *attribute->state;
        if ( attribute->online )
            attributes._online = *attribute->online;
        if ( attribute->position ){
            attributes._pos = *attribute->position;
            attributes._hasPosition = true;
        }
    }
    if ( !names.contains( "state" ) || !names.contains( "online" ) )
        return BadReply( "Incomplete attribute snapshot returned by server" );
    return attributes;
}

//
RpcResult<void> gRPCClient::updatePosition( DroneRef const& drone, DronePosition const& position ) const{
    if ( !ValidRef( drone ) || !ValidPosition( position ) )
        return Invalid( "Device identity and valid coordinates are required" );
    proto::PositionRequest request;
    FillDevice( drone, request.mutable_device() );
    FillPosition( position, request.mutable_pos() );
    return WriteResult( _impl->Call<proto::AttributeStatusReply>( [&]( auto& context, auto& output ){
        return _impl->attributes->UpdatePosition( &context, request, &output );
    } ) );
}

//
RpcResult<DronePosition> gRPCClient::getPosition( DroneRef const& drone ) const{
    if ( !ValidRef( drone ) )
        return Invalid( "A UUID and nonzero client ID are required" );
    proto::Device request;
    FillDevice( drone, &request );
    auto reply = _impl->Call<proto::Position>( [&]( auto& context, auto& output ){
        return _impl->attributes->GetPosition( &context, request, &output );
    } );
    if ( !reply )
        return std::unexpected( reply.error() );
    return ReadPosition( *reply );
}

//
RpcResult<void> gRPCClient::addTimeseries( DroneRef const& drone, DroneSamples const& samples ) const{
    if ( !ValidRef( drone ) || samples.isEmpty() )
        return Invalid( "Device identity and a nonempty sample batch are required" );
    std::map<qint64, unsigned> pairs;
    proto::TimeseriesRequest   request;
    FillDevice( drone, request.mutable_device() );
    for ( auto const& sample: samples ){
        if ( !IsSeries( sample.name ) || !std::isfinite( sample.coordinate )
             || std::abs( sample.coordinate ) > ( sample.name == "latitude" ? 90 : 180 ) )
            return Invalid( "Unsupported series or invalid coordinate" );
        const auto bit = sample.name == "latitude" ? 1u : 2u;
        if ( pairs[sample.timestampMs] & bit )
            return Invalid( "Duplicate coordinate at the same timestamp" );
        pairs[sample.timestampMs] |= bit;
        auto* output = request.mutable_timeseries()->add_timeseries();
        output->set_name( sample.name.toStdString() );
        output->set_timestamp( std::to_string( sample.timestampMs ) );
        output->set_svalue( QString::number( sample.coordinate, 'g', 17 ).toStdString() );
    }
    for ( auto const& [stamp, mask]: pairs ){
        if ( mask != 3 )
            return Invalid( "Supply both coordinates for each timestamp" );
    }
    return WriteResult( _impl->Call<proto::TimeseriesStatusReply>( [&]( auto& context, auto& output ){
        return _impl->timeseries->AddTimeseries( &context, request, &output );
    } ) );
}

//
RpcResult<DroneSample> gRPCClient::getTimeseries( DroneRef const& drone, QString const& name,
                                                  std::optional<qint64> stamp ) const{
    if ( !ValidRef( drone ) || !IsSeries( name ) )
        return Invalid( "Device identity and a supported series name are required" );
    proto::TimeseriesQuery request;
    FillDevice( drone, request.mutable_device() );
    request.set_name( name.toStdString() );
    if ( stamp )
        request.set_timestamp_ms( *stamp );
    auto reply = _impl->Call<proto::TimeSerie>( [&]( auto& context, auto& output ){
        return _impl->timeseries->GetTimeseries( &context, request, &output );
    } );
    if ( !reply )
        return std::unexpected( reply.error() );
    auto sample = ReadSample( *reply );
    if ( sample && ( sample->name != name || ( stamp && sample->timestampMs != *stamp ) ) )
        return BadReply( "Server returned a different historical sample" );
    return sample;
}

//
RpcResult<DroneSamples> gRPCClient::getTimeseriesRange( DroneRef const& drone, QString const& name,
                                                        std::optional<qint64> start, std::optional<qint64> end ) const{
    if ( !ValidRef( drone ) || ( !name.isEmpty() && !IsSeries( name ) ) || ( start && end && *start > *end ) )
        return Invalid( "Invalid device identity, series name, or time bounds" );
    proto::TimeseriesRangeQuery request;
    FillDevice( drone, request.mutable_device() );
    request.set_name( name.toStdString() );
    if ( start )
        request.set_start_timestamp_ms( *start );
    if ( end )
        request.set_end_timestamp_ms( *end );
    auto reply = _impl->Call<proto::TimeSeries>( [&]( auto& context, auto& output ){
        return _impl->timeseries->GetTimeseriesRange( &context, request, &output );
    } );
    if ( !reply )
        return std::unexpected( reply.error() );
    DroneSamples samples;
    for ( auto const& input: reply->timeseries() ){
        auto sample = ReadSample( input );
        if ( !sample )
            return std::unexpected( sample.error() );
        if ( ( !name.isEmpty() && sample->name != name ) || ( start && sample->timestampMs < *start )
             || ( end && sample->timestampMs > *end ) )
            return BadReply( "Server returned a sample outside the requested range" );
        samples.push_back( std::move( *sample ) );
    }
    return samples;
}
