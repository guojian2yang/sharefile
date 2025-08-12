#ifndef _API_COMMON_H_
#define _API_COMMON_H_
#include <string>
#include <unordered_map>
#include <mutex>
#include <list>
#include <chrono>
#include <fstream>
#include <sstream>
#include <queue>
#include <condition_variable>
#include <thread>
#include <functional>
#include "cache_pool.h" // redis操作头文件
#include "db_pool.h"    //MySQL操作头文件
#include <json/json.h> // jsoncpp头文件
#include "muduo/base/Logging.h" // Logger日志头文件
 

using std::string;

#define SQL_MAX_LEN (512) // sql语句长度


#define FILE_NAME_LEN (256)    //文件名字长度
#define TEMP_BUF_MAX_LEN (512) //临时缓冲区大小
#define FILE_URL_LEN (512)     //文件所存放storage的host_name长度
#define HOST_NAME_LEN (30)     //主机ip地址长度
#define USER_NAME_LEN (128)    //用户名字长度
#define TOKEN_LEN (128)        //登陆token长度
#define MD5_LEN (256)          //文件md5长度
#define PWD_LEN (256)          //密码长度
#define TIME_STRING_LEN (25)   //时间戳长度
#define SUFFIX_LEN (8)         //后缀名长度
#define PIC_NAME_LEN (10)      //图片资源名字长度
#define PIC_URL_LEN (256)      //图片资源url名字长度

#define HTTP_RESP_OK 0
#define HTTP_RESP_FAIL 1           //
#define HTTP_RESP_USER_EXIST 2     // 用户存在
#define HTTP_RESP_DEALFILE_EXIST 3 // 别人已经分享此文件
#define HTTP_RESP_TOKEN_ERR 4      //  token验证失败
#define HTTP_RESP_FILE_EXIST 5     //个人已经存储了该文件




//redis key相关定义
#define REDIS_SERVER_IP "127.0.0.1"
#define REDIS_SERVER_PORT "6379"


extern string s_dfs_path_client;
extern string s_storage_web_server_ip;
extern string s_storage_web_server_port;
extern string s_shorturl_server_address;
extern string s_shorturl_server_access_token;

/*--------------------------------------------------------
| 共享用户文件有序集合 (ZSET)
| Key:     FILE_PUBLIC_LIST
| value:   md5文件名
| redis 语句
|   ZADD key score member 添加成员
|   ZREM key member 删除成员
|   ZREVRANGE key start stop [WITHSCORES] 降序查看
|   ZINCRBY key increment member 权重累加increment
|   ZCARD key 返回key的有序集元素个数
|   ZSCORE key member 获取某个成员的分数
|   ZREMRANGEBYRANK key start stop 删除指定范围的成员
|   zlexcount zset [member [member 判断某个成员是否存在，存在返回1，不存在返回0
`---------------------------------------------------------*/
#define FILE_PUBLIC_ZSET "FILE_PUBLIC_ZSET"

/*-------------------------------------------------------
| 文件标示和文件名对应表 (HASH)
| Key:    FILE_NAME_HASH
| field:  file_id(md5文件名)
| value:  file_name
| redis 语句
|    hset key field value
|    hget key field
`--------------------------------------------------------*/
#define FILE_NAME_HASH "FILE_NAME_HASH"

#define FILE_PUBLIC_COUNT "FILE_PUBLIC_COUNT"  // 共享文件数量
#define FILE_USER_COUNT "FILE_USER_COUNT"   // 用户文件数量 FILE_USER_COUNT+username
#define SHARE_PIC_COUNT "SHARE_PIC_COUNT"   // 用户分享图片数量 SHARE_PIC_COUNT+username


// 某些参数没有使用时用该宏定义避免报警告
#define UNUSED(expr)                                                           \
    do {                                                                       \
        (void)(expr);                                                          \
    } while (0)
//验证登陆token，成功返回0，失败-1
int VerifyToken(string &user_name, string &token);


//获取用户文件个数
int CacheSetCount(CacheConn *cache_conn, string key, int64_t count);
int CacheGetCount(CacheConn *cache_conn, string key, int64_t &count);
int CacheIncrCount(CacheConn *cache_conn, string key);
int CacheDecrCount(CacheConn *cache_conn, string key);

int handleGetSharefilesCount(int &count);

