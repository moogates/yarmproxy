// https://github.com/nbsdx/ThreadPool
#ifndef CONCURRENT_THREADPOOL_H
#define CONCURRENT_THREADPOOL_H

#include <atomic>
#include <thread>
#include <mutex>
#include <array>
#include <list>
#include <functional>
#include <condition_variable>

namespace nbsdx {
namespace concurrent {

/**
 *  Simple ThreadPool that creates `ThreadCount` threads upon its creation,
 *  and pulls from a queue to get new jobs. The default is 10 threads.
 *
 *  This class requires a number of c++11 features be present in your compiler.
 */
template <unsigned ThreadCount = 10>
class ThreadPool {
    
    std::array<std::thread, ThreadCount> threads_;
    std::list<std::function<void(void)>> job_queue_;

    std::atomic_int         jobs_left_;
    std::atomic_bool        bailout_;
    std::atomic_bool        finished;
    std::condition_variable job_available_var_;
    std::condition_variable wait_var_;
    std::mutex              wait_mutex;
    std::mutex              queue_mutex_;

    /**
     *  Take the next job in the queue and run it.
     *  Notify the main thread that a job has completed.
     */
    void Task() {
        while(!bailout_) {
            next_job()();
            --jobs_left_;
            wait_var_.notify_one();
        }
    }

    /**
     *  Get the next job; pop the first item in the queue, 
     *  otherwise wait for a signal from the main thread.
     */
    std::function<void(void)> next_job() {
        std::function<void(void)> res;
        std::unique_lock<std::mutex> job_lock(queue_mutex_);

        // Wait for a job if we don't have any.
        job_available_var_.wait(job_lock,
            [this]() ->bool { return job_queue_.size() || bailout_; }
            );
        
        // Get job from the queue
        if (!bailout_) {
            res = job_queue_.front();
            job_queue_.pop_front();
        } else { // If we're bailing out, 'inject' a job into the queue to keep jobs_left accurate.
            res = []{}; // 返回一个空job
            ++jobs_left_;
        }
        return res;
    }

public:
    ThreadPool()
        : jobs_left_(0)
        , bailout_(false)
        , finished(false) 
    {
        for( unsigned i = 0; i < ThreadCount; ++i )
            threads_[ i ] = std::thread( [this]{ this->Task(); } );
    }

    /**
     *  JoinAll on deconstruction
     */
    ~ThreadPool() {
        JoinAll();
    }

    /**
     *  Get the number of threads in this pool
     */
    inline unsigned Size() const {
        return ThreadCount;
    }

    /**
     *  Get the number of jobs left in the queue.
     */
    inline unsigned JobsRemaining() {
        std::lock_guard<std::mutex> guard(queue_mutex_);
        return job_queue_.size();
    }

    /**
     *  Add a new job to the pool. If there are no jobs in the queue,
     *  a thread is woken up to take the job. If all threads are busy,
     *  the job is added to the end of the queue.
     */
    void AddJob(std::function<void(void)> job) {
        std::lock_guard<std::mutex> guard(queue_mutex_);
        job_queue_.emplace_back( job );
        ++jobs_left_;
        job_available_var_.notify_one();
    }

    /**
     *  Join with all threads. Block until all threads have completed.
     *  Params: WaitForAll: If true, will wait for the queue to empty 
     *          before joining with threads. If false, will complete
     *          current jobs, then inform the threads to exit.
     *  The queue will be empty after this call, and the threads will
     *  be done. After invoking `ThreadPool::JoinAll`, the pool can no
     *  longer be used. If you need the pool to exist past completion
     *  of jobs, look to use `ThreadPool::WaitAll`.
     */
    void JoinAll( bool WaitForAll = true ) {
        if( !finished ) {
            if( WaitForAll ) {
                WaitAll();
            }

            // note that we're done, and wake up any thread that's
            // waiting for a new job
            bailout_ = true;
            job_available_var_.notify_all();

            for( auto &x : threads_)
                if( x.joinable() )
                    x.join();
            finished = true;
        }
    }

    /**
     *  Wait for the pool to empty before continuing. 
     *  This does not call `std::thread::join`, it only waits until
     *  all jobs have finshed executing.
     */
    void WaitAll() {
        if (jobs_left_ > 0) {
            std::unique_lock<std::mutex> lock(wait_mutex);
            wait_var_.wait(lock, 
                  [this]{
                    return this->jobs_left_ == 0; // 可访问私有成员变量
                  }
                );
            lock.unlock(); // 为何这里要调用unlock?不是自动unlock吗？
        }
    }
};

} // namespace concurrent
} // namespace nbsdx

#endif //CONCURRENT_THREADPOOL_H

