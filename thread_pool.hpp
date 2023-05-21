#pragma once
#include <thread>
#include <atomic>
#include <queue>
#include <vector>
#include <mutex>
#include <functional>
#include <condition_variable>

struct JobQueue{
    std::queue<std::function<void()>> queue;
    std::atomic<std::size_t> size = 0;
    std::mutex mtx;
    std::condition_variable wait_job_cv;
};

class work_steal_thread_pool{
private:
    std::function<void()> job;
    std::mutex condition_mutex;
    std::condition_variable running_cv;
    std::atomic_bool running = false;
    std::atomic_bool joined = false;
    std::thread th;

    JobQueue& job_queue;
public:
    void wakeup(){
        if(joined) throw std::runtime_error("the thread is already joined!!");
        running = true;
        running.notify_all();
    }
    bool is_running() const noexcept {
        return running && not joined;
    }
    bool is_joined() const noexcept {
        return joined;
    }
    work_steal_thread_pool(JobQueue& queue_ref):
        job_queue(queue_ref),
        job([]{}),
        th([this](){
            bool job_queue_empty = true;
            while(not joined){
                {
                    std::lock_guard lock(job_queue.mtx);
                    job_queue_empty = job_queue.queue.empty();
                    if(not job_queue_empty){
                        running = true;
                        job = std::move(job_queue.queue.front());
                        job_queue.queue.pop();
                        --(job_queue.size);
                    }
                }
                if(not job_queue_empty) job();
                if(job_queue.size == 0){ // sleep
                    running = false;
                    job_queue.wait_job_cv.notify_all();
                    running.wait(false);
                }
            }
        }){
    }
    void join(){
        if(th.joinable()){
            joined = true;
            running = true;
            running.notify_all();
            th.join();
        }
    }
    ~work_steal_thread_pool(){
        join();
    }
};

class job_queue_service{
private:
    std::vector<work_steal_thread_pool*> thread_pool_ptrs;
    JobQueue job_queue;
public:
    template<class F, class... Args>
    void post(F&& f, Args&&... args){
        {
            std::lock_guard queue_lock(job_queue.mtx);
            job_queue.queue.push(static_cast<std::function<void()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...)));
            ++(job_queue.size);
        }
    }

    void wakeup_all(){
        for(auto& pool_ptr : thread_pool_ptrs){
            if(not pool_ptr->is_running()) pool_ptr->wakeup();
        }
    }

    void wait(){
        std::unique_lock job_queue_lock(job_queue.mtx);
        job_queue.wait_job_cv.wait(job_queue_lock, [this](){
            for(auto& pool_ptr : thread_pool_ptrs){
                if(pool_ptr->is_running()) return false;
            }
            return true;
        });
    }

    job_queue_service(int cores = std::thread::hardware_concurrency()) : thread_pool_ptrs(cores, nullptr){
        std::size_t hardware_destructive_interference_size = 64;
        for(auto& pool_ptr : thread_pool_ptrs){
            pool_ptr = new (std::align_val_t(hardware_destructive_interference_size)) work_steal_thread_pool(job_queue);
        }
    }
    ~job_queue_service(){
        //wait();
        for(auto& pool_ptr : thread_pool_ptrs){
            pool_ptr->join();
            delete pool_ptr;
            pool_ptr = nullptr;
        }
    }
    int job_count_in_queue() const noexcept {
        return job_queue.size;
    }

    int thread_pool_count() const noexcept {
        return thread_pool_ptrs.size();
    }
};