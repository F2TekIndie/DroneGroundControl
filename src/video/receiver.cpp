#include "receiver.h"
#include <QDebug>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRunnable>
#include <QElapsedTimer>
#include <gst/gst.h>
#include <atomic>
#include <mutex>
#include <utility>



// A queued render job may outlive Stop. Share ownership and serialize state
// changes so that an old job cannot restart a cancelled pipeline.
struct ReceiverSession
{
    GstElement* pipeline = nullptr;
    GstBus*     bus = nullptr;
    std::mutex mutex;
    bool cancelled = false;
    QString startupError = "";
    std::atomic<quint64> packets{0};
    std::atomic<quint64> decoded{0};
    std::atomic<quint64> uploaded{0};
    QElapsedTimer elapsed;
    qint64 lastReport = 0;
    ~ReceiverSession()
    {
        if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
        if (bus) gst_object_unref(bus);
        if (pipeline) gst_object_unref(pipeline);
    }
};

//
namespace{
    static void countBuffers(GstElement *pipeline, const char *elementName,
                             const char *padName, std::atomic<quint64> *counter)
    {
        GstElement *element = gst_bin_get_by_name(GST_BIN(pipeline), elementName);
        GstPad *pad = gst_element_get_static_pad(element, padName);
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                          [](GstPad *, GstPadProbeInfo *, gpointer data) -> GstPadProbeReturn {
                              static_cast<std::atomic<quint64> *>(data)->fetch_add(1, std::memory_order_relaxed);
                              return GST_PAD_PROBE_OK;
                          }, counter, nullptr);
        gst_object_unref(pad);
        gst_object_unref(element);
    }
}

//
class StartJob final : public QRunnable
{
public:
    explicit StartJob(std::shared_ptr<ReceiverSession> session)
        :session_(std::move(session))
    {}
    void run() override
    {
        std::lock_guard<std::mutex> lock(session_->mutex);
        if (session_->cancelled)
            return;
        GstElement *sink = gst_bin_get_by_name(GST_BIN(session_->pipeline), "videoSink");
        // Obtain Qt's GstGLDisplay before glupload/glcolorconvert start.
        const auto ready = gst_element_set_state(sink, GST_STATE_READY);
        gst_object_unref(sink);
        if ( ready == GST_STATE_CHANGE_FAILURE || gst_element_set_state(session_->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE )
            session_->startupError = QStringLiteral("Could not start the GL pipeline; inspect GStreamer diagnostics.");
    }
private:
    std::shared_ptr<ReceiverSession> session_;
};

Receiver::Receiver(QObject *parent)
    :QObject(parent)
{
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, &Receiver::pollBus);
}

Receiver::~Receiver(){
    stop();
}

void Receiver::attach(QQuickWindow *window, QQuickItem *item)
{
    window_ = window;
    item_ = item;
    // Preserve GL resources when minimized/hidden; stop before window teardown.
    window->setPersistentGraphics(true);
    window->setPersistentSceneGraph(true);
}

void Receiver::start()
{
    if (session_)
        return;
    if (!window_ || !item_){
        fail(QStringLiteral("Video item is unavailable."));
        return;
    }
    const QString source = testVideo_
        ? QStringLiteral("videotestsrc name=udpSource is-live=true pattern=smpte ! "
                         "video/x-raw,width=640,height=360,framerate=30/1 ! identity name=decoder ! ")
        : QStringLiteral(
                                    "udpsrc name=udpSource port=5600 reuse=false caps=\"application/x-rtp,media=video,"
                                    "encoding-name=H264,clock-rate=90000,payload=%1\" ! "
                                    "rtpjitterbuffer latency=100 drop-on-latency=true ! rtph264depay ! "
                                    "h264parse ! openh264dec name=decoder ! ").arg(payload_);
    const QString description = source + QStringLiteral(
                                    "queue max-size-buffers=3 max-size-bytes=0 "
                                    "max-size-time=0 leaky=downstream ! videoconvert ! "
                                    // Keep upstream buffers in CPU memory instead of allowing
                                    // GL/EGL allocation to propagate into the decoder/converter.
                                    "video/x-raw,format=RGBA ! identity drop-allocation=true ! "
                                    "glupload ! glcolorconvert ! "
                                    "video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D ! "
                                    "qml6glsink name=videoSink sync=false");
    qInfo().noquote() << "Starting video pipeline:" << description;
    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(description.toUtf8().constData(), &error);
    if (error || !pipeline) {
        const QString message = error ? QString::fromUtf8(error->message) : QStringLiteral("Pipeline creation failed.");
        if (error)
            g_error_free(error);
        if (pipeline)
            gst_object_unref(pipeline);
        fail(message);
        return;
    }
    auto session = std::make_shared<ReceiverSession>();
    session->pipeline = pipeline;
    session->bus = gst_element_get_bus(pipeline);
    session->elapsed.start();
    // Probes touch only atomic counters; Qt properties remain on the GUI thread.
    countBuffers(pipeline, "udpSource", "src", &session->packets);
    countBuffers(pipeline, "decoder", "src", &session->decoded);
    countBuffers(pipeline, "videoSink", "sink", &session->uploaded);
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "videoSink");
    g_object_set(sink, "widget", item_.data(), nullptr);
    gst_object_unref(sink);
    session_ = session;
    status_ = QStringLiteral("Starting; waiting for the window to render…");
    timer_.start();
    window_->scheduleRenderJob(new StartJob(session), QQuickWindow::BeforeSynchronizingStage);
    window_->update();
    emit changed();
}

