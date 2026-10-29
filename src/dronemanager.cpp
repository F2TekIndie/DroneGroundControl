#include "dronemanager.h"
#include <QMetaType>
#include <QSet>
#include <QThread>
#include <cmath>
#include <limits>



//
namespace{
    QString ErrorName( RpcErrorCode code ){
        switch ( code ){
        case RpcErrorCode::Cancelled:
            return "cancelled";
        case RpcErrorCode::Timeout:
            return "timeout";
        case RpcErrorCode::Unavailable:
            return "unavailable";
        case RpcErrorCode::NotFound:
            return "notFound";
        case RpcErrorCode::InvalidRequest:
            return "invalidRequest";
        case RpcErrorCode::Rejected:
            return "rejected";
        case RpcErrorCode::InvalidResponse:
            return "invalidResponse";
        case RpcErrorCode::ResourceExhausted:
            return "resourceExhausted";
        case RpcErrorCode::Internal:
            return "internal";
        }
        return "internal";
    }
    QVariantMap PositionValue( DronePosition const& position ){
        return { { "latitude", position.latitude }, { "longitude", position.longitude } };
    }
    QVariantMap AttributesValue( DroneAttributeChanges const& changes ){
        QVariantMap result;
        if ( changes.state )
            result.insert( "state", *changes.state );
        if ( changes.online )
            result.insert( "online", *changes.online );
        if ( changes.position )
            result.insert( "position", PositionValue( *changes.position ) );
        return result;
    }
    QVariantMap AttributesValue( DroneAttributes const& attributes ){
        QVariantMap result{ { "state", attributes._state }, { "online", attributes._online } };
        if ( attributes._hasPosition )
            result.insert( "position", PositionValue( attributes._pos ) );
        return result;
    }
    QVariantMap DeviceValue( Drone const& drone ){
        return { { "id", drone._droneId },
                 { "clientId", drone._clientId },
                 { "name", drone._name },
                 { "type", drone._type } };
    }
    QVariantMap SampleValue( DroneSample const& sample ){
        return { { "name", sample.name },
                 { "timestampMs", QString::number( sample.timestampMs ) },
                 { "coordinate", sample.coordinate } };
    }
    // Strings preserve the entire int64 domain; JS numbers must be exact integers.
    bool Timestamp( QVariant const& value, std::optional<qint64>& stamp ){
        if ( !value.isValid() || value.isNull() ){
            stamp.reset();
            return true;
        }
        bool ok{};
        if ( value.metaType().id() == QMetaType::QString ){
            auto text   = value.toString();
            auto number = text.toLongLong( &ok );
            if ( !ok || QString::number( number ) != text )
                return false;
            stamp = number;
            return true;
        }
        if ( value.metaType().id() == QMetaType::Double || value.metaType().id() == QMetaType::Float ){
            const double number = value.toDouble();
            if ( !std::isfinite( number ) || std::trunc( number ) != number || std::abs( number ) > 9007199254740991.0 )
                return false;
            stamp = static_cast<qint64>( number );
            return true;
        }
        const auto type = value.metaType().id();
        if ( type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong
             && type != QMetaType::ULongLong )
            return false;
        if ( type == QMetaType::ULongLong && value.toULongLong() > quint64( std::numeric_limits<qint64>::max() ) )
            return false;
        auto number = value.toLongLong( &ok );
        if ( ok )
            stamp = number;
        return ok;
    }
    bool Number( QVariant const& value, double& output ){
        const auto type = value.metaType().id();
        if ( type != QMetaType::Double && type != QMetaType::Float && type != QMetaType::Int && type != QMetaType::UInt
             && type != QMetaType::LongLong && type != QMetaType::ULongLong )
            return false;
        output = value.toDouble();
        return std::isfinite( output );
    }
    RpcResult<DroneAttributeChanges> ReadAttributes( QVariantMap const& values ){
        DroneAttributeChanges result;
        for ( auto it = values.begin(); it != values.end(); ++it ){
            if ( it.key() == "online" && it.value().metaType().id() == QMetaType::Bool )
                result.online = it.value().toBool();
            else if ( it.key() == "state" ){
                double number{};
                if ( !Number( it.value(), number ) || number < 0 || number > std::numeric_limits<quint32>::max()
                     || std::trunc( number ) != number )
                    return std::unexpected( RpcError{ RpcErrorCode::InvalidRequest, "State must be a uint32 value" } );
                result.state = static_cast<quint32>( number );
            }
            else if ( it.key() == "position" && it.value().metaType().id() == QMetaType::QVariantMap ){
                auto          map = it.value().toMap();
                DronePosition position;
                if ( map.size() != 2 || !Number( map.value( "latitude" ), position.latitude )
                     || !Number( map.value( "longitude" ), position.longitude ) )
                    return std::unexpected(
                        RpcError{ RpcErrorCode::InvalidRequest, "Position needs latitude and longitude" } );
                result.position = position;
            }
            else
                return std::unexpected(
                    RpcError{ RpcErrorCode::InvalidRequest, "Unsupported attribute or value type" } );
        }
        return result;
    }
}

