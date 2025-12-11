#include <stdio.h>
#include <string.h>
#include <cronet/cronet.h>
#include <map>

std::map<Cronet_UrlResponseInfoPtr, Cronet_UrlRequestPtr> rr_map; 

// 回调上下文
typedef struct {
    char response[8192];
    int redirect_count;
    int completed;
} CallbackData;

// 回调：重定向
void on_redirect_received(Cronet_UrlRequestPtr request,
                         Cronet_UrlResponseInfoPtr info,
                         const char* new_location_url,
                         void* user_data) {
    CallbackData* data = (CallbackData*)user_data;
    data->redirect_count++;
    rr_map[info] = request; 
    
    printf("Redirect #%d to: %s\n", data->redirect_count, new_location_url);
    
    // 跟随重定向
    Cronet_UrlRequest_FollowRedirect(request);
}

// 回调：响应开始
void on_response_started(Cronet_UrlRequestPtr request,
                        Cronet_UrlResponseInfoPtr info,
                        void* user_data) {
    CallbackData* data = (CallbackData*)user_data;
    rr_map[info] = request; 
    
    int32_t status = Cronet_UrlResponseInfo_http_status_code_get(info);
    printf("Response started. Status: %d\n", status);
    
    Cronet_BufferPtr buffer = Cronet_Buffer_Create();
    Cronet_Buffer_InitWithAlloc(buffer, 2048);
    Cronet_UrlRequest_Read(request, buffer);
}

// 回调：读取完成
void on_read_completed(Cronet_UrlRequestPtr request,
                      Cronet_UrlResponseInfoPtr info,
                      Cronet_BufferPtr buffer,
                      uint64_t bytes_read,
                      void* user_data) {
    CallbackData* data = (CallbackData*)user_data;
    rr_map[info] = request; 
    
    if (bytes_read > 0) {
        uint8_t* buf_data = Cronet_Buffer_GetData(buffer);
        strncat(data->response, (char*)buf_data, bytes_read);
        
        Cronet_Buffer_Clear(buffer);
        Cronet_UrlRequest_Read(request, buffer);
    }
}

// 回调：请求成功
void on_succeeded(Cronet_UrlRequestPtr request,
                 Cronet_UrlResponseInfoPtr info,
                 void* user_data) {
    CallbackData* data = (CallbackData*)user_data;
    rr_map[info] = request; 
    
    printf("\nFinal URL: %s\n", 
           Cronet_UrlResponseInfo_url_get(info));
    printf("Redirects: %d\n", data->redirect_count);
    printf("Response length: %zu bytes\n\n", strlen(data->response));
    
    data->completed = 1;
}

// 回调：请求失败
void on_failed(Cronet_UrlRequestPtr request,
              Cronet_UrlResponseInfoPtr info,
              Cronet_ErrorPtr error,
              void* user_data) {
    CallbackData* data = (CallbackData*)user_data;
    rr_map[info] = request; 
    printf("Request failed: %s\n", Cronet_Error_message_get(error));
    data->completed = 1;
}

extern on_request_finished(Cronet_ClientContext obj, int64_t connect);
void on_request_finished_listener(
    Cronet_RequestFinishedInfoListenerPtr self,
    Cronet_RequestFinishedInfoPtr request_info,
    Cronet_UrlResponseInfoPtr response_info,
    Cronet_ErrorPtr error)
{
    int64_t connect = 0;
    Cronet_MetricsPtr metrics = Cronet_RequestFinishedInfo_metrics(request_info);
    if (metrics) {
        Cronet_DateTimePtr start = Cronet_Metrics_connect_start(metrics);
        Cronet_DateTimePtr end = Cronet_Metrics_connect_end(metrics);
        if (start && end) {
            int64_t start_ms = Cronet::instance()->Cronet_DateTime_value_get(start);
            int64_t end_ms = Cronet::instance()->Cronet_DateTime_value_get(end);
            connect = (start_ms > 0 && end_ms > 0) ? (end_ms - start_ms) : 0;
        }
    }

    auto it = rr_map.find(response_info);
    if (it != rr_map.end()) {
        Cronet_UrlRequestPtr req = it->second;
        Cronet_ClientContext obj = Cronet_UrlRequest_GetClientContext(req);
        if (obj) {
            on_request_finished(obj, connect);
        }
    }
}

int main() {
    // 1. 创建引擎
    Cronet_EnginePtr engine = Cronet_Engine_Create();
    Cronet_EngineParamsPtr params = Cronet_EngineParams_Create();
    Cronet_Engine_StartWithParams(engine, params);
    
    // 2. 初始化回调数据
    CallbackData cb_data = {0};
    
    // 3. 创建回调
    Cronet_UrlRequestCallbackPtr callback = Cronet_UrlRequestCallback_CreateWith(
        on_redirect_received,  // 重定向回调
        on_response_started,
        on_read_completed,
        on_succeeded,
        on_failed,
        NULL
    );
    Cronet_UrlRequestCallback_SetClientContext(callback, &cb_data);
    
    // 4. 配置请求
    Cronet_UrlRequestParamsPtr req_params = Cronet_UrlRequestParams_Create();
    Cronet_UrlRequestParams_http_method_set(req_params, "GET");
    
    // 添加请求头
    Cronet_HttpHeaderPtr header = Cronet_HttpHeader_Create();
    Cronet_HttpHeader_name_set(header, "User-Agent");
    Cronet_HttpHeader_value_set(header, "Cronet-C-Client");
    Cronet_UrlRequestParams_request_headers_add(req_params, header);
    
    // 5. 创建执行器
    Cronet_ExecutorPtr executor = Cronet_Executor_CreateWith(NULL);
    
    // 6. 创建监听器
    Cronet_RequestFinishedInfoListenerPtr listener = Cronet_RequestFinishedInfoListener_CreateWith(on_request_finished_listener);
    if (listener) {
        Cronet_Engine_AddRequestFinishedListener(engine, listener, executor);
        printf("request finished listener registered\n");
    }
    else {
        printf("setup request finished listener failed, no connection statistic provided\n");
    }
    
    // 7. 创建并启动请求
    Cronet_UrlRequestPtr request = Cronet_UrlRequest_Create();
    Cronet_UrlRequest_InitWithParams(request, engine, 
                                     "http://httpbin.org/redirect/3",  // 重定向3次
                                     req_params, callback, executor);
    Cronet_UrlRequest_Start(request);
    
    // 8. 等待请求完成
    while (!cb_data.completed) {
        usleep(100000);
    }
    
    // 9. 清理资源
    Cronet_UrlRequest_Destroy(request);
    Cronet_HttpHeader_Destroy(header);
    Cronet_UrlRequestParams_Destroy(req_params);
    if (listener) {
        Cronet_Engine_RemoveRequestFinishedListener(engine, listener);
        Cronet_RequestFinishedInfoListener_Destroy(listener);
    }
    Cronet_Executor_Destroy(executor);
    Cronet_UrlRequestCallback_Destroy(callback);
    Cronet_EngineParams_Destroy(params);
    Cronet_Engine_Destroy(engine);
    
    return 0;
}
