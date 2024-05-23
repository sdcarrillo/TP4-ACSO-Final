/**
 * File: thread-pool.cc
 * --------------------
 * Presents the implementation of the ThreadPool class.
 */


#include "thread-pool.h"
#include <iostream>
using namespace std;

ThreadPool::ThreadPool(size_t numThreads) :
 wts(numThreads), 
 semaphore_workers(numThreads),
 done(false),
 my_workers_info(numThreads), 
 workers_working(0)

 {

    for (size_t i = 0; i < numThreads; i++) {
        wts[i] = thread([this, i] { worker(i); });
    }
    

    dt = thread([this] { dispatcher(); });  

}

void ThreadPool::schedule(const function<void(void)>& thunk) {
    {
        unique_lock<mutex> lock(queue_mutex); 
        queue_thunks.push(thunk); 

    }
    cv.notify_all();
}

void ThreadPool::wait() {
    unique_lock<mutex> lock(queue_mutex);
    cv.wait(lock, [this] { return queue_thunks.empty() && workers_working == 0; }); 
}

void ThreadPool::worker(size_t id) {

    while(true){
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            my_workers_info[id].cv_worker.wait(lock, [this, id](){return my_workers_info[id].occupied != false || done;});
            task = my_workers_info[id].thunk;
            if(done && queue_thunks.empty()){
                return;
            }
        }

        task();

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            my_workers_info[id].occupied = false;
            workers_working--;
            semaphore_workers.signal();
            if (queue_thunks.empty() && workers_working == 0){
                cv.notify_all();
            }
        }
   }
}

void ThreadPool::dispatcher(){
    while(true){
        function<void(void)> task;

        {
            unique_lock<mutex> lock(queue_mutex);
            cv.wait(lock, [this] { return !queue_thunks.empty() || done; });
            if(done && queue_thunks.empty()){
                return;
            }
            task = queue_thunks.front();
            queue_thunks.pop();
        }
        semaphore_workers.wait(); 
        {
            unique_lock<mutex> lock(queue_mutex);
            for(size_t id = 0; id < wts.size(); id++){
                if(!my_workers_info[id].occupied){
                    my_workers_info[id].thunk = task;
                    my_workers_info[id].occupied = true;
                    my_workers_info[id].cv_worker.notify_all();
                    workers_working++;
                    break;
                }
            }    
            
        }
    }
}

ThreadPool::~ThreadPool() {

    wait();

    {
        unique_lock<mutex> lock(queue_mutex);
        done = true; 
    }
    cv.notify_all();

    for (size_t i = 0; i < my_workers_info.size(); i++) {
        my_workers_info[i].cv_worker.notify_all();
        wts[i].join();
    }
    
    dt.join();
}