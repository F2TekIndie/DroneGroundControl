#include <QGuiApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QtQuick/QQuickView>
#include <QQuickItem>
#include <QQmlApplicationEngine>
#include <gst/gst.h>
#include "video/receiver.h"
#include "dronemanager.h"



//
int main(int argc, char *argv[])
{
    //
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi( QSGRendererInterface::OpenGL );
    //
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"test-video", "Display local color bars through the same GL sink."});
    parser.addOption({"start", "Start reception (or test video) automatically."});
    parser.addOption({"snapshot", "Save this application's window after two seconds, then exit.", "file"});
    parser.process(app);
    qInfo() << "Qt version:" << qVersion() << "platform:" << QGuiApplication::platformName();

    //
    GError *error = nullptr;
    if ( !gst_init_check(nullptr, nullptr, &error) ){
        qCritical() << error->message;
        g_error_free(error);
        return 1;
    }
    // Loading this factory registers org.freedesktop.gstreamer.Qt6GLVideoItem.
    // It must happen before the QML engine loads Main.qml.
    GstElement *registrationSink = gst_element_factory_make("qml6glsink", nullptr);
    if ( !registrationSink ){
        qCritical() << "qml6glsink missing; install gstreamer1-plugins-good-qt6";
        return 1;
    }
    /////////////////////////////
    Receiver receiver;
    receiver.setTestVideo(parser.isSet("test-video"));
    DroneManager droneManager;
    //
    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {"receiver", QVariant::fromValue( &receiver )},
        {"dm", QVariant::fromValue( &droneManager )}
    });
    engine.loadFromModule("DroneGroundControl", "Main");
    //
    gst_object_unref(registrationSink);
    //
    QList<QObject*> rootObjs = engine.rootObjects();
    if (rootObjs.isEmpty()) {
        qCritical() << "Main.qml failed to load; see the QML errors above.";
        return 1;
    }
    auto *window = qobject_cast<QQuickWindow*>( rootObjs.first() );
    auto *item = window ? window->findChild<QQuickItem*>( QStringLiteral("videoItem") ) : nullptr;
    if ( !window || !item ){
        qCritical() << "QML window/video item missing";
        return 1;
    }
    receiver.attach(window, item);
    if (parser.isSet("start"))
        QTimer::singleShot(0, &receiver, &Receiver::start);
    if (parser.isSet("snapshot")) {
        const QString path = parser.value("snapshot");
        QTimer::singleShot(2000, window, [window, path, &app] {
            const bool saved = window->grabWindow().save(path);
            if (!saved) qWarning() << "Could not save window snapshot:" << path;
            app.exit(saved ? 0 : 1);
        });
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &receiver, &Receiver::stop);
    //
    const int result = app.exec();
    receiver.stop(); // Engine and its video item are still alive here.
    return result;
}
