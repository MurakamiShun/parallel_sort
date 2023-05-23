#pragma once
#include <thread>
#include <atomic>
#include <queue>
#include <vector>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <unordered_map>

class task_queue_service{
private:
    class work_steal_thread_pool{
    public:
        struct TaskQueue{
            std::queue<std::function<void()>> queue;
            std::atomic<std::size_t> size = 0;
            std::mutex mtx;
        } task_queue;
    private:
        std::function<void()> task;
        std::condition_variable running_cv;
        std::atomic_bool running = false;
        std::atomic_bool joined = false;
        std::thread th;

        std::vector<TaskQueue*>& other_thread_queues;
        std::condition_variable& wait_task_cv;
    public:

        work_steal_thread_pool(std::vector<TaskQueue*>& other_queues, std::condition_variable& wait_cv):
            other_thread_queues(other_queues),
            wait_task_cv(wait_cv),
            task([]{}){
            th = std::thread([this](){
                bool task_queue_empty = true;
                while(not joined){
                    { // self task queue
                        std::lock_guard lock(task_queue.mtx);
                        task_queue_empty = task_queue.queue.empty();
                        if(not task_queue_empty){
                            running = true;
                            task = std::move(task_queue.queue.front());
                            task_queue.queue.pop();
                            --(task_queue.size);
                        }
                    }
                    if(task_queue_empty){ // other task queue
                        for(auto other_thread_queue : other_thread_queues){
                            std::lock_guard lock(other_thread_queue->mtx);
                            task_queue_empty = other_thread_queue->queue.empty();
                            if(not task_queue_empty){
                                running = true;
                                task = std::move(other_thread_queue->queue.front());
                                other_thread_queue->queue.pop();
                                --(other_thread_queue->size);
                                break;
                            }
                        }
                    }
                    if(not task_queue_empty) task();
                    else { // sleep
                        running = false;
                        wait_task_cv.notify_all();
                        running.wait(false);
                    }
                }
            });
        }

        template<class F, class... Args>
        void post(F&& f, Args&&... args){
            std::lock_guard queue_lock(task_queue.mtx);
            task_queue.queue.push(static_cast<std::function<void()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...)));
            ++(task_queue.size);
        }

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
        std::thread::id get_thread_id() const noexcept{
            return th.get_id();
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
        int task_count_in_queue() const noexcept {
            return task_queue.size;
        }
    };

    std::vector<work_steal_thread_pool*> thread_pool_ptrs;
    std::vector<work_steal_thread_pool::TaskQueue*> other_thread_queues;
    std::condition_variable wait_task_cv;

    std::unordered_map<std::thread::id, work_steal_thread_pool*> thread_pool_ptrs_map;
public:
    template<class F, class... Args>
    void post(F&& f, Args&&... args){
        static std::atomic<std::size_t> task_idx = 0;
        work_steal_thread_pool* current_thread_ptr = thread_pool_ptrs[task_idx];
        task_idx = (task_idx+1) % thread_pool_ptrs.size();
        const auto current_thread_iter = thread_pool_ptrs_map.find(std::this_thread::get_id());
        
        if(current_thread_iter != thread_pool_ptrs_map.end()){
            current_thread_ptr = current_thread_iter->second;
        }
        current_thread_ptr->post(std::forward<F>(f), std::forward<Args>(args)...);
    }

    void wakeup_all(){
        for(auto& pool_ptr : thread_pool_ptrs){
            if(not pool_ptr->is_running()) pool_ptr->wakeup();
        }
    }

    void wait(){
        std::mutex dummy_mutex;
        std::unique_lock lock(dummy_mutex);
        wait_task_cv.wait(lock, [this](){
            for(auto& pool_ptr : thread_pool_ptrs){
                if(pool_ptr->is_running()) return false;
            }
            return true;
        });
    }

    task_queue_service(int cores = std::thread::hardware_concurrency()) : thread_pool_ptrs(cores, nullptr){
        std::size_t hardware_destructive_interference_size = 64;
        for(auto& pool_ptr : thread_pool_ptrs){
            pool_ptr = new (std::align_val_t(hardware_destructive_interference_size)) work_steal_thread_pool(other_thread_queues, wait_task_cv);
            other_thread_queues.push_back(&(pool_ptr->task_queue));
            thread_pool_ptrs_map[pool_ptr->get_thread_id()] = pool_ptr;
        }
    }
    ~task_queue_service(){
        wait();
        for(auto& pool_ptr : thread_pool_ptrs){
            pool_ptr->join();
            delete pool_ptr;
            pool_ptr = nullptr;
        }
    }
    
    int task_count_in_queue() const noexcept {
        int total = 0;
        for(auto& pool_ptr : thread_pool_ptrs){
            total += pool_ptr->task_count_in_queue();
        }
        return total;
    }

    int thread_pool_count() const noexcept {
        return thread_pool_ptrs.size();
    }
};