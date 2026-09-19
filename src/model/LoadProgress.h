#pragma once

#include <QString>
#include <QThreadPool>
#include <cstdint>
#include <memory>
#include <mutex>

// One instance per job. Workers never retain a window/model pointer.
class LoadProgress
{
  public:
    enum Phase { Opening, Decoding, Processing, Rendering };
    struct Snapshot {
        Phase phase = Opening;
        uint64_t completed = 0, total = 0, generation = 0;
        QString step, context;
    };
    explicit LoadProgress(uint64_t generation = 0) { m_value.generation = generation; }
    void begin(Phase phase, uint64_t total = 0, const QString& step = QString())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_enabled) return;
        m_value.phase = phase; m_value.completed = 0; m_value.total = total; m_value.step = step;
    }
    void advance(uint64_t count = 1)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_enabled) m_value.completed += count;
    }
    void setContext(const QString& context)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_enabled) m_value.context = context;
    }
    Snapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_value;
    }
    void stop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enabled = false;
    }
  private:
    mutable std::mutex m_mutex;
    Snapshot m_value;
    bool m_enabled = true;
};

using Progress = std::shared_ptr<LoadProgress>;

inline QThreadPool* imageLoadPool()
{
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured);
    return &pool;
}
