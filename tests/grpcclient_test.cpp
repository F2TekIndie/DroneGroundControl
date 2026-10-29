#include "grpcclient.h"
#include "dronemanager.h"
#include "devices.grpc.pb.h"
#include "attributes.grpc.pb.h"
#include "timeseries.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <QtTest>
#include <QAbstractItemModelTester>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>



//
namespace proto = dronegroundcontrol;

//
namespace{
    const QString Id = QStringLiteral( "12345678-1234-4234-8234-123456789abc" );
    struct TestService : proto::DeviceFunctions::Service,
                         proto::AttributeFunctions::Service,
                         proto::TimeseriesFunctions::Service {
        std::mutex        mutex;
        quint32           state{ 7 };
        bool              online{};
        bool              accepted{ true };
        DronePosition     position{ 52, 13 };
        std::atomic<bool> blockAttributes{}, attributesEntered{}, releaseAttributes{}, unavailable{};
        std::atomic<int>  attributeCalls{}, listCalls{};
        std::atomic<bool> identityValid{ true };
        void Check( proto::Device const& device ){
            if ( device.id() != Id.toStdString() || device.client_id().id() != 99 )
                identityValid = false;
        }
        static void Device( proto::Device* device, quint32 clientId = 99 ){
            device->set_id( Id.toStdString() );
            device->set_name( "Test drone" );
            device->mutable_client_id()->set_id( clientId );
        }
        static void Position( DronePosition const& position, proto::Position* output ){
            output->set_lat( QString::number( position.latitude, 'g', 17 ).toStdString() );
            output->set_long_( QString::number( position.longitude, 'g', 17 ).toStdString() );
        }
        void Attributes( proto::Attributes* output ){
            auto* a = output->add_timeseries();
            a->set_name( "state" );
            a->set_ivalue( state );
            a = output->add_timeseries();
            a->set_name( "online" );
            a->set_bvalue( online );
            a = output->add_timeseries();
            a->set_name( "position" );
            Position( position, a->mutable_pos() );
        }
        grpc::Status CreateDevice( grpc::ServerContext*, proto::Client const* request, proto::Device* reply ) override{
            Device( reply, request->id() );
            return grpc::Status::OK;
        }
        grpc::Status DeletDevice( grpc::ServerContext*, proto::Device const* request,
                                  proto::DeviceStatusReply* reply ) override{
            Check( *request );
            reply->set_success( accepted );
            return grpc::Status::OK;
        }
        grpc::Status ListDevices( grpc::ServerContext*, proto::Client const* request, proto::Devices* reply ) override{
            ++listCalls;
            if ( unavailable )
                return { grpc::StatusCode::UNAVAILABLE, "Test unavailable" };
            Device( reply->add_devicelist(), request->id() );
            return grpc::Status::OK;
        }
        grpc::Status UpdateAttribute( grpc::ServerContext*, proto::AttributeRequest const* request,
                                      proto::AttributeStatusReply* reply ) override{
            Check( request->device() );
            std::lock_guard lock( mutex );
            for ( auto const& value: request->attributes().timeseries() ){
                if ( value.name() == "state" )
                    state = value.ivalue();
                else if ( value.name() == "online" )
                    online = value.bvalue();
                else if ( value.name() == "position" ){
                    position.latitude = QString::fromStdString( value.pos().lat() ).toDouble();
                    position.longitude = QString::fromStdString( value.pos().long_() ).toDouble();
                }
            }
            reply->set_success( accepted );
            return grpc::Status::OK;
        }
        grpc::Status GetAttribute( grpc::ServerContext*, proto::AttributeQuery const* request,
                                   proto::Attribute* reply ) override{
            Check( request->device() );
            std::lock_guard   lock( mutex );
            proto::Attributes all;
            Attributes( &all );
            for ( auto const& value: all.timeseries() )
                if ( value.name() == request->name() ){
                    *reply = value;
                    return grpc::Status::OK;
                }
            return { grpc::StatusCode::NOT_FOUND, "Attribute not found" };
        }
        grpc::Status GetAttributes( grpc::ServerContext* context, proto::Device const* request,
                                    proto::Attributes* reply ) override{
            ++attributeCalls;
            {
                std::lock_guard lock( mutex );
                Attributes( reply );
            }
            // Snapshot before waiting, to deliberately return an older read after a write.
            if ( blockAttributes.exchange( false ) ){
                attributesEntered = true;
                while ( !releaseAttributes && !context->IsCancelled() )
                    std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                if ( context->IsCancelled() )
                    return { grpc::StatusCode::CANCELLED, "Test cancelled" };
            }
            if ( request->id() != Id.toStdString() )
                return { grpc::StatusCode::NOT_FOUND, "Drone not found" };
            return grpc::Status::OK;
        }
        grpc::Status UpdatePosition( grpc::ServerContext*, proto::PositionRequest const* request,
                                     proto::AttributeStatusReply* reply ) override{
            Check( request->device() );
            std::lock_guard lock( mutex );
            position = { QString::fromStdString( request->pos().lat() ).toDouble(),
                         QString::fromStdString( request->pos().long_() ).toDouble() };
            reply->set_success( accepted );
            return grpc::Status::OK;
        }
        grpc::Status GetPosition( grpc::ServerContext*, proto::Device const* request,
                                  proto::Position* reply ) override{
            Check( *request );
            std::lock_guard lock( mutex );
            Position( position, reply );
            return grpc::Status::OK;
        }
        grpc::Status AddTimeseries( grpc::ServerContext*, proto::TimeseriesRequest const* request,
                                    proto::TimeseriesStatusReply* reply ) override{
            Check( request->device() );
            if ( !request->has_timeseries() || request->has_data() || request->timeseries().timeseries_size() != 2 )
                return { grpc::StatusCode::INVALID_ARGUMENT, "Expected a pair" };
            reply->set_success( accepted );
            return grpc::Status::OK;
        }
        static void Sample( std::string const& name, qint64 stamp, proto::TimeSerie* reply ){
            reply->set_name( name );
            reply->set_timestamp( std::to_string( stamp ) );
            reply->set_svalue( "1.25" );
        }
        grpc::Status GetTimeseries( grpc::ServerContext*, proto::TimeseriesQuery const* request,
                                    proto::TimeSerie* reply ) override{
            Check( request->device() );
            Sample( request->name(), request->has_timestamp_ms() ? request->timestamp_ms() : 1234, reply );
            return grpc::Status::OK;
        }
        grpc::Status GetTimeseriesRange( grpc::ServerContext*, proto::TimeseriesRangeQuery const* request,
                                         proto::TimeSeries* reply ) override{
            Check( request->device() );
            for ( auto stamp: { 0, 1000, 2000 } ){
                if ( ( request->has_start_timestamp_ms() && stamp < request->start_timestamp_ms() )
                     || ( request->has_end_timestamp_ms() && stamp > request->end_timestamp_ms() ) )
                    continue;
                for ( auto name: { "latitude", "longitude" } )
                    if ( request->name().empty() || request->name() == name )
                        Sample( name, stamp, reply->add_timeseries() );
            }
            return grpc::Status::OK;
        }
    };
}

