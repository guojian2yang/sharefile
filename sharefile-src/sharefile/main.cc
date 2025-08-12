#include <iostream>
#include <signal.h>


#include "muduo/net/TcpServer.h"
#include  "muduo/net/TcpConnection.h"
#include "muduo/base/ThreadPool.h"

#include "muduo/net/EventLoop.h"  //EventLoop
#include "muduo/base/Logging.h" // Logger日志头文件
#include "http_parser_wrapper.h"
#include "http_conn.h"
#include "config_file_reader.h"
#include "db_pool.h"
#include "cache_pool.h"
#include "api_upload.h"
#include "api_common.h"
#include "short_rpc.h"
#include "shorturl_web_service.h"

using namespace muduo;
using namespace muduo::net;
using namespace std;

std::map<uint32_t, CHttpConnPtr> s_http_map;

class HttpServer
{
public:
    //构造函数 loop主线程的EventLoop， addr封装ip，port, name服务名字，num_event_loops多少个subReactor
    HttpServer(EventLoop *loop, const InetAddress &addr, const std::string &name,  int num_event_loops
                ,int num_threads)
    :loop_(loop)
    , server_(loop, addr,name)
    , num_threads_(num_threads)
    {
        server_.setConnectionCallback(std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
        server_.setMessageCallback(
            std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        server_.setWriteCompleteCallback(std::bind(&HttpServer::onWriteComplete, this, std::placeholders::_1));
   
        server_.setThreadNum(num_event_loops);
    }
    void start() {
        if(num_threads_ != 0)
            thread_pool_.start(num_threads_);
        server_.start();
    }
private:
    void onConnection(const TcpConnectionPtr &conn)  {
        if (conn->connected())
        {
            LOG_INFO <<  "onConnection  new conn" << conn.get();
            uint32_t uuid = conn_uuid_generator_++;
            conn->setContext(uuid);
            CHttpConnPtr http_conn = std::make_shared<CHttpConn>(conn);
         
            std::lock_guard<std::mutex> ulock(mtx_); //自动释放
            s_http_map.insert({ uuid, http_conn});
         
        } else {
            LOG_INFO <<  "onConnection  dis conn" << conn.get();
            uint32_t uuid = std::any_cast<uint32_t>(conn->getContext());
            std::lock_guard<std::mutex> ulock(mtx_); //自动释放
            s_http_map.erase(uuid);
        }
    }

    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time) {
        LOG_INFO <<  "onMessage " << conn.get();
        uint32_t uuid = std::any_cast<uint32_t>(conn->getContext());
        mtx_.lock();  
        CHttpConnPtr &http_conn = s_http_map[uuid];
        mtx_.unlock();
         //处理 相关业务
        if(num_threads_ != 0)  //开启了线程池
            thread_pool_.run(std::bind(&CHttpConn::OnRead, http_conn, buf)); //给到业务线程处理
        else {  //没有开启线程池
            http_conn->OnRead(buf);  // 直接在io线程处理
        }   
       
    }

    void onWriteComplete(const TcpConnectionPtr& conn) {
        LOG_INFO <<  "onWriteComplete " << conn.get();
    }


    TcpServer server_;    // 每个连接的回调数据 新的连接/断开连接  收到数据  发送数据完成   
    EventLoop *loop_ = nullptr; //这个是主线程的EventLoop
    std::atomic<uint32_t> conn_uuid_generator_ = 0;  //这里是用于http请求，不会一直保持链接
    std::mutex mtx_;

    //线程池
    ThreadPool thread_pool_;
    const int num_threads_ = 0;
};


// 从 Redis 批量拉取排行榜信息
std::string fetchBatchRankingListFromRedis(int start, int count) {
//获取redis连接
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("ranking_list");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    int total = 0;

    int ret = handleGetSharefilesCount(total);

    string str_json;
    char filename[512] = {0};
    int file_count = 0;
    RVALUES value = NULL;
    int end;
    Json::Value root;
    Json::Value files;
    value = (RVALUES)calloc(count, VALUES_ID_SIZE); //堆区请求空间
    if (value == NULL) {
        ret = -1;
        goto END;
    }
     end = start + count - 1; //加载资源的结束位置  start = 0; count = 10; end = 9;  [0, 9]
    //降序获取有序集合的元素   file_count获取实际返回的个数
    ret = cache_conn->ZsetZrevrange(FILE_PUBLIC_ZSET, start, end, value, file_count);
    if (ret != 0) {
        LOG_ERROR << "ZsetZrevrange 操作失败";
        ret = -1;
        goto END;
    }
    
    //遍历元素个数
    for (int i = 0; i < file_count; ++i) {
        ret = cache_conn->Hget(FILE_NAME_HASH, value[i], filename);
        if (ret != 0) {
            LOG_ERROR << "hget  操作失败";
            ret = -1;
            goto END;
        }
        Json::Value file;
        file["filename"] = filename; 
        // zset 根据 member名字获取score
        int score = cache_conn->ZsetGetScore(FILE_PUBLIC_ZSET, value[i]);
        if (score == -1) {
            LOG_ERROR << "ZsetGetScore  操作失败";
            ret = -1;
            goto END;
        }
        file["pv"] = score;
        files[i] = file;
    }
    ret = 0;
    // json
END:
    if(ret == 0) {
        root["code"] = 0;
        root["total"] = total;
        root["count"]  = file_count;
        root["files"] = files;
    } else {
         root["code"] = 1;
    }
    str_json = root.toStyledString();
    return str_json;
}

