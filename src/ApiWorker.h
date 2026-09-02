#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <atomic>

// Runs enqueued jobs (API calls) on a single background thread, off the
// render thread. WinHTTP calls in ApiClient are synchronous/blocking — a
// telemetry POST taking a few hundred ms is unremarkable over the network,
// but doing that on the same thread that draws the overlay would show up as
// exactly the kind of stutter this app exists to get rid of.
//
// Jobs run strictly in submission order on one thread, so callers don't need
// their own locking as long as they only touch shared state from inside a
// job or via the result-mailbox pattern used in App.cpp.
class ApiWorker
{
public:
    ApiWorker();
    ~ApiWorker();

    ApiWorker(const ApiWorker&) = delete;
    ApiWorker& operator=(const ApiWorker&) = delete;

    void Enqueue(std::function<void()> job);

private:
    void Run();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_queue;
    std::atomic<bool> m_stop{false};
};
