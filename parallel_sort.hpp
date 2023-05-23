#pragma once
#include <utility>
#include <new>
#include "thread_pool.hpp"
#include <mutex>
#include <omp.h>

template<class Compare, class Ref>
void swap_if(Compare comp, Ref&& right, Ref&& left){
    if(comp(right, left)) std::swap(std::forward<Ref>(right), std::forward<Ref>(left));
}

template<class RandomAccessIterator, class Compare>
void heap_sort(RandomAccessIterator first, RandomAccessIterator last, Compare comp){
    using diff_type = typename RandomAccessIterator::difference_type;
    const diff_type N = std::distance(first, last);

    // make heap
    for(diff_type i = 1; i < N; ++i){
        diff_type n = i;
        while(n > 0){
            const auto parent_idx = (n - 1)/2;
            swap_if(comp, *(first + parent_idx), *(first + n));
            n = parent_idx;
        }
    }
    // down heap
    for(diff_type end = N - 1; end > 0; --end){
        std::swap(*(first), *(first + end));
        diff_type n = 0;
        diff_type child = 0;
        while((child = n * 2 + 1) < end){
            if(child + 1 < end && comp(*(first + child), *(first + child + 1))) ++child;
            swap_if(comp, *(first + n), *(first + child));
            n = child;
        }
    }
}

template<class RandomAccessIterator>
void heap_sort(RandomAccessIterator first, RandomAccessIterator last){
    heap_sort(first, last, std::less<typename RandomAccessIterator::value_type>{});
}

template<class RandomAccessIterator, class Compare>
void intro_sort(RandomAccessIterator first, RandomAccessIterator last, Compare comp){
    using value_type = typename RandomAccessIterator::value_type;
    using diff_type = typename RandomAccessIterator::difference_type;
    const diff_type N = std::distance(first, last);

    if(N <= 8){
        return heap_sort(first, last, comp);
    }
    const value_type pivot = *(first + N/2);
    auto first_iter = first;
    auto last_iter = last-1;
    while(true){
        while(comp(*first_iter, pivot)) ++first_iter;
        while(comp(pivot, *last_iter)) --last_iter;
        if(first_iter >= last_iter) break;
        std::swap(*first_iter, *last_iter);
        ++first_iter;
        --last_iter;
    }
    intro_sort(first, first_iter, comp);
    intro_sort(first_iter, last, comp);
}

template<class RandomAccessIterator>
void intro_sort(RandomAccessIterator first, RandomAccessIterator last){
    intro_sort(first, last, std::less<typename RandomAccessIterator::value_type>{});
}


template<class RandomAccessIterator, class Compare>
void parallel_intro_sort_impl(task_queue_service* task_queue, RandomAccessIterator first, RandomAccessIterator last, Compare& comp){
    using value_type = typename RandomAccessIterator::value_type;
    using diff_type = typename RandomAccessIterator::difference_type;
    const diff_type N = std::distance(first, last);

    if(N <= 16){
        return heap_sort(first, last, comp);
    }
    const value_type pivot = *(first + N/2);
    auto first_iter = first;
    auto last_iter = last-1;
    while(true){
        while(comp(*first_iter, pivot)) ++first_iter;
        while(comp(pivot, *last_iter)) --last_iter;
        if(first_iter >= last_iter) break;
        std::swap(*first_iter, *last_iter);
        ++first_iter;
        --last_iter;
    }
    // 1us for lock&unlock
    if(std::distance(first, first_iter) > 128 && task_queue->task_count_in_queue() < task_queue->thread_pool_count()*2){
        task_queue->post([task_queue, first, first_iter, &comp]{parallel_intro_sort_impl(task_queue, first, first_iter, comp);});
        task_queue->wakeup_all();
    }
    else{
        parallel_intro_sort_impl(task_queue, first, first_iter, comp);
    }
    if(std::distance(first_iter, last) > 128 && task_queue->task_count_in_queue() < task_queue->thread_pool_count()*2){
        task_queue->post([task_queue, first_iter, last, &comp]{parallel_intro_sort_impl(task_queue, first_iter, last, comp);});
        task_queue->wakeup_all();
    }
    else{
        parallel_intro_sort_impl(task_queue, first_iter, last, comp);
    }
}

template<class RandomAccessIterator, class Compare>
void parallel_intro_sort(RandomAccessIterator first, RandomAccessIterator last, Compare comp){
    if(std::distance(first, last) > 1024*128){
        task_queue_service task_queue(12);
        task_queue.post([task_queue_ptr = &task_queue, first, last, &comp](){parallel_intro_sort_impl(task_queue_ptr, first, last, comp);});
        task_queue.wakeup_all();
        task_queue.wait();
    }else{
        intro_sort(first, last, comp);
    }
}

template<class RandomAccessIterator>
void parallel_intro_sort(RandomAccessIterator first, RandomAccessIterator last){
    parallel_intro_sort(first, last, std::less<typename RandomAccessIterator::value_type>{});
}

template<typename RandomAccessIterator, typename Compare>
void bitonic_sort(RandomAccessIterator first, RandomAccessIterator last, Compare comp){
    const auto N = std::distance(first, last);
    for(int stage = 0; (2 << (stage-1)) < N; ++stage){
        for(int depth = stage; depth >= 0; --depth){
            #pragma omp parallel for
            for(std::size_t i = 0; i < N/2; ++i){
                const std::size_t block = i/(1<<depth);
                const std::size_t j = i % (1<<depth);
                if((block>>(stage - depth))%2 == 0){
                    swap_if(comp, *(first + block*(2 << depth) + j + (1<<depth)), *(first + block*(2 << depth) + j));
                }
                else{
                    swap_if(comp, *(first + block*(2 << depth) + j), *(first + block*(2 << depth) + j + (1<<depth)));
                }
            }
        }
    }
}

template<typename RandomAccessIterator>
void bitonic_sort(RandomAccessIterator first, RandomAccessIterator last){
    bitonic_sort(first, last, std::less<typename RandomAccessIterator::value_type>{});
}

