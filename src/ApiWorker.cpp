#include "ApiWorker.h"

ApiWorker::ApiWorker()
{
    m_thread = std::thread(&ApiWorker::Run, this);
}

ApiWorker::~ApiWorker()
{
    m_stop = true;
    m_cv.notify_all();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void ApiWorker::Enqueue(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void ApiWorker::Run()
{
    for (;;)
    {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty())
            {
                return;
            }
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }
        job();
    }
}
