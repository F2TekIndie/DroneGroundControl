#pragma once

#include "drone.h"
#include <QVector>
#include <chrono>
#include <expected>
#include <memory>
#include <optional>



//
struct DroneRef{
    QString id;
    quint32 clientId{};
};

// Optional fields distinguish an omitted attribute from false, zero, or (0, 0).
// Used for partial writes and single-attribute replies, not stored drone state.
struct DroneAttributeChanges{
    std::optional<quint32>       state;
    std::optional<bool>          online;
    std::optional<DronePosition> position;
};

//
struct DroneSample{
    QString name;
    qint64  timestampMs{};
    double  coordinate{};
};
using DroneSamples = QVector<DroneSample>;

// No transport types cross this boundary. Calls are synchronous; the manager
// runs them in QRunnable jobs. One client can serve multiple concurrent jobs.
enum class RpcErrorCode{
    Cancelled,
    Timeout,
    Unavailable,
    NotFound,
    InvalidRequest,
    Rejected,
    InvalidResponse,
    ResourceExhausted,
    Internal
};

//
struct RpcError{
    RpcErrorCode code;
    QString      message;
};
template <class T> using RpcResult = std::expected<T, RpcError>;

//
class gRPCClient
{
public:
    explicit gRPCClient( QString                   endpoint = QStringLiteral( "127.0.0.1:5600" ),
                         std::chrono::milliseconds timeout  = std::chrono::seconds( 5 ) );
    ~gRPCClient();
    gRPCClient( gRPCClient const& )            = delete;
    gRPCClient& operator=( gRPCClient const& ) = delete;

    RpcResult<Drone> createDrone( quint32 clientId ) const;
    RpcResult<void> deleteDrone( DroneRef const& drone ) const;
    RpcResult<QVector<Drone>> getDrones( quint32 clientId ) const;
    RpcResult<void> updateAttributes( DroneRef const& drone, DroneAttributeChanges const& values ) const;
    RpcResult<DroneAttributeChanges> getAttribute( DroneRef const& drone, QString const& name ) const;
    RpcResult<DroneAttributes> getAttributes( DroneRef const& drone ) const;
    RpcResult<void> updatePosition( DroneRef const& drone, DronePosition const& position ) const;
    RpcResult<DronePosition> getPosition( DroneRef const& drone ) const;
    RpcResult<void> addTimeseries( DroneRef const& drone, DroneSamples const& samples ) const;
    RpcResult<DroneSample> getTimeseries( DroneRef const& drone, QString const& name,
                                          std::optional<qint64> timestampMs = {} ) const;
    RpcResult<DroneSamples> getTimeseriesRange( DroneRef const& drone, QString const& name = {},
                                                std::optional<qint64> startMs = {},
                                                std::optional<qint64> endMs   = {} ) const;

    // Cancels active calls and prevents queued jobs from starting more calls.
    // A new connection/session uses a new client instance.
    void cancelAll();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