//
DroneManager::DroneManager( QObject* parent )
    :QAbstractListModel( parent )
    ,_client( std::make_shared<gRPCClient>() ){
    _pool.setMaxThreadCount( 4 );
}

//
DroneManager::~DroneManager(){
    shutdown();
}

//
int DroneManager::rowCount( QModelIndex const& parent ) const{
    return parent.isValid() ? 0 : _drones.size();
}

//
QVariant DroneManager::data( QModelIndex const& index, int role ) const{
    if ( !index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= _drones.size() )
        return {};
    auto const& drone = _drones[index.row()];
    const auto attributes = _attributes.value( drone._droneId );
    switch ( role ){
    case IdRole:
        return drone._droneId;
    case ClientIdRole:
        return drone._clientId;
    case NameRole:
        return drone._name;
    case TypeRole:
        return drone._type;
    case StateRole:
        return attributes._state;
    case OnlineRole:
        return attributes._online;
    case LatitudeRole:
        return attributes._hasPosition ? QVariant( attributes._pos.latitude ) : QVariant{};
    case LongitudeRole:
        return attributes._hasPosition ? QVariant( attributes._pos.longitude ) : QVariant{};
    case HasPositionRole:
        return attributes._hasPosition;
    default:
        return {};
    }
}

//
QHash<int, QByteArray> DroneManager::roleNames() const{
    return { { IdRole, "droneId" },        { ClientIdRole, "clientId" },   { NameRole, "name" },
             { TypeRole, "type" },         { StateRole, "state" },         { OnlineRole, "online" },
             { LatitudeRole, "latitude" }, { LongitudeRole, "longitude" }, { HasPositionRole, "hasPosition" } };
}

//
void DroneManager::setClientId( quint32 id ){
    if ( !id || id == _clientId || _shutdown )
        return;
    _clientId = id;
    resetSession( true );
    emit clientIdChanged();
}

//
void DroneManager::setEndpoint( QString const& endpoint ){
    const auto address = endpoint.trimmed();
    if ( address.isEmpty() || address == _endpoint || _shutdown )
        return;
    _endpoint = address;
    resetSession( true );
    emit endpointChanged();
}

//
void DroneManager::resetSession( bool clearModel ){
    _client->cancelAll();
    _client = std::make_shared<gRPCClient>( _endpoint );
    // Cancelled jobs still deliver completion, keeping _pending accurate.
    // Leave queued jobs in the pool; the cancelled client rejects them before RPC.
    if ( clearModel ){
        beginResetModel();
        _drones.clear();
        _attributes.clear();
        endResetModel();
    }
}

//
void DroneManager::cancelRequests(){
    if ( !_shutdown )
        resetSession( false );
}

//
void DroneManager::shutdown(){
    if ( _shutdown )
        return;
    _shutdown = true;
    _client->cancelAll();
    _pool.clear();
    _pool.waitForDone(); // Workers only post queued callbacks; they never wait for the GUI.
    const bool wasBusy = busy();
    _pending           = 0;
    if ( wasBusy )
        emit busyChanged();
}

//
void DroneManager::reportError( quint64 request, QString const& operation, QString const& id, RpcError const& error ){
    const QPointer<DroneManager> guard( this );
    if ( _lastError != error.message ){
        _lastError = error.message;
        emit lastErrorChanged();
    }
    if ( !guard || guard->_shutdown )
        return;
    emit operationFailed( request, operation, id, ErrorName( error.code ), error.message );
}

//
quint64 DroneManager::reject( QString const& operation, QString const& id, QString message ){
    if ( _shutdown )
        return 0;
    const auto request = ++_nextRequestId;
    reportError( request, operation, id, { RpcErrorCode::InvalidRequest, std::move( message ) } );
    return request;
}