void Receiver::stop()
{
    timer_.stop();
    if (session_){
        {
            std::lock_guard<std::mutex> lock(session_->mutex);
            session_->cancelled = true;
            gst_element_set_state(session_->pipeline, GST_STATE_NULL);
            GstElement *sink = gst_bin_get_by_name(GST_BIN(session_->pipeline), "videoSink");
            g_object_set(sink, "widget", nullptr, nullptr);
            gst_object_unref(sink);
        }
        session_.reset();
    }
    status_ = QStringLiteral("Stopped");
    emit changed();
}

void Receiver::fail(const QString &message)
{
    stop();
    status_ = QStringLiteral("Error: ") + message;
    qWarning().noquote() << status_;
    emit changed();
}

void Receiver::pollBus()
{
    if (!session_)
        return;
    // No GLib main loop is needed: consume bus messages on the Qt GUI thread.
    while (GstMessage *message = gst_bus_pop(session_->bus)) {
        QString fatal;
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            fatal = QStringLiteral("%1: %2").arg(
                QString::fromUtf8(GST_OBJECT_NAME(message->src)), QString::fromUtf8(error->message));
            qWarning().noquote() << fatal << (debug ? debug : "");
            g_error_free(error);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError *warning = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_warning(message, &warning, &debug);
            qWarning() << warning->message << (debug ? debug : "");
            g_error_free(warning);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_EOS:
            fatal = QStringLiteral("End of stream.");
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (message->src == GST_OBJECT(session_->pipeline)) {
                GstState oldState, newState, pending;
                gst_message_parse_state_changed(message, &oldState, &newState, &pending);
                if (newState == GST_STATE_PLAYING) {
                    status_ = QStringLiteral("Listening on UDP 5600 (H.264 RTP, PT %1)").arg(payload_);
                    emit changed();
                }
            }
            break;
        default: break;
        }
        gst_message_unref(message);
        if (!fatal.isEmpty()){
            fail(fatal);
            return;
        }
    }
    QString startupError;
    {
        std::lock_guard<std::mutex> lock(session_->mutex);
        startupError = session_->startupError;
    }
    if ( !startupError.isEmpty() )
        { fail(startupError); return; }

    const qint64 now = session_->elapsed.elapsed();
    if (now - session_->lastReport >= 1000) {
        session_->lastReport = now;
        const auto packets = session_->packets.load(std::memory_order_relaxed);
        const auto decoded = session_->decoded.load(std::memory_order_relaxed);
        const auto uploaded = session_->uploaded.load(std::memory_order_relaxed);
        const QString progress = uploaded ? QStringLiteral("Frames reached the GL sink")
            : decoded ? QStringLiteral("Decoded frames; waiting for GL upload")
            : packets ? QStringLiteral("UDP arriving; waiting for H.264 frames/keyframe")
            : QStringLiteral("Waiting for UDP packets on port 5600");
        status_ = QStringLiteral("%1 | %2: %3 | decoded: %4 | GL sink: %5 | display: %6×%7")
            .arg(progress, testVideo_ ? QStringLiteral("test frames") : QStringLiteral("UDP packets"))
            .arg(packets).arg(decoded).arg(uploaded)
            .arg(item_ ? item_->width() : 0).arg(item_ ? item_->height() : 0);
        emit changed();
    }
}
