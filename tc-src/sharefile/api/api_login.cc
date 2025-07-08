#include "api_login.h"

#include <iostream>
#include "muduo/base/Logging.h" // Logger日志头文件
#include <json/json.h>
#include <uuid/uuid.h>

using namespace std;

std::string generateUUID() {
    uuid_t uuid;
    uuid_generate_time_safe(uuid);  //调用uuid的接口
 
    char uuidStr[40] = {0};
    uuid_unparse(uuid, uuidStr);     //调用uuid的接口
 
    return std::string(uuidStr);
}

// / 解析登录信息
int decodeLoginJson(const std::string &str_json, string &user_name,
                    string &pwd) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse login json failed ";
        return -1;
    }
    // 用户名
    if (root["user"].isNull()) {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

    //密码
    if (root["pwd"].isNull()) {
        LOG_ERROR << "pwd null";
        return -1;
    }
    pwd = root["pwd"].asString();

    return 0;
}

// 封装登录结果的json
int encodeLoginJson(int code, string &token, string &str_json) {
    Json::Value root;
    root["code"] = code;
    if (code == 0) {
        root["token"] = token; // 正常返回的时候才写入token
    }
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int verifyUserPassword(string &user_name, string &pwd) {
    int ret = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);   //析构时自动归还连接

    // 根据用户名查询密码
    string strSql = FormatString("select password from user_info where user_name='%s'", user_name.c_str());
    CResultSet *result_set = db_conn->ExecuteQuery(strSql.c_str());
    if (result_set && result_set->Next()) { //如果存在则读取密码
        // 存在在返回
        string password = result_set->GetString("password");
        LOG_INFO << "mysql-pwd: " << password << ", user-pwd: " <<  pwd;
        if (password == pwd)            //对比密码是否一致
            ret = 0;                    //对比成功
        else
            ret = -1;                   //对比失败
    } else {                        // 说明用户不存在
        ret = -1;
    }

    delete result_set;

    return ret;
}

int setToken(string &user_name, string &token) {
    int ret = 0;

    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    token = generateUUID(); // 生成唯一的token

    if (cache_conn) {
        //token - 用户名, 86400有效时间为24小时  有效期可以自己修改
        cache_conn->SetEx(token, 86400, user_name); // redis做超时
    } else {
        ret = -1;
    }

    return ret;
 
}
 

int ApiUserLogin(std::string &post_data, std::string &resp_json){
    string user_name;
    string pwd;
    string token;
    // 判断数据是否为空
    if (post_data.empty()) {
        encodeLoginJson(1, token, resp_json);
        return -1;
    }

     // 解析json
    if (decodeLoginJson(post_data, user_name, pwd) < 0) {
        LOG_ERROR << "decodeRegisterJson failed";
        encodeLoginJson(1, token, resp_json);
        return -1;
    }
    // 验证账号和密码是否匹配
    if (verifyUserPassword(user_name, pwd) < 0) {
        LOG_ERROR << "verifyUserPassword failed";
        encodeLoginJson(1, token, resp_json);
        return -1;
    }
    // 生成token

    if (setToken(user_name, token) < 0) {
        LOG_ERROR << "setToken failed";
        encodeLoginJson(1, token, resp_json);
        return -1;
    }

     // 封装登录结果
    encodeLoginJson(0, token, resp_json);

    return 0;
}
