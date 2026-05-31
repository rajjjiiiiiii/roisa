#pragma once
// BgWorker.h — Runs a std::function on a worker thread and reports completion.
//
// Usage: move a BgWorker onto a QThread, connect QThread::started → run(),
// and connect done()/failed() to GUI-thread handlers.  The work function
// should compute into caller-owned storage and not touch shared GUI state;
// apply the result in the done() handler (which runs on the GUI thread).

#include <QObject>
#include <QString>
#include <functional>

class BgWorker : public QObject
{
    Q_OBJECT
public:
    explicit BgWorker(std::function<void()> fn, QObject* parent = nullptr)
        : QObject(parent), m_fn(std::move(fn)) {}

public slots:
    void run() {
        try {
            m_fn();
            emit done();
        } catch (const std::exception& e) {
            emit failed(QString::fromUtf8(e.what()));
        } catch (...) {
            emit failed("unknown error");
        }
    }

signals:
    void done();
    void failed(QString message);

private:
    std::function<void()> m_fn;
};
