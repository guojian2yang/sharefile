#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <json/json.h>
#include <thread>
#include <unordered_map>
#include <random>
#include <ctime>
#include "api_common.h"
#include "shorturl_rpc.pb.h"

#define JSON_ENABLE 0
#define PROTOBUF_ENABLE 1

// 存储短链标识和完整链接的映射
// std::unordered_map<std::string, std::string> shortUrlMap;
std::string generateErrorResponse();

// 生成短链标识
std::string generateShortKey() {
    static const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);
    std::string shortKey;
    for (int i = 0; i < 6; ++i) {
        shortKey += chars[dis(gen)];
    }
    return shortKey;
}

#if JSON_ENABLE
// 处理完整链接转短链请求
std::string handleConvertToShortUrlRequest(const Json::Value& params) {
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    std::string fullUrl = params["full_url"].asString();
    std::string shortKey = generateShortKey();
    // 确保短链标识唯一
    // 在 Redis 中检查短链是否已存在，存在则重新生成
    // 在 Redis 中检查短链是否已存在，存在则重新生成
    while (!cache_conn->Get(shortKey).empty()) {
        shortKey = generateShortKey();
    }
    // 存储映射关系
    // 存储映射关系到 Redis
    std::string setResult = cache_conn->Set(shortKey, fullUrl);
    if (setResult != "OK") {
        std::cerr << "Redis set failed: " << setResult << std::endl;
        return generateErrorResponse();
    }

    // 存储映射关系到 MySQL
    std::string str_sql = FormatString("insert into short_url_map (short_key, full_url) values ('%s', '%s')", 
        shortKey.c_str(), fullUrl.c_str());
    LOG_INFO << "执行：" << str_sql;
    if (!db_conn->ExecuteCreate(str_sql.c_str())) {
        LOG_ERROR << str_sql << " 操作失败";
    } 
    std::cout << "fullUrl: " << fullUrl << " shortKey: " << shortKey << std::endl;
    // 构建短链
    std::string shortUrl = "http://192.168.31.43/p/" + shortKey;

    Json::Value response;
    response["code"] = 0;
    response["urlmd5"] = shortUrl;
    Json::FastWriter writer;
    return writer.write(response);
}

// 封装错误响应函数
std::string generateErrorResponse() {
    Json::Value response;
    response["code"] = -1;
    Json::FastWriter writer;
    return writer.write(response);
}

std::string handleResolveShortUrlRequest(const Json::Value& params) {
    std::string shortUrl = params["short_url"].asString();
    std::string shortKey;

    // 从短链中提取标识符 (如从 "http://192.168.31.43/aC4ii5" 提取 "aC4ii5")
    if(shortUrl.find("http://") == 0) {
        size_t lastSlash = shortUrl.find_last_of('/');
        if (lastSlash == std::string::npos) {
            return generateErrorResponse();
        }
        shortKey = shortUrl.substr(lastSlash + 1);
    } else {
        // 如果不以 "http://" 开头，直接将 shortUrl 作为 shortKey
        shortKey = shortUrl;
    }

    // 移除可能的 JSON 转义字符
    size_t quotePos = shortKey.find('"');
    if(quotePos != std::string::npos) {
        shortKey = shortKey.substr(0, quotePos);
    }

    std::cout << "Cleaned shortKey: " << shortKey << std::endl;

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    // 先从 Redis 中查找
    std::string fullUrl = cache_conn->Get(shortKey);
    if (!fullUrl.empty()) {
        Json::Value response;
        response["code"] = 0;
        response["full_url"] = fullUrl;
        Json::FastWriter writer;
        return writer.write(response);
    }

    // Redis 未找到，从 MySQL 中查找
    string sql_cmd = FormatString( "select full_url from "
            "short_url_map where short_key = '%s'", shortKey.c_str());
    CResultSet * result_set = db_conn->ExecuteQuery(sql_cmd.c_str());
    if (result_set) {
        std::string fullUrl = result_set->GetString("full_url");

        // 将结果缓存到 Redis
        std::string setResult = cache_conn->Set(shortKey, fullUrl);
        if (setResult != "OK") {
            std::cerr << "Redis cache set failed: " << setResult << std::endl;
        }

        Json::Value response;
        response["code"] = 0;
        response["full_url"] = fullUrl;
        Json::FastWriter writer;
        delete result_set;
        return writer.write(response);
    }
    if (result_set) {
        delete result_set;
    }

    return generateErrorResponse();
}

// 处理客户端请求
std::string handleRequest(const std::string& requestStr) {
    Json::Value request;
    Json::Reader reader;
    if (reader.parse(requestStr, request)) {
        std::string method = request["method"].asString();
        Json::Value params = request["params"];

        if (method == "ConvertToShortUrl") {
            return handleConvertToShortUrlRequest(params);
        }
        else if (method == "ResolveShortUrl") {
            return handleResolveShortUrlRequest(params);
        }
    }

    Json::Value response;
    response["code"] = -1;
    Json::FastWriter writer;
    return writer.write(response);
}

