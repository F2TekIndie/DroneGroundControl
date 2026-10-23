#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>

//
class QQuickItem;
class QQuickWindow;
struct ReceiverSession;

//
class Receiver : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Receiver is owned and supplied by C++")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    explicit Receiver(QObject *parent = nullptr);
    ~Receiver() override;
    //
    void attach(QQuickWindow *window, QQuickItem *item);
    void setTestVideo(bool enabled) { testVideo_ = enabled; }
    bool active() const { return bool(session_); }
    QString status() const { return status_; }
    //
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void changed();

private:
    void pollBus();
    void fail(const QString &message);
    //
    int payload_={96};
    bool testVideo_ = false;
    QPointer<QQuickWindow> window_={nullptr};
    QPointer<QQuickItem>   item_={nullptr};
    QTimer timer_;
    QString status_ = QStringLiteral("Stopped");
    std::shared_ptr<ReceiverSession> session_={nullptr};
};
