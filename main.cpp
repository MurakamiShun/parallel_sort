#define _GLIBCXX_PARALLEL
#include <iostream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <execution>
#include "parallel_sort.hpp"
#include "thread_pool.hpp"
#include <chrono>

int main(){
    std::vector<int> vec(1024*1024*512);

    // fill vector with random numbers
    std::srand(unsigned(std::time(nullptr)));
    std::generate(vec.begin(), vec.end(), [](){ return std::rand() % 100; });

    std::vector<int> heap_vec = vec;

    auto start = std::chrono::system_clock::now();
    parallel_intro_sort(heap_vec.begin(), heap_vec.end());
    std::cout << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start).count() << "us" << std::endl;
    
    // sort vector
    start = std::chrono::system_clock::now();
    std::sort(vec.begin(), vec.end());
    std::cout << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start).count() << "us" << std::endl;

    std::cout << (vec == heap_vec) << std::endl;
}
