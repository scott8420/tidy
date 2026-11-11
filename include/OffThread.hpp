#pragma once
// OffThread<T> -- s019: run one read-only look on a worker thread, hand its
// result back on the window's thread (Glib::Dispatcher). The work must touch
// no widget and no member of the page: give it copies, get a T back.
//
// One look at a time. A start() while one runs asks for one more after it
// (the newest ask wins; the work is the same look, read fresh). The
// destructor waits for a running look -- a page never outlives its thread.
#include <glibmm/dispatcher.h>

#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace tidy {

template <class T>
class OffThread {
public:
    OffThread() {
        m_disp.connect([this] { on_done(); });
    }
    ~OffThread() {
        if (m_thread.joinable()) m_thread.join();
    }
    OffThread(const OffThread&) = delete;
    OffThread& operator=(const OffThread&) = delete;

    bool busy() const { return m_busy; }

    // `work` runs off the window's thread; `done` gets its T on it.
    void start(std::function<T()> work, std::function<void(T)> done) {
        if (m_busy) { m_again = {std::move(work), std::move(done)}; return; }
        if (m_thread.joinable()) m_thread.join();
        m_busy = true;
        m_done = std::move(done);
        m_thread = std::thread([this, w = std::move(work)] {
            T r = w();
            {
                std::lock_guard<std::mutex> g(m_mx);
                m_result = std::move(r);
            }
            m_disp.emit();
        });
    }

private:
    void on_done() {
        if (m_thread.joinable()) m_thread.join();
        std::optional<T> r;
        {
            std::lock_guard<std::mutex> g(m_mx);
            r.swap(m_result);
        }
        m_busy = false;
        auto done = std::move(m_done);
        if (m_again) {   // asked again while it ran: the newest look replaces this one's result
            auto again = std::move(*m_again);
            m_again.reset();
            start(std::move(again.first), std::move(again.second));
            return;
        }
        if (r && done) done(std::move(*r));
    }

    Glib::Dispatcher m_disp;
    std::thread m_thread;
    std::mutex m_mx;
    std::optional<T> m_result;
    std::function<void(T)> m_done;
    std::optional<std::pair<std::function<T()>, std::function<void(T)>>> m_again;
    bool m_busy = false;
};

}  // namespace tidy
