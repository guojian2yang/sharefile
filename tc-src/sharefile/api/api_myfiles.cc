#include "api_myfiles.h"

int decodeCountJson(string &str_json, string &user_name, string &token) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }
    int ret = 0;

    // 用户名
    if (root["user"].isNull()) {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

    //密码
    if (root["token"].isNull()) {
        LOG_ERROR << "token null";
        return -1;
    }
    token = root["token"].asString();

    return ret;
}

int encodeCountJson(int ret, int total, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    if (ret == 0) {
        root["total"] = total; // 正常返回的时候才写入token
    }
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int encodeGetFileListFailedJson(string &str_json) {
    Json::Value root;
    root["code"] = 1;
    
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

//查询数据库 根据user 查询 user_file_list
int getUserFilesCount(CDBConn *db_conn, string &user_name, int &count) {
    int ret = 0;
    //封装sql语句  select count(*) from user_file_list where user = 'qingfu';
    string sql_str = FormatString("select count(*) from user_file_list where user = '%s'", user_name.c_str());
    CResultSet *result_set = db_conn->ExecuteQuery(sql_str.c_str());
    if (result_set && result_set->Next()) {
        // 存在在返回
        count = result_set->GetInt("count(*)");
        LOG_INFO << "count: " << count;
        ret = 0;
        delete result_set;
    }  else {
        ret = -1;
        LOG_ERROR << "操作 " << sql_str << "失败";
    }

    return 0;
}

int handleUserFilesCount(string &user_name, int &count) {
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    // CacheManager *cache_manager = CacheManager::getInstance();
    // CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    // AUTO_REL_CACHECONN(cache_manager, cache_conn);

    int ret = getUserFilesCount(db_conn, user_name, count);
    return ret;
}

//解析的json包
// 参数
// {
// "count": 2,
// "start": 0,
// "token": "3a58ca22317e637797f8bcad5c047446",
// "user": "qingfu"
// }
int decodeFileslistJson(string &str_json, string &user_name, string &token,
                        int &start, int &count) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }
    int ret = 0;

    // 用户名
    if (root["user"].isNull()) {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

    //密码
    if (root["token"].isNull()) {
        LOG_ERROR << "token null";
        return -1;
    }
    token = root["token"].asString();

    if (root["start"].isNull()) {
        LOG_ERROR << "start null";
        return -1;
    }
    start = root["start"].asInt();

    if (root["count"].isNull()) {
        LOG_ERROR << "count null";
        return -1;
    }
    count = root["count"].asInt();

    return ret;
}

//正常顺序 升序 降序 count分页请求时 具体一页的大小
int getUserFileList(string cmd, string &user_name, int &start, int &count, string &str_json)
{
    LOG_INFO << "getUserFileList into";
    int ret = 0;
    int total = 0;
    string str_sql;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);

    //获取总的文件数量
    ret = getUserFilesCount(db_conn, user_name, total);
    if(ret < 0) {
        LOG_ERROR << "getUserFilesCount failed";
        Json::Value root;
        root["code"] = 1;
        Json::FastWriter writer;
        str_json = writer.write(root);
        return -1;
    }
    if(0 == total) {
        Json::Value root;
        root["code"] = 0;
        root["count"] = 0;
        root["total"] = 0;
        Json::FastWriter writer;
        str_json = writer.write(root);
        return 0;
    }
    //select user_file_list.*, file_info.url, file_info.size from file_info, user_file_list where user = 'qingfu' and file_info.md5 = user_file_list.md5 limit 0, 10;
    str_sql = FormatString("select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, \
             user_file_list where user = '%s' and file_info.md5 = user_file_list.md5 limit %d, %d ", 
              user_name.c_str(), start, count);
    LOG_INFO << "执行：" << str_sql;
    CResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set) {
        Json::Value root;
        Json::Value files;
        root["code"] = 0;
        root["total"] = total; //即使 total 不为0， file_index为0
        int file_index = 0;
        // 和查询结果对比 total
        while (result_set->Next())
        {
            Json::Value file;
            file["user"] = result_set->GetString("user");
            file["md5"] = result_set->GetString("md5");
            file["create_time"] = result_set->GetString("create_time");
            file["file_name"] = result_set->GetString("file_name");
            file["share_status"] = result_set->GetInt("shared_status");
            file["pv"] = result_set->GetInt("pv");
            file["url"] = result_set->GetString("url");
            file["size"] = result_set->GetInt("size");
            file["type"] = result_set->GetString("type");

            files[file_index] = file;
            file_index++;
        }
        root["files"] = files;
        root["count"] = file_index;
        Json::FastWriter writer;
        str_json = writer.write(root);
        LOG_INFO << "str_json: " << str_json;
        delete result_set;
        return 0;
    } else  {
        LOG_ERROR << "ExecuteQuery failed";
        Json::Value root;
        root["code"] = 1;
        Json::FastWriter writer;
        str_json = writer.write(root);
        return -1;
    }

}

// /api/myfiles&cmd=count
// /api/myfiles&cmd=normal
int ApiMyfiles(string &url, string &post_data, string &str_json){
    char cmd[20]; 
    string user_name;
    string token;
    int ret = 0;
    int total_count = 0;
    int start;
    int count;
    // 解析命令

    //解析命令 解析url获取自定义参数
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    if(strcmp(cmd, "count") == 0) {
        //反序列化
        if (decodeCountJson(post_data, user_name, token) < 0) {
            encodeCountJson(1, 0, str_json);
            LOG_ERROR << "decodeCountJson failed";
            return -1;
        }
        //校验token
        ret = VerifyToken(user_name, token);
        if(ret < 0) {
            encodeCountJson(1, 0, str_json);
            LOG_ERROR << "VerifyToken failed";
            return -1;
        }
        // 获取文件数量
        ret =  handleUserFilesCount(user_name, total_count);
        if(ret < 0) {
            encodeCountJson(1, 0, str_json);
        } else {
            encodeCountJson(0, total_count, str_json);
        }
        return ret;
        
    } else if(strcmp(cmd, "normal") == 0) {
        //反序列化
        if(decodeFileslistJson(post_data, user_name, token, start, count) < 0) {
            encodeGetFileListFailedJson(str_json);
            LOG_ERROR << "decodeCountJson failed";
            return -1;
        }
        //token校验
         //校验token
        ret = VerifyToken(user_name, token);
        if(ret < 0) {
            encodeGetFileListFailedJson(str_json);
            LOG_ERROR << "VerifyToken failed";
            return -1;
        }

        getUserFileList(cmd, user_name, start, count, str_json);
     } else {
        encodeGetFileListFailedJson(str_json); 
        LOG_ERROR << "un handle" << cmd;  
    }

    return 0;
}