//
class ClientTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<TestService>  service;
    std::unique_ptr<grpc::Server> server;
    QString                       endpoint;
private slots:
    void init(){
        service = std::make_unique<TestService>();
        grpc::ServerBuilder builder;
        int                 port{};
        builder.AddListeningPort( "127.0.0.1:0", grpc::InsecureServerCredentials(), &port );
        builder.RegisterService( static_cast<proto::DeviceFunctions::Service*>( service.get() ) );
        builder.RegisterService( static_cast<proto::AttributeFunctions::Service*>( service.get() ) );
        builder.RegisterService( static_cast<proto::TimeseriesFunctions::Service*>( service.get() ) );
        server = builder.BuildAndStart();
        QVERIFY( server );
        endpoint = "127.0.0.1:" + QString::number( port );
    }
    void cleanup(){
        server->Shutdown();
        server.reset();
        service.reset();
    }
    void clientOperations(){
        gRPCClient     client( endpoint );
        const DroneRef drone{ Id, 99 };
        auto           created = client.createDrone( 99 );
        QVERIFY( created );
        QCOMPARE( created->_droneId, Id );
        auto drones = client.getDrones( 99 );
        QVERIFY( drones );
        QCOMPARE( drones->size(), 1 );
        QVERIFY( client.updateAttributes( drone, { .state = 42, .online = true } ) );
        auto attribute = client.getAttribute( drone, "state" );
        QVERIFY( attribute );
        QCOMPARE( attribute->state.value(), 42u );
        auto attributes = client.getAttributes( drone );
        QVERIFY( attributes );
        QCOMPARE( attributes->_state, 42u );
        QVERIFY( attributes->_online );
        QVERIFY( attributes->_hasPosition );
        QVERIFY( client.updatePosition( drone, { 52.1234567890123, 13.9876543210987 } ) );
        auto position = client.getPosition( drone );
        QVERIFY( position );
        QCOMPARE( position->latitude, 52.1234567890123 );
        QVERIFY( client.addTimeseries( drone, { { "latitude", 0, 1.25 }, { "longitude", 0, 1.25 } } ) );
        auto exact = client.getTimeseries( drone, "latitude", 0 );
        QVERIFY( exact );
        QCOMPARE( exact->timestampMs, 0 );
        auto latest = client.getTimeseries( drone, "longitude" );
        QVERIFY( latest );
        QCOMPARE( latest->timestampMs, 1234 );
        auto range = client.getTimeseriesRange( drone, {}, 0, 1000 );
        QVERIFY( range );
        QCOMPARE( range->size(), 4 );
        QVERIFY( client.deleteDrone( drone ) );
        QVERIFY( service->identityValid );
        QVERIFY( !client.getPosition( { "invalid", 99 } ) );
        QVERIFY( !client.addTimeseries( drone, { { "latitude", 0, 1 } } ) );
        QVERIFY( !client.updatePosition( drone, { 91, 0 } ) );
        QVERIFY( !client.getTimeseriesRange( drone, {}, 2, 1 ) );
        service->accepted = false;
        auto rejected     = client.deleteDrone( drone );
        QVERIFY( !rejected );
        QCOMPARE( rejected.error().code, RpcErrorCode::Rejected );
        service->unavailable = true;
        auto error           = client.getDrones( 99 );
        QVERIFY( !error );
        QCOMPARE( error.error().code, RpcErrorCode::Unavailable );
    }
    void partialAttributeUpdates(){
        gRPCClient client( endpoint );
        const DroneRef drone{ Id, 99 };
        QVERIFY( client.updateAttributes( drone, { .state = 42, .online = true } ) );
        QVERIFY( client.updateAttributes( drone, { .state = 0 } ) );
        auto attributes = client.getAttributes( drone );
        QVERIFY( attributes );
        QCOMPARE( attributes->_state, 0u );
        QVERIFY( attributes->_online );
        QCOMPARE( attributes->_pos.latitude, 52.0 );
        QVERIFY( client.updateAttributes( drone, { .online = false } ) );
        QVERIFY( client.updateAttributes( drone, { .position = DronePosition{ 0, 0 } } ) );
        attributes = client.getAttributes( drone );
        QVERIFY( attributes );
        QCOMPARE( attributes->_state, 0u );
        QVERIFY( !attributes->_online );
        QVERIFY( attributes->_hasPosition );
        QCOMPARE( attributes->_pos.latitude, 0.0 );
        QCOMPARE( attributes->_pos.longitude, 0.0 );
        auto state = client.getAttribute( drone, "state" );
        QVERIFY( state && state->state.has_value() );
        QCOMPARE( *state->state, 0u );
        QVERIFY( !state->online && !state->position );
        auto online = client.getAttribute( drone, "online" );
        QVERIFY( online && online->online.has_value() );
        QVERIFY( !*online->online && !online->state && !online->position );
        auto empty = client.updateAttributes( drone, {} );
        QVERIFY( !empty );
        QCOMPARE( empty.error().code, RpcErrorCode::InvalidRequest );
    }
    void managerSeparateAttributes(){
        DroneManager manager;
        manager.setEndpoint( endpoint );
        QAbstractItemModelTester modelTester( &manager, QAbstractItemModelTester::FailureReportingMode::QtTest );
        manager.refreshDrones();
        QTRY_VERIFY_WITH_TIMEOUT( !manager.busy(), 2000 );
        {
            std::lock_guard lock( service->mutex );
            service->position = { 58, 10 };
            service->state = 42;
        }
        manager.getAttribute( Id, "state" );
        QTRY_VERIFY_WITH_TIMEOUT( !manager.busy(), 2000 );
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::StateRole ).toUInt(), 42u );
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::LatitudeRole ).toDouble(), 52.0 );
        manager.getPosition( Id );
        QTRY_VERIFY_WITH_TIMEOUT( !manager.busy(), 2000 );
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::StateRole ).toUInt(), 42u );
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::LatitudeRole ).toDouble(), 58.0 );
        service->blockAttributes = true;
        manager.refreshDrones();
        QTRY_VERIFY_WITH_TIMEOUT( service->attributesEntered.load(), 1000 );
        // Refreshing identities must retain the separate attribute snapshot.
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::StateRole ).toUInt(), 42u );
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::LatitudeRole ).toDouble(), 58.0 );
        service->releaseAttributes = true;
        QTRY_VERIFY_WITH_TIMEOUT( !manager.busy(), 2000 );
        manager.setClientId( 100 );
        QCOMPARE( manager.rowCount(), 0 );
        manager.shutdown();
    }
    void deadlinesAndCancellation(){
        service->blockAttributes = true;
        gRPCClient shortDeadline( endpoint, std::chrono::milliseconds( 50 ) );
        auto       timedOut = shortDeadline.getAttributes( { Id, 99 } );
        QVERIFY( !timedOut );
        QCOMPARE( timedOut.error().code, RpcErrorCode::Timeout );
        service->attributesEntered = false;
        service->blockAttributes   = true;
        gRPCClient client( endpoint );
        auto       future = std::async( std::launch::async, [&] {
            return client.getAttributes( { Id, 99 } );
        } );
        QTRY_VERIFY_WITH_TIMEOUT( service->attributesEntered.load(), 1000 );
        client.cancelAll();
        QVERIFY( future.wait_for( std::chrono::seconds( 1 ) ) == std::future_status::ready );
        auto result = future.get();
        QVERIFY( !result );
        QCOMPARE( result.error().code, RpcErrorCode::Cancelled );
        auto queued = client.getDrones( 99 );
        QVERIFY( !queued );
        QCOMPARE( queued.error().code, RpcErrorCode::Cancelled );
    }
    void managerModelAndWrites(){
        DroneManager manager;
        manager.setEndpoint( endpoint );
        QAbstractItemModelTester modelTester( &manager, QAbstractItemModelTester::FailureReportingMode::QtTest );
        QSignalSpy               failures( &manager, &DroneManager::operationFailed );
        bool                     guiThread = true;
        connect( &manager, &QAbstractItemModel::dataChanged, &manager, [&] {
            guiThread &= QThread::currentThread() == manager.thread();
        } );
        connect( &manager, &DroneManager::operationFinished, &manager, [&] {
            guiThread &= QThread::currentThread() == manager.thread();
        } );
        QVERIFY( manager.refreshDrones() );
        QTRY_VERIFY_WITH_TIMEOUT( !manager.busy(), 2000 );
        QCOMPARE( manager.rowCount(), 1 );
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::StateRole ).toUInt(), 7u );
        manager.updateAttributes( Id, { { "state", 42 } } );
        QTRY_COMPARE_WITH_TIMEOUT( manager.data( manager.index( 0 ), DroneManager::StateRole ).toUInt(), 42u, 2000 );
        QTRY_VERIFY_WITH_TIMEOUT( !manager.busy(), 2000 );
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::StateRole ).toUInt(), 42u );
        QVERIFY( guiThread );
        QVERIFY( failures.isEmpty() );
        const auto reads = service->attributeCalls.load();
        manager.updatePosition( Id, 91, 0 );
        QTRY_VERIFY_WITH_TIMEOUT( !manager.busy(), 2000 );
        QCOMPARE( failures.size(), 1 );
        QCOMPARE( failures[0][3].toString(), QStringLiteral( "invalidRequest" ) );
        QCOMPARE( service->attributeCalls.load(), reads );
        manager.shutdown();
    }
    void managerSessionAndLifetime(){
        DroneManager manager;
        manager.setEndpoint( endpoint );
        service->blockAttributes = true;
        QSignalSpy failures( &manager, &DroneManager::operationFailed );
        manager.refreshDrones();
        QTRY_VERIFY_WITH_TIMEOUT( service->attributesEntered.load(), 1000 );
        const auto lists = service->listCalls.load();
        QVERIFY( manager.refreshDrones() );
        QTRY_VERIFY_WITH_TIMEOUT( service->listCalls.load() > lists, 1000 );
        QVERIFY( manager.busy() ); // The first attribute read is still pending.
        manager.setClientId( 100 ); // Cancels old calls; their completions still settle _pending.
        service->releaseAttributes = true;
        manager.refreshDrones();
        QTRY_VERIFY_WITH_TIMEOUT( !manager.busy(), 2000 );
        QCOMPARE( manager.data( manager.index( 0 ), DroneManager::ClientIdRole ).toUInt(), 100u );
        for ( auto const& failure: failures )
            QCOMPARE( failure[3].toString(), QStringLiteral( "cancelled" ) );
        manager.shutdown();
        auto dying = std::make_unique<DroneManager>();
        dying->setEndpoint( endpoint );
        service->releaseAttributes = false;
        service->attributesEntered = false;
        service->blockAttributes   = true;
        dying->getAttributes( Id );
        QTRY_VERIFY_WITH_TIMEOUT( service->attributesEntered.load(), 1000 );
        QElapsedTimer timer;
        timer.start();
        dying.reset();
        QVERIFY( timer.elapsed() < 1000 ); // Joins cancelled jobs without GUI callbacks or deadlock.
        QCoreApplication::processEvents();
    }
};
QTEST_GUILESS_MAIN( ClientTest )
#include "grpcclient_test.moc"