    // 定时拉取排行榜信息并异步更新到 KV 存储
void startPeriodicRankingFetch(std::chrono::seconds interval) {
    std::thread([interval]() {
        while (true) {
            std::this_thread::sleep_for(interval);
            // 拉取前五页
            // cout << "startPeriodicRankingFetch" << endl;
            for (int start = 0; start <= 50; start = start + 10) {
                int count = 10;
                std::string ranking_list = fetchBatchRankingListFromRedis(start, 10);
                std::string kv_key = "ranking_list_" + std::to_string(start) + "_" + std::to_string(10);
                // 异步设置 KV 存储
                KVStore::getInstance().asyncSet(kv_key, ranking_list, std::chrono::seconds(60));
            }
        }
    }).detach();
}

// 定时持久化任务
void startPeriodicPersistence(std::chrono::seconds interval, const std::string& filename) {
    std::thread([interval, filename]() {
        while (true) {
            std::this_thread::sleep_for(interval);
            KVStore::getInstance().persistToFile(filename);
        }
    }).detach();
}

void preheatCache() {
    // 假设拉取前 5 页的排行榜数据，每页 10 条记录
    cout << "preheatCache" << endl;
    for (int start = 0; start <= 50; start = start + 10) {
        int count = 10;
        std::string ranking_list = fetchBatchRankingListFromRedis(start, 10);
        std::string kv_key = "ranking_list_" + std::to_string(start) + "_" + std::to_string(10);
        // 异步设置 KV 存储
        KVStore::getInstance().asyncSet(kv_key, ranking_list, std::chrono::seconds(60));
    }
}

int main(int argc, char *argv[])
{

    std::cout  << argv[0] << "[conf ] "<< std::endl;
     

     // 默认情况下，往一个读端关闭的管道或socket连接中写数据将引发SIGPIPE信号。我们需要在代码中捕获并处理该信号，
    // 或者至少忽略它，因为程序接收到SIGPIPE信号的默认行为是结束进程，而我们绝对不希望因为错误的写操作而导致程序退出。
    // SIG_IGN 忽略信号的处理程序
    signal(SIGPIPE, SIG_IGN); //忽略SIGPIPE信号
    int ret = 0;
    char *str_tc_http_server_conf = NULL;
    if(argc > 1) {
        str_tc_http_server_conf = argv[1];  // 指向配置文件路径
    } else {
        str_tc_http_server_conf = (char *)"tc_http_server.conf";
    }
     std::cout << "conf file path: " <<  str_tc_http_server_conf << std::endl;
     // 读取配置文件
    CConfigFileReader config_file(str_tc_http_server_conf);     //读取配置文件

    // Logger::setLogLevel(Logger::ERROR);     //性能测试时减少打印
    //日志设置级别
    char *str_log_level =  config_file.GetConfigName("log_level");  
    Logger::LogLevel log_level = static_cast<Logger::LogLevel>(atoi(str_log_level));
    Logger::setLogLevel(log_level);


    char *dfs_path_client = config_file.GetConfigName("dfs_path_client"); // /etc/fdfs/client.conf
    char *storage_web_server_ip = config_file.GetConfigName("storage_web_server_ip"); //后续可以配置域名
    char *storage_web_server_port = config_file.GetConfigName("storage_web_server_port");

    
     // 初始化mysql、redis连接池，内部也会读取读取配置文件tc_http_server.conf
    CacheManager::SetConfPath(str_tc_http_server_conf); //设置配置文件路径
    CacheManager *cache_manager = CacheManager::getInstance();
    if (!cache_manager) {
        LOG_ERROR <<"CacheManager init failed";
        return -1;
    }

    // 将配置文件的参数传递给对应模块
     ApiUploadInit(dfs_path_client, storage_web_server_ip, storage_web_server_port, "", "");

    CDBManager::SetConfPath(str_tc_http_server_conf);   //设置配置文件路径
    CDBManager *db_manager = CDBManager::getInstance();
    if (!db_manager) {
        LOG_ERROR <<"DBManager init failed";
        return -1;
    }

    std::cout << "hello 图床 ../../bin/tc_http_srv\n";
    uint16_t http_bind_port = 8081;
    const char *http_bind_ip = "0.0.0.0";
    char *str_num_event_loops = config_file.GetConfigName("num_event_loops");  
    int num_event_loops = atoi(str_num_event_loops);
    char *str_num_threads = config_file.GetConfigName("num_threads");  
    int num_threads = atoi(str_num_threads);

    char *str_timeout_ms = config_file.GetConfigName("timeout_ms");  
    int timeout_ms = atoi(str_timeout_ms);
    std::cout << "timeout_ms: " << timeout_ms << std::endl;



    // 设置 KV 存储的最大容量
    KVStore::getInstance().setMaxCapacity(200);
    // 启动定时清理过期 key 的任务
    KVStore::getInstance().startExpirationCleaner(std::chrono::minutes(1));
    // 执行缓存预热
    preheatCache();
    // 启动定时拉取排行榜信息的任务
    startPeriodicRankingFetch(std::chrono::seconds(10));
    // 从文件加载持久化数据
    KVStore::getInstance().loadFromFile("kv_store_data.txt");
    // 启动定时持久化任务
    startPeriodicPersistence(std::chrono::seconds(60), "kv_store_data.txt");

    std::thread rpcThread(startRpcServer);
    rpcThread.detach();

    std::thread shortUrlThread(RunShortUrlWebService);
    shortUrlThread.detach();

    EventLoop loop;     //主循环
    InetAddress addr(http_bind_ip, http_bind_port);     // 注意别搞错位置了
    LOG_INFO << "port: " << http_bind_port;
    HttpServer server(&loop, addr, "HttpServer", num_event_loops, num_threads);
    server.start();
    loop.loop(timeout_ms); //1000ms
    return 0;
}