//处理数据库查询结果，结果集保存在buf，只处理一条记录，一个字段,
//如果buf为NULL，无需保存结果集，只做判断有没有此记录 返回值：
// 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
int GetResultOneCount(CDBConn *db_conn, char *sql_cmd, int &count);
int GetResultOneStatus(CDBConn *db_conn, char *sql_cmd, int &shared_status);

// 检测是否存在记录，-1 操作失败，0:没有记录， 1:有记录
int CheckwhetherHaveRecord(CDBConn *db_conn, char *sql_cmd);

int DBGetUserFilesCountByUsername(CDBConn *db_conn, string user_name, int &count);

// 解析http url中的参数
int QueryParseKeyValue(const char *query, const char *key, char *value, int *value_len_p);
//获取文件名后缀
int GetFileSuffix(const char *file_name, char *suffix);
//去掉两边空白字符
int TrimSpace(char *inbuf);
string RandomString(const int len);
string generateUUID();

// 它是一个模板函数，实现也需要放在头文件里，否则报错
template <typename... Args>
std::string FormatString(const std::string &format, Args... args) {
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...) +
                1; // Extra space for '\0'
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

int DBGetSharePictureCountByUsername(CDBConn *db_conn, string user_name, int &count);

int RemoveFileFromFastDfs(const char *fileid);


#include <iostream>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <memory>

class FileInfoLock {
 public:
  // 获取单例实例的静态方法
  static FileInfoLock& GetInstance() {
    static FileInfoLock instance;  // 线程安全的单例（C++11及以上）
    return instance;
  }

  // 尝试获取锁，支持超时退出
  bool TryLockFor(int milliseconds) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (!cv_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                      [this]() { return !locked_ || stop_; })) {
      return false;  // 超时退出
    }
    if (stop_) {
      return false;  // 主动退出
    }
    locked_ = true;
    return true;
  }

  // 释放锁
  void Unlock() {
    std::unique_lock<std::mutex> lock(mtx_);
    locked_ = false;
    cv_.notify_one();
  }

  // 主动退出，通知所有等待的线程
  void StopWaiting() {
    std::unique_lock<std::mutex> lock(mtx_);
    stop_ = true;
    cv_.notify_all();  // 通知所有等待的线程
  }

  // 重置状态，允许重新使用
  void Reset() {
    std::unique_lock<std::mutex> lock(mtx_);
    stop_ = false;
    locked_ = false;
  }

 private:
  // 私有化构造函数和拷贝构造函数
  FileInfoLock() : locked_(false), stop_(false) {}
  FileInfoLock(const FileInfoLock&) = delete;             // 禁止拷贝构造
  FileInfoLock& operator=(const FileInfoLock&) = delete;  // 禁止赋值操作

  std::mutex mtx_;
  std::condition_variable cv_;
  bool locked_;
  std::atomic<bool> stop_;  // 用于主动退出的标志位
};

// RAII 包装类，用于自动管理锁的生命周期
class ScopedFileInfoLock {
 public:
  // 构造函数：尝试获取锁
  ScopedFileInfoLock(FileInfoLock& file_info_lock, int timeout_ms)
      : file_info_lock_(file_info_lock), is_locked_(false) {
    is_locked_ = file_info_lock_.TryLockFor(timeout_ms);
    if (is_locked_) {
      std::cout << "Lock acquired successfully!" << std::endl;
    } else {
      std::cout << "Failed to acquire lock within the timeout period!" << std::endl;
    }
  }

  // 析构函数：自动释放锁
  ~ScopedFileInfoLock() {
    if (is_locked_) {
      file_info_lock_.Unlock();
      std::cout << "Lock released automatically!" << std::endl;
    }
  }

  // 检查是否成功获取锁
  bool IsLocked() const {
    return is_locked_;
  }

 private:
  FileInfoLock& file_info_lock_;
  bool is_locked_;
};


//kv存储系统
class KVStore {
public:
    static KVStore& getInstance() { //单例模式
        static KVStore instance;
        return instance;
    }

    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    // 设置缓存最大容量
    void setMaxCapacity(size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_capacity_ = capacity;
    }

