#pragma once

#include "drone.h"
#include "grpcclient.h"
#include <QAbstractListModel>
#include <QQmlEngine>
#include <QHash>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QVariant>
#include <type_traits>



//
class DroneManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY( QAbstractItemModel* drones READ drones CONSTANT )
    Q_PROPERTY( quint32 clientId READ clientId WRITE setClientId NOTIFY clientIdChanged )
    Q_PROPERTY( QString endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged )
    Q_PROPERTY( bool busy READ busy NOTIFY busyChanged )
    Q_PROPERTY( QString lastError READ lastError NOTIFY lastErrorChanged )
    Q_PROPERTY( QString selectedDroneId READ selectedDroneId WRITE setSelectedDroneId NOTIFY selectedDroneIdChanged )
public:
    enum Role{
        IdRole = Qt::UserRole + 1,
        ClientIdRole,
        NameRole,
        TypeRole,
        StateRole,
        OnlineRole,
        LatitudeRole,
        LongitudeRole,
        HasPositionRole,
        StateNameRole
    };
    Q_ENUM( Role )
    explicit DroneManager( QObject* parent = nullptr );
    ~DroneManager() override;

    int rowCount( QModelIndex const& parent = {} ) const override;
    QVariant data( QModelIndex const& index, int role ) const override;
    QHash<int, QByteArray> roleNames() const override;
    QAbstractItemModel* drones(){
        return this;
    }
    quint32 clientId() const{
        return _clientId;
    }
    void setClientId( quint32 id );
    QString endpoint() const{
        return _endpoint;
    }
    void setEndpoint( QString const& endpoint );
    QString selectedDroneId() const{
        return _selectedDroneId;
    }
    void setSelectedDroneId( QString const& id );
    Q_INVOKABLE QVariantMap selectedDronePosition() const;
    Q_INVOKABLE QVariantMap selectedDroneData() const;
    bool busy() const{
        return _pending != 0;
    }
    QString lastError() const{
        return _lastError;
    }
    Q_INVOKABLE void cancelRequests();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE quint64 refreshDrones();
    Q_INVOKABLE quint64 createDrone();
    Q_INVOKABLE quint64 deleteDrone( QString const& id );
    Q_INVOKABLE quint64 updateAttributes( QString const& id, QVariantMap const& values );
    Q_INVOKABLE quint64 getAttribute( QString const& id, QString const& name );
    Q_INVOKABLE quint64 getAttributes( QString const& id );
    Q_INVOKABLE quint64 updatePosition( QString const& id, double latitude, double longitude );
    Q_INVOKABLE quint64 getPosition( QString const& id );
    // Samples: [{name: "latitude", timestampMs: ..., coordinate: ...}, ...].
    // Use decimal strings for timestamps outside JavaScript's exact integer range.
    Q_INVOKABLE quint64 addTimeseries( QString const& id, QVariantList const& samples );
    Q_INVOKABLE quint64 getTimeseries( QString const& id, QString const& name, QVariant timestampMs = {} );
    Q_INVOKABLE quint64 getTimeseriesRange( QString const& id, QString const& name = {}, QVariant startMs = {}, QVariant endMs = {} );

signals:
    void clientIdChanged();
    void endpointChanged();
    void busyChanged();
    void lastErrorChanged();
    void selectedDroneIdChanged();
    // IDs correlate replies with requests. Payloads contain only application/QML values.
    void operationFinished( quint64 requestId, QString operation, QString droneId, QVariant result );
    void operationFailed( quint64 requestId, QString operation, QString droneId, QString code, QString message );

private:
    template <class Work, class Success>
    quint64 submit( QString operation, QString droneId, Work work, Success success ){
        if ( _shutdown )
            return 0;
        const auto                   requestId  = ++_nextRequestId;
        const auto                   client     = _client;
        const QPointer<DroneManager> receiver( this );
        if ( ++_pending == 1 )
            emit busyChanged();
        _pool.start( QRunnable::create( [receiver, client, requestId, operation = std::move( operation ),
                                         droneId = std::move( droneId ), work = std::move( work ),
                                         success = std::move( success )]() mutable {
            using Result = std::invoke_result_t<Work, gRPCClient&>;
            auto result  = [&]() -> Result {
                try{
                    return work( *client );
                }
                catch ( std::exception const& error ){
                    return std::unexpected( RpcError{ RpcErrorCode::Internal, QString::fromUtf8( error.what() ) } );
                }
                catch ( ... ){
                    return std::unexpected(
                        RpcError{ RpcErrorCode::Internal, QStringLiteral( "Unexpected request failure" ) } );
                }
            }();
            // The destructor cancels and joins the pool before QObject destruction,
            // so posting to this context is safe. No model state is accessed here.
            QMetaObject::invokeMethod(
                receiver.data(),
                [receiver, requestId, operation = std::move( operation ), droneId = std::move( droneId ),
                 result = std::move( result ), success = std::move( success )]() mutable {
                    if ( !receiver || receiver->_shutdown )
                        return;
                    QVariant payload;
                    if ( result ){
                        if constexpr ( std::is_void_v<typename Result::value_type> )
                            payload = success();
                        else
                            payload = success( *result );
                    }
                    if ( !receiver || receiver->_shutdown )
                        return;
                    if ( --receiver->_pending == 0 )
                        emit receiver->busyChanged();
                    if ( !receiver || receiver->_shutdown )
                        return;
                    if ( result )
                        emit receiver->operationFinished( requestId, operation, droneId, payload );
                    else
                        receiver->reportError( requestId, operation, droneId, result.error() );
                },
                Qt::QueuedConnection );
        } ) );
        return requestId;
    }
    void resetSession( bool clearModel );
    void reportError( quint64 request, QString const& operation, QString const& id, RpcError const& error );
    quint64 reject( QString const& operation, QString const& id, QString message );
    int indexOf( QString const& id ) const;
    void replaceDevices( QVector<Drone> devices );
    void upsert( Drone const& drone );
    void applyAttributes( QString const& id, DroneAttributes const& values );
    void applyChanges( QString const& id, DroneAttributeChanges const& changes );
    quint64 fetchAttributes( QString const& id );

    QVector<Drone>                  _drones;
    QString                         _selectedDroneId;
    QHash<QString, DroneAttributes> _attributes;
    quint32                         _clientId{ 99 };
    //
    QString                         _endpoint{ QStringLiteral( "127.0.0.1:5600" ) };
    std::shared_ptr<gRPCClient>     _client;
    bool                        _shutdown{};
    QString                     _lastError;
    //
    quint64                     _nextRequestId{};
    int                         _pending{};
    QThreadPool                 _pool;
};