#elif PROTOBUF_ENABLE
// 处理完整链接转短链请求
shorturl_rpc::ConvertToShortUrlResponse handleConvertToShortUrlRequest(const shorturl_rpc::ConvertToShortUrlRequest& request) {
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    std::string fullUrl = request.full_url();
    std::string shortKey = generateShortKey();
    // 确保短链标识唯一
    while (!cache_conn->Get(shortKey).empty()) {
        shortKey = generateShortKey();
    }
    // 存储映射关系到 Redis
    std::string setResult = cache_conn->Set(shortKey, fullUrl);
    shorturl_rpc::ConvertToShortUrlResponse response;
    if (setResult != "OK") {
        std::cerr << "Redis set failed: " << setResult << std::endl;
        response.set_code(-1);
        return response;
    }

    // 存储映射关系到 MySQL
    std::string str_sql = FormatString("insert into short_url_map (short_key, full_url) values ('%s', '%s')", 
        shortKey.c_str(), fullUrl.c_str());
    LOG_INFO << "执行：" << str_sql;
    if (!db_conn->ExecuteCreate(str_sql.c_str())) {
        LOG_ERROR << str_sql << " 操作失败";
        response.set_code(-1);
        return response;
    } 
    std::cout << "fullUrl: " << fullUrl << " shortKey: " << shortKey << std::endl;
    // 构建短链
    std::string shortUrl = "http://192.168.31.43/p/" + shortKey;

    response.set_code(0);
    response.set_urlmd5(shortUrl);
    return response;
}

// 封装错误响应函数
shorturl_rpc::ResolveShortUrlResponse generateProtobufErrorResponse() {
    shorturl_rpc::ResolveShortUrlResponse response;
    response.set_code(-1);
    return response;
}

std::string handleResolveShortUrlRequest(const shorturl_rpc::ResolveShortUrlRequest& request) {
    std::string shortUrl = request.short_url();
    std::string shortKey;

    // cout << "shortUrl1: " << shortUrl << endl;

    // 从短链中提取标识符 
    if(shortUrl.find("http://") == 0) {
        size_t lastSlash = shortUrl.find_last_of('/');
        // cout<< "shortUrl2: " << shortUrl << endl;
        if (lastSlash == std::string::npos) {
            shorturl_rpc::ResolveShortUrlResponse response = generateProtobufErrorResponse();
            std::string responseStr;
            response.SerializeToString(&responseStr);
            return responseStr;
        }
        shortKey = shortUrl.substr(lastSlash + 1);
        // cout << "shortKey: " << shortKey << endl;
    } else {
        shortKey = shortUrl;
    }

    // 移除可能的 JSON 转义字符
    size_t quotePos = shortKey.find('"');
    if(quotePos != std::string::npos) {
        shortKey = shortKey.substr(0, quotePos);
    }

    // std::cout << "Cleaned shortKey: " << shortKey << std::endl;

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    // 先从 Redis 中查找
    std::string fullUrl = cache_conn->Get(shortKey);
    shorturl_rpc::ResolveShortUrlResponse response;
    if (!fullUrl.empty()) {
        response.set_code(0);
        response.set_full_url(fullUrl);
    } else {
        // Redis 未找到，从 MySQL 中查找
        std::string sql_cmd = FormatString( "select full_url from "
                "short_url_map where short_key = '%s'", shortKey.c_str());
        CResultSet * result_set = db_conn->ExecuteQuery(sql_cmd.c_str());
        if (result_set) {
            fullUrl = result_set->GetString("full_url");
            // 将结果缓存到 Redis
            std::string setResult = cache_conn->Set(shortKey, fullUrl);
            if (setResult != "OK") {
                std::cerr << "Redis cache set failed: " << setResult << std::endl;
            }
            response.set_code(0);
            response.set_full_url(fullUrl);
            delete result_set;
        } else {
            response = generateProtobufErrorResponse();
        }
    }
    std::string responseStr;
    response.SerializeToString(&responseStr);
    return responseStr;
}

std::string handleconvertRequest(const std::string& requestStr) {
    shorturl_rpc::ConvertToShortUrlRequest convertRequest;
    if (convertRequest.ParseFromString(requestStr)) {
        // cout << "convertRequest: " << endl;
        shorturl_rpc::ConvertToShortUrlResponse response = handleConvertToShortUrlRequest(convertRequest);
        std::string responseStr;
        response.SerializeToString(&responseStr);
        return responseStr;
    }
    shorturl_rpc::ResolveShortUrlResponse response = generateProtobufErrorResponse();
    std::string responseStr;
    response.SerializeToString(&responseStr);
    return responseStr;
}


// 处理客户端请求
std::string handleresolveRequest(const std::string& requestStr) {
    shorturl_rpc::ResolveShortUrlRequest resolveRequest;
    if (resolveRequest.ParseFromString(requestStr)) {
        cout << "resolveRequest: " << endl;
        return handleResolveShortUrlRequest(resolveRequest);
    }
    shorturl_rpc::ResolveShortUrlResponse response = generateProtobufErrorResponse();
    std::string responseStr;
    response.SerializeToString(&responseStr);
    return responseStr;
}

#endif

// 启动 RPC 服务
void RunRpcServer() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        return;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(50051);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return;
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        return;
    }

    std::cout << "RPC Server listening on port 50051" << std::endl;

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        memset(buffer, 0, sizeof(buffer));
        read(new_socket, buffer, 1024);
        std::string requestStr(buffer);

        std::string responseStr = handleconvertRequest(requestStr);

        send(new_socket, responseStr.c_str(), responseStr.length(), 0);
        close(new_socket);
    }
}

void startRpcServer() {
    srand(time(NULL));
    RunRpcServer();
}