//
int DroneManager::indexOf( QString const& id ) const{
    for ( int i = 0; i < _drones.size(); ++i )
        if ( _drones[i]._droneId == id )
            return i;
    return -1;
}

//
void DroneManager::replaceDevices( QVector<Drone> devices ){
    QSet<QString> ids;
    bool sameOrder = devices.size() == _drones.size();
    for ( int i = 0; i < devices.size(); ++i ){
        ids.insert( devices[i]._droneId );
        if ( sameOrder && devices[i]._droneId != _drones[i]._droneId )
            sameOrder = false;
    }
    if ( !sameOrder )
        beginResetModel();
    _drones = std::move( devices );
    // Keep attributes for retained identities and discard removed drones.
    for ( auto it = _attributes.begin(); it != _attributes.end(); ){
        if ( !ids.contains( it.key() ) )
            it = _attributes.erase( it );
        else
            ++it;
    }
    if ( !sameOrder )
        endResetModel();
    else if ( !_drones.isEmpty() )
        emit dataChanged( index( 0 ), index( _drones.size() - 1 ) );
}

//
void DroneManager::upsert( Drone const& drone ){
    auto row = indexOf( drone._droneId );
    if ( row < 0 ){
        row = _drones.size();
        beginInsertRows( {}, row, row );
        _drones.push_back( drone );
        endInsertRows();
    }
    else{
        _drones[row] = drone;
        emit dataChanged( index( row ), index( row ) );
    }
}

//
void DroneManager::applyAttributes( QString const& id, DroneAttributes const& attributes ){
    Q_ASSERT( QThread::currentThread() == thread() );
    const auto row = indexOf( id );
    if ( row < 0 )
        return;
    _attributes.insert( id, attributes );
    emit dataChanged( index( row ), index( row ), { StateRole, OnlineRole, LatitudeRole, LongitudeRole, HasPositionRole } );
}

//
void DroneManager::applyChanges( QString const& id, DroneAttributeChanges const& changes ){
    if ( indexOf( id ) < 0 )
        return;
    auto attributes = _attributes.value( id );
    if ( changes.state )
        attributes._state = *changes.state;
    if ( changes.online )
        attributes._online = *changes.online;
    if ( changes.position ){
        attributes._pos = *changes.position;
        attributes._hasPosition = true;
    }
    applyAttributes( id, attributes );
}

//
quint64 DroneManager::refreshDrones(){
    if ( _shutdown )
        return 0;
    const auto clientId = _clientId;
    return submit(
        "getDrones", {},
        [clientId]( auto& client ){
            return client.getDrones( clientId );
        },
        [this]( QVector<Drone> const& drones ) -> QVariant {
            QVariantList result;
            for ( auto const& drone: drones )
                result.push_back( DeviceValue( drone ) );
            replaceDevices( drones );
            for ( auto const& drone: drones ){
                fetchAttributes( drone._droneId );
            }
            return result;
        } );
}

//
quint64 DroneManager::createDrone(){
    const auto clientId = _clientId;
    return submit(
        "createDrone", {},
        [clientId]( auto& client ){
            return client.createDrone( clientId );
        },
        [this]( Drone const& drone ) -> QVariant {
            upsert( drone );
            fetchAttributes( drone._droneId );
            return DeviceValue( drone );
        } );
}

//
quint64 DroneManager::deleteDrone( QString const& id ){
    const DroneRef drone{ id, _clientId };
    return submit(
        "deleteDrone", id,
        [drone]( auto& client ){
            return client.deleteDrone( drone );
        },
        [this, id]() -> QVariant {
            const auto row = indexOf( id );
            if ( row >= 0 ){
                beginRemoveRows( {}, row, row );
                _drones.removeAt( row );
                _attributes.remove( id );
                endRemoveRows();
            }
            return true;
        } );
}

//
quint64 DroneManager::updateAttributes( QString const& id, QVariantMap const& values ){
    auto attributes = ReadAttributes( values );
    if ( !attributes )
        return reject( "updateAttributes", id, attributes.error().message );
    const DroneRef drone{ id, _clientId };
    return submit(
        "updateAttributes", id,
        [drone, attributes = std::move( *attributes )]( auto& client ){
            return client.updateAttributes( drone, attributes );
        },
        [this, id]() -> QVariant {
            if ( indexOf( id ) >= 0 )
                fetchAttributes( id );
            return true;
        } );
}