    // 异步设置 key 并指定过期时间
    void asyncSet(const std::string& key, const std::string& value, std::chrono::seconds ttl) {
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            task_queue_.emplace([this, key, value, ttl]() {
                set(key, value, ttl);
            });
        }
        task_cv_.notify_one();
    }

    // 获取 key 的值
    std::string get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = store_.find(key);
        if (it == store_.end()) {
            return "";
        }

        if (std::chrono::system_clock::now() > it->second.expire_time) {
            access_order_.erase(it->second.it);
            store_.erase(it);
            return "";
        }

        access_order_.erase(it->second.it);
        access_order_.push_front(key);
        it->second.it = access_order_.begin();

        return it->second.value;
    }

    // 删除所有以指定前缀开头的 key
    void deleteKeysWithPrefix(const std::string& prefix) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = store_.begin(); it != store_.end(); ) {
            if (it->first.find(prefix) == 0) {
                access_order_.erase(it->second.it);
                it = store_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 数据持久化到文件
    void persistToFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream file(filename);
        if (file.is_open()) {
            for (const auto& entry : store_) {
                const auto& key = entry.first;
                const auto& value = entry.second.value;
                auto expire_time = entry.second.expire_time.time_since_epoch().count();
                file << key << "\t" << value << "\t" << expire_time << "\n";
            }
            file.close();
        }
    }

    // 从文件加载持久化数据
    void loadFromFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream file(filename);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                std::istringstream iss(line);
                std::string key, value;
                int64_t expire_time_count;
                if (std::getline(iss, key, '\t') && 
                    std::getline(iss, value, '\t') && 
                    (iss >> expire_time_count)) {
                    auto expire_time = std::chrono::system_clock::time_point(
                        std::chrono::nanoseconds(expire_time_count));
                    if (std::chrono::system_clock::now() < expire_time) {
                        set(key, value, std::chrono::duration_cast<std::chrono::seconds>(
                            expire_time - std::chrono::system_clock::now()));
                    }
                }
            }
            file.close();
        }
    }

    // 启动定时清理过期 key 的任务
    void startExpirationCleaner(std::chrono::seconds interval) {
        std::thread([this, interval]() {
            while (true) {
                std::this_thread::sleep_for(interval);
                cleanExpiredKeys();
            }
        }).detach();
    }

    // 启动工作线程
    void startWorkerThread() {
        worker_running_ = true;
        worker_thread_ = std::thread([this]() {
            while (worker_running_) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(task_mutex_);
                    task_cv_.wait(lock, [this]() {
                        return!task_queue_.empty() ||!worker_running_;
                    });
                    if (!task_queue_.empty()) {
                        task = std::move(task_queue_.front());
                        task_queue_.pop();
                    }
                }
                if (task) {
                    task();
                }
            }
        });
    }

    // 停止工作线程
    void stopWorkerThread() {
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            worker_running_ = false;
        }
        task_cv_.notify_one();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    KVStore() : max_capacity_(100) {
        startWorkerThread();
    }

    ~KVStore() {
        stopWorkerThread();
    }

    struct Entry {
        std::string value;
        std::chrono::system_clock::time_point expire_time;
        std::list<std::string>::iterator it;
    };

    // 同步设置 key 并指定过期时间
    void set(const std::string& key, const std::string& value, std::chrono::seconds ttl) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto expire_time = std::chrono::system_clock::now() + ttl;

        if (store_.find(key) != store_.end()) {
            access_order_.erase(store_[key].it);
        }

        access_order_.push_front(key);
        store_[key] = {value, expire_time, access_order_.begin()};

        if (store_.size() > max_capacity_) {
            auto last_key = access_order_.back();
            access_order_.pop_back();
            store_.erase(last_key);
        }
    }

    // 清理过期的 key
    void cleanExpiredKeys() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        for (auto it = store_.begin(); it != store_.end(); ) {
            if (now > it->second.expire_time) {
                access_order_.erase(it->second.it);
                it = store_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::unordered_map<std::string, Entry> store_;
    std::list<std::string> access_order_;
    size_t max_capacity_;
    std::mutex mutex_;

    std::queue<std::function<void()>> task_queue_;
    std::mutex task_mutex_;
    std::condition_variable task_cv_;
    std::thread worker_thread_;
    bool worker_running_ = false;
};


//测试范例
#if 0
int main() {
  FileInfoLock& file_lock = FileInfoLock::GetInstance();

  {
    // 使用 ScopedFileInfoLock 自动管理锁
    ScopedFileInfoLock lock(file_lock, 1000);  // 超时时间为 1000 毫秒
    if (lock.IsLocked()) {
      // 模拟一些操作
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } else {
      std::cout << "Cannot proceed without lock!" << std::endl;
    }
  }  // 锁在此处自动释放

  return 0;
}

#endif
#endif