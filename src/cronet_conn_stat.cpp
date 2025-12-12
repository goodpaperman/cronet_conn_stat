#include <cronet/cronet_c.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

std::map<Cronet_UrlResponseInfoPtr, Cronet_UrlRequestPtr> rr_map; 

// 回调函数签名修正
void on_redirect_received(Cronet_UrlRequestCallback* callback,
                         Cronet_UrlRequest* request,
                         Cronet_UrlResponseInfo* info,
                         const char* new_location) {
    std::cout << "Redirect to: " << new_location << std::endl;
    Cronet_UrlRequest_FollowRedirect(request);
}

void on_response_started(Cronet_UrlRequestCallback* callback,
                        Cronet_UrlRequest* request,
                        Cronet_UrlResponseInfo* info) {
    std::cout << "Response started" << std::endl;
    Cronet_Buffer* buffer = Cronet_Buffer_Create();
    Cronet_Buffer_InitWithAlloc(buffer, 4096); // 4KB缓冲区
    Cronet_UrlRequest_Read(request, buffer);
}

void on_read_completed(Cronet_UrlRequestCallback* callback,
                      Cronet_UrlRequest* request,
                      Cronet_UrlResponseInfo* info,
                      Cronet_Buffer* buffer,
                      uint64_t bytes_read) {
    // 处理数据
    if (bytes_read > 0) {
        const char* data = static_cast<const char*>(Cronet_Buffer_GetData(buffer));
        std::cout << "Read " << bytes_read << " bytes" << std::endl;
        std::cout << data << std::endl; 
    }

    // 释放当前buffer
    Cronet_Buffer_Destroy(buffer);

    // 继续读取（如果还有数据且未完成）
    if (bytes_read > 0) {
        Cronet_Buffer* new_buffer = Cronet_Buffer_Create();
        Cronet_Buffer_InitWithAlloc(new_buffer, 4096);
        Cronet_UrlRequest_Read(request, new_buffer);
    } else {
        std::cout << "Read completed" << std::endl;
    }
}

void on_succeeded(Cronet_UrlRequestCallback* callback,
                 Cronet_UrlRequest* request,
                 Cronet_UrlResponseInfo* info) {
    std::cout << "Request succeeded" << std::endl;
}

void on_failed(Cronet_UrlRequestCallback* callback,
              Cronet_UrlRequest* request,
              Cronet_UrlResponseInfo* info,
              Cronet_Error* error) {
    std::cout << "Request failed" << std::endl;
}

void on_canceled(Cronet_UrlRequestCallback* callback,
                Cronet_UrlRequest* request,
                Cronet_UrlResponseInfo* info) {
    std::cout << "Request cancelled" << std::endl;
}

void on_request_finished(Cronet_ClientContext obj, int64_t connect) 
{
    std::cout << "request finish, connect elapse " << connect << " ms" << std::endl; 
}

void on_request_finished_listener(
    Cronet_RequestFinishedInfoListenerPtr self,
    Cronet_RequestFinishedInfoPtr request_info,
    Cronet_UrlResponseInfoPtr response_info,
    Cronet_ErrorPtr error)
{
    int64_t connect = 0;
    Cronet_MetricsPtr metrics = Cronet_RequestFinishedInfo_metrics_get(request_info);
    if (metrics) {
        Cronet_DateTimePtr start = Cronet_Metrics_connect_start_get(metrics);
        Cronet_DateTimePtr end = Cronet_Metrics_connect_end_get(metrics);
        if (start && end) {
            int64_t start_ms = Cronet_DateTime_value_get(start);
            int64_t end_ms = Cronet_DateTime_value_get(end);
            connect = (start_ms > 0 && end_ms > 0) ? (end_ms - start_ms) : 0;
        }
    }

    auto it = rr_map.find(response_info);
    if (it != rr_map.end()) {
        Cronet_UrlRequestPtr req = it->second;
        Cronet_ClientContext obj = Cronet_UrlRequest_GetClientContext(req);
        on_request_finished(obj, connect);
    }
}

std::atomic<bool> request_completed{false};

// 任务队列和线程管理
class ExecutorThread {
private:
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::thread worker_thread_;
    std::atomic<bool> stop_{false};

public:
    ExecutorThread() {
        worker_thread_ = std::thread([this]() {
            this->run();
        });
    }