//
quint64 DroneManager::getAttribute( QString const& id, QString const& name ){
    const DroneRef drone{ id, _clientId };
    return submit(
        "getAttribute", id,
        [drone, name]( auto& client ){
            return client.getAttribute( drone, name );
        },
        [this, id]( DroneAttributeChanges const& attribute ) -> QVariant {
            applyChanges( id, attribute );
            return AttributesValue( attribute );
        } );
}

//
quint64 DroneManager::fetchAttributes( QString const& id ){
    const DroneRef drone{ id, _clientId };
    return submit(
        "getAttributes", id,
        [drone]( auto& client ){
            return client.getAttributes( drone );
        },
        [this, id]( DroneAttributes const& attributes ) -> QVariant {
            applyAttributes( id, attributes );
            return AttributesValue( attributes );
        } );
}

//
quint64 DroneManager::getAttributes( QString const& id ){
    return fetchAttributes( id );
}

//
quint64 DroneManager::updatePosition( QString const& id, double latitude, double longitude ){
    const DroneRef      drone{ id, _clientId };
    const DronePosition position{ latitude, longitude };
    return submit(
        "updatePosition", id,
        [drone, position]( auto& client ){
            return client.updatePosition( drone, position );
        },
        [this, id]() -> QVariant {
            if ( indexOf( id ) >= 0 )
                fetchAttributes( id );
            return true;
        } );
}

//
quint64 DroneManager::getPosition( QString const& id ){
    const DroneRef drone{ id, _clientId };
    return submit(
        "getPosition", id,
        [drone]( auto& client ){
            return client.getPosition( drone );
        },
        [this, id]( DronePosition const& position ) -> QVariant {
            applyChanges( id, { .position = position } );
            return PositionValue( position );
        } );
}

//
quint64 DroneManager::addTimeseries( QString const& id, QVariantList const& values ){
    DroneSamples samples;
    for ( auto const& value: values ){
        if ( value.metaType().id() != QMetaType::QVariantMap )
            return reject( "addTimeseries", id, "Each sample must be a map" );
        const auto            map = value.toMap();
        std::optional<qint64> stamp;
        double                coordinate{};
        if ( !Timestamp( map.value( "timestampMs" ), stamp ) || !stamp
             || !Number( map.value( "coordinate" ), coordinate )
             || map.value( "name" ).metaType().id() != QMetaType::QString )
            return reject( "addTimeseries", id, "Each sample needs name, integer timestampMs, and coordinate" );
        samples.push_back( { map.value( "name" ).toString(), *stamp, coordinate } );
    }
    const DroneRef drone{ id, _clientId };
    return submit(
        "addTimeseries", id,
        [drone, samples]( auto& client ){
            return client.addTimeseries( drone, samples );
        },
        [this, id]() -> QVariant {
            if ( indexOf( id ) >= 0 )
                fetchAttributes( id );
            return true;
        } );
}

//
quint64 DroneManager::getTimeseries( QString const& id, QString const& name, QVariant timestampMs ){
    std::optional<qint64> stamp;
    if ( !Timestamp( timestampMs, stamp ) )
        return reject( "getTimeseries", id, "Timestamp must be an integer or decimal int64 string" );
    const DroneRef drone{ id, _clientId };
    return submit(
        "getTimeseries", id,
        [drone, name, stamp]( auto& client ){
            return client.getTimeseries( drone, name, stamp );
        },
        []( DroneSample const& sample ) -> QVariant {
            return SampleValue( sample );
        } );
}

//
quint64 DroneManager::getTimeseriesRange( QString const& id, QString const& name, QVariant startMs, QVariant endMs ){
    std::optional<qint64> start, end;
    if ( !Timestamp( startMs, start ) || !Timestamp( endMs, end ) )
        return reject( "getTimeseriesRange", id, "Time bounds must be integers or decimal int64 strings" );
    const DroneRef drone{ id, _clientId };
    return submit(
        "getTimeseriesRange", id,
        [drone, name, start, end]( auto& client ){
            return client.getTimeseriesRange( drone, name, start, end );
        },
        []( DroneSamples const& samples ) -> QVariant {
            QVariantList result;
            for ( auto const& sample: samples )
                result.push_back( SampleValue( sample ) );
            return result;
        } );
}
