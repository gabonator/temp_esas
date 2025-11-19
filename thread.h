//
//  thread.h
//  eset1
//
//  Created by Gabriel Valky on 19/11/2025.
//

#include <csetjmp>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <future>

class ThreadBase {
public:
    virtual int run(uint64_t tid) = 0;
    virtual void terminate() = 0;
};

class CThread : public std::enable_shared_from_this<CThread> {
    enum {
        timeoutMs = 3000,
        maxStack = 8*1024
    };
    
public:
    std::shared_ptr<ThreadBase> config;
    static thread_local uint64_t currentThreadId;
    bool shouldStop{false};

private:
    uint64_t threadId;
    std::thread nativeThread;
    std::chrono::milliseconds timeout{timeoutMs}; // 10 second default timeout
    
    static std::atomic<uint64_t> threadCounter;
    static std::mutex gThreadRegistryMutex;
    static std::unordered_map<uint64_t, std::shared_ptr<CThread>> gThreadRegistry;
    static std::mutex syncObjectsMutex;
    static std::unordered_map<uint64_t, std::unique_ptr<std::mutex>> mutexMap;

    void registerThread() {
        std::lock_guard<std::mutex> lock(gThreadRegistryMutex);
        printf("[GLOBAL] register threadId %lld\n", threadId);
        gThreadRegistry[threadId] = shared_from_this();
    }
    
    void unregisterThread() {
        std::lock_guard<std::mutex> lock(gThreadRegistryMutex);
        printf("[GLOBAL] unregister threadId %lld\n", threadId);
        gThreadRegistry.erase(threadId);
    }

public:
    explicit CThread(const std::shared_ptr<ThreadBase>& cfg) : config(cfg) {
        threadId = threadCounter.fetch_add(1, std::memory_order_relaxed);
    }
    
    ~CThread() {
        if (nativeThread.joinable()) {
            nativeThread.join();
        }
    }
    
    // Prevent copying
    CThread(const CThread&) = delete;
    CThread& operator=(const CThread&) = delete;
        
    uint64_t run() {
        printf("[Thread %lld] Start...\n", threadId);
        assert(!nativeThread.joinable());
        
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, maxStack);
        
        registerThread();
        nativeThread = std::thread([this]() {
            currentThreadId = threadId;
            
            // Execute with timeout using async
            auto future = std::async(std::launch::async, [this]() -> int {
                // Set for async thread too, two threads share the same CThread obj
                // Ugly, but std::async doesn't allow setting stack size limit
                currentThreadId = threadId;
                return config->run(threadId);
            });
            
            auto status = future.wait_for(timeout);
            if (status == std::future_status::timeout) {
                printf("[Thread %lld] Execution timeout\n", threadId);
                shouldStop = true;
                auto status = future.wait_for(timeout);
                if (status == std::future_status::timeout) {
                    printf("[Thread %lld] Not responding, terminating\n", threadId);
                    exit(1);
                }
            } else {
                int result = future.get();
                if (result == 1) {
                    printf("[Thread %lld] Halted via terminate\n", threadId);
                } else if (result == 0) {
                    printf("[Thread %lld] Completed normally\n", threadId);
                }
            }
            
            unregisterThread();
        });
        
        pthread_attr_destroy(&attr);
        return threadId;
    }
    
    // Wait for thread to complete
    void join() {
        assert(nativeThread.joinable());
        printf("[Thread %lld] Joining...\n", threadId);
        nativeThread.join();
        printf("[Thread %lld] Join done...\n", threadId);
    }

    static std::shared_ptr<CThread> getCurrent()
    {
        return getById(currentThreadId);
    }
    
    static std::shared_ptr<CThread> getById(uint64_t tid)
    {
        std::lock_guard<std::mutex> lock(gThreadRegistryMutex);
        auto it = gThreadRegistry.find(tid);
        if (it != gThreadRegistry.end()) {
            return it->second;
        }
        assert(0);
        return {};
    }

    void lock(uint64_t lockId) {
        printf("[Thread %lld] Locking object %d\n", threadId, lockId);
        std::unique_lock<std::mutex> registryLock(syncObjectsMutex);
        
        if (mutexMap.find(lockId) == mutexMap.end()) {
            mutexMap[lockId] = std::make_unique<std::mutex>();
        }
        
        auto* mtx = mutexMap[lockId].get();
        registryLock.unlock();
        
        mtx->lock();
        printf("[Thread %lld] Locked object %d\n", threadId, lockId);
    }
    
    void unlock(uint64_t lockId) {
        std::lock_guard<std::mutex> registryLock(syncObjectsMutex);
        
        auto it = mutexMap.find(lockId);
        if (it != mutexMap.end()) {
            printf("[Thread %lld] Unlocking object %d\n", threadId, lockId);
            it->second->unlock();
        } else {
            printf("[Thread %lld] Warning: Unlock on non-existent lock %d\n", threadId, lockId);
        }
    }
};

// Static member initialization
thread_local uint64_t CThread::currentThreadId = 10;
std::mutex CThread::gThreadRegistryMutex;
std::unordered_map<uint64_t, std::shared_ptr<CThread>> CThread::gThreadRegistry;
std::mutex CThread::syncObjectsMutex;
std::unordered_map<uint64_t, std::unique_ptr<std::mutex>> CThread::mutexMap;
std::atomic<uint64_t> CThread::threadCounter{1};