    ~ExecutorThread() {
        stop_ = true;
        condition_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    void postTask(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            task_queue_.push(std::move(task));
        }
        condition_.notify_one();
    }

private:
    void run() {
        while (!stop_) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this]() {
                    return stop_ || !task_queue_.empty();
                });

                if (stop_ && task_queue_.empty()) {
                    return;
                }

                task = std::move(task_queue_.front());
                task_queue_.pop();
            }

            // 执行任务
            if (task) {
                try {
                    task();
                } catch (const std::exception& e) {
                    std::cerr << "Executor task error: " << e.what() << std::endl;
                }
            }
        }
    }
};

void custom_executor_func(Cronet_Executor *executor, Cronet_Runnable *cronet_task) {
    ExecutorThread* et = (ExecutorThread*)Cronet_Executor_GetClientContext(executor); 
    if (!et) {
        std::cerr << "Executor not initialized!" << std::endl;
        return;
    }

    // 将Cronet的任务包装成std::function
    if (cronet_task) {
        et->postTask([cronet_task]() {
            // 执行Cronet任务
            Cronet_Runnable_Run(cronet_task);
        });
    }
}

int main() {
    // 1. 创建引擎
    Cronet_EnginePtr engine = Cronet_Engine_Create();
    Cronet_EngineParamsPtr params = Cronet_EngineParams_Create();
    Cronet_Engine_StartWithParams(engine, params);
    
    // 2. 创建回调
    Cronet_UrlRequestCallbackPtr callback = Cronet_UrlRequestCallback_CreateWith(
        on_redirect_received,
        on_response_started,
        on_read_completed,
        on_succeeded,
        on_failed,
        on_canceled
    );
    
    // 3. 配置请求
    Cronet_UrlRequestParamsPtr req_params = Cronet_UrlRequestParams_Create();
    Cronet_UrlRequestParams_http_method_set(req_params, "GET");
    
    // 添加请求头
    Cronet_HttpHeaderPtr header = Cronet_HttpHeader_Create();
    Cronet_HttpHeader_name_set(header, "User-Agent");
    Cronet_HttpHeader_value_set(header, "Cronet-C-Client");
    Cronet_UrlRequestParams_request_headers_add(req_params, header);
    
    // 4. 创建执行器
    auto executor_thread = new ExecutorThread; 
    Cronet_ExecutorPtr executor = Cronet_Executor_CreateWith(custom_executor_func);
    Cronet_Executor_SetClientContext(executor, executor_thread); 
    
    // 5. 创建监听器
    Cronet_RequestFinishedInfoListenerPtr listener = Cronet_RequestFinishedInfoListener_CreateWith(on_request_finished_listener);
    if (listener) {
        Cronet_Engine_AddRequestFinishedListener(engine, listener, executor);
        std::cout << "request finished listener registered" << std::endl;
    }
    else {
        std::cout << "setup request finished listener failed, no connection statistic provided" << std::endl;
    }

    // 6. 创建并启动请求
    Cronet_UrlRequestPtr request = Cronet_UrlRequest_Create();
    Cronet_UrlRequest_InitWithParams(request, engine, 
                                     "http://httpbin.org/json", 
                                     req_params, callback, executor);
    Cronet_UrlRequest_Start(request);
    // std::cout << "start request" << std::endl;
    
    // 7. 等待请求完成（简化演示，实际应用需事件循环）
    std::this_thread::sleep_for(std::chrono::seconds(15));
    
    // std::cout << "request done" << std::endl;
    // 8. 清理资源
    Cronet_UrlRequest_Destroy(request);
    Cronet_HttpHeader_Destroy(header);
    Cronet_UrlRequestParams_Destroy(req_params);
    if (listener) {
        Cronet_Engine_RemoveRequestFinishedListener(engine, listener);
        Cronet_RequestFinishedInfoListener_Destroy(listener);
    }
    delete executor_thread; 
    Cronet_Executor_Destroy(executor);
    Cronet_UrlRequestCallback_Destroy(callback);
    Cronet_EngineParams_Destroy(params);
    Cronet_Engine_Destroy(engine);
    
    return 0;
}
