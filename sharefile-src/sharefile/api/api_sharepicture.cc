#include "api_sharepicture.h"
#include "shorturl_rpc.pb.h"

#define JSON_ENABLE 0
#define PROTOBUF_ENABLE 1
//解析的json包
int decodeSharePictureJson(string &str_json, string &user_name, string &token,
                           string &md5, string &filename) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }

    if (root["token"].isNull()) {
        LOG_ERROR << "token null";
        return -1;
    }
    token = root["token"].asString();

    if (root["user"].isNull()) {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

    if (root["md5"].isNull()) {
        LOG_ERROR << "md5 null";
        return -1;
    }
    md5 = root["md5"].asString();

    if (root["filename"].isNull()) {
        LOG_ERROR << "filename null";
        return -1;
    }
    filename = root["filename"].asString();

    return 0;
}
int encodeSharePictureJson(int ret, string urlmd5, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    if (HTTP_RESP_OK == ret)
        root["urlmd5"] = urlmd5;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int handleSharePicture(const char *user, const char *filemd5,
                       const char *file_name, string &str_json) 
{
    // 获取数据库连接
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    int ret = 0;
    string key;
    string urlmd5;
    urlmd5 = generateUUID();
    char create_time[TIME_STRING_LEN];
    time_t now;
    //获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S", localtime(&now));
    // share_picture_list 添加记录
    string str_sql = FormatString("insert into share_picture_list (user, filemd5, file_name, urlmd5, `key`, pv, create_time) values ('%s', '%s', '%s', '%s', '%s', %d, '%s')", 
        user, filemd5, file_name, urlmd5.c_str(), key.c_str(), 0, create_time);
    LOG_INFO << "执行：" << str_sql;
    if (!db_conn->ExecuteCreate(str_sql.c_str())) {
        LOG_ERROR << str_sql << " 操作失败";
        ret = 1;
    } else {
        ret = 0;
    }

    if (ret == 0) {
        // 构建完整分享链接
        std::string baseUrl = "http://192.168.31.43/share?urlmd5=";
        std::string fullShareUrl = baseUrl + urlmd5;

        // 调用 RPC 接口将完整分享链接转为短链
        #if JSON_ENABLE
        std::string rpcResponse = convertToShortUrl(fullShareUrl); 
        Json::Value rpcJson;
        Json::Reader reader;
        if (reader.parse(rpcResponse, rpcJson) && rpcJson["code"].asInt() == 0) {
            std::string shortUrl = rpcJson["urlmd5"].asString();
            std::cout << "shortUrl: " << shortUrl << std::endl;
        } else {
            LOG_ERROR << "RPC 调用生成短链失败";
            ret = -1;
        }
#elif PROTOBUF_ENABLE
        cout << "fullShareUrl: " << fullShareUrl << endl;
        std::string rpcResponse = convertToShortUrl(fullShareUrl); 
        if (rpcResponse.size() > 0) {
            std::string shortUrl = rpcResponse;
            std::cout << "shortUrl: " << shortUrl << std::endl;
        } else {
            LOG_ERROR << "RPC 调用生成短链失败";
            ret = -1;
        }
#endif
    }

     if (ret == 0) {
        encodeSharePictureJson(HTTP_RESP_OK, urlmd5, str_json);
    } else {
        encodeSharePictureJson(HTTP_RESP_FAIL, urlmd5, str_json);
    }

    return 0;
}
int decodeBrowsePictureJson(string &str_json, string &urlmd5) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }

    if (root["urlmd5"].isNull()) {
        LOG_ERROR << "urlmd5 null";
        return -1;
    }
    urlmd5 = root["urlmd5"].asString();
    // cout << "urlmd5: " << urlmd5 << endl;
    return 0;
}

int encodeBrowselPictureJson(int ret, int pv, string url, string user,
                             string time, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    if (ret == 0) {
        root["pv"] = pv;
        root["url"] = url;
        root["user"] = user;
        root["time"] = time;
    }
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int handleBrowsePicture(const char *urlmd5, string &str_json) {

    int ret = 0;
    
    string picture_url;
    string file_name;
    string user;
    string filemd5;
    string create_time;
    int pv = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);
    // LOG_INFO << "urlmd5: " <<  urlmd5;
    cout << "urlmd5: " << urlmd5 << endl;

    string sql_cmd = FormatString( "select user, filemd5, file_name, pv, create_time from "
            "share_picture_list where urlmd5 = '%s'", urlmd5);
    LOG_INFO << "执行: " <<  sql_cmd;
    CResultSet * result_set = db_conn->ExecuteQuery(sql_cmd.c_str());
    if (result_set && result_set->Next()) {
        user = result_set->GetString("user");
        filemd5 = result_set->GetString("filemd5");
        file_name = result_set->GetString("file_name");
        pv = result_set->GetInt("pv");
        create_time = result_set->GetString("create_time");
        delete result_set;
    } else {
        if (result_set)
            delete result_set;
        ret = -1;
        goto END;
    }

    cout << "user: " << user << endl;
    cout << "filemd5: " << filemd5 << endl;
    cout << "file_name: " << file_name << endl;
    cout << "pv: " << pv << endl;
    cout << "create_time: " << create_time << endl;

// 2. 通过文件的MD5查找对应的url地址
    sql_cmd = FormatString("select url from file_info where md5 ='%s'", filemd5.c_str());
    LOG_INFO << "执行: " <<  sql_cmd;
    result_set = db_conn->ExecuteQuery(sql_cmd.c_str());
    if (result_set && result_set->Next()) {
        picture_url = result_set->GetString("url");
        cout << "picture_url: " << picture_url << endl;
        delete result_set;
    } else {
        if (result_set)
            delete result_set;
        ret = -1;
        goto END;
    }
    //更新访问计数 pv
    pv += 1;
    sql_cmd = FormatString( "update share_picture_list set pv = pv+1 where urlmd5 = '%s'", urlmd5);
    LOG_INFO << "执行: " <<  sql_cmd;
    if (!db_conn->ExecuteUpdate(sql_cmd.c_str())) {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }
    ret = 0;   
END:
    // 4. 返回urlmd5 和提取码key
    if (ret == 0) {
        encodeBrowselPictureJson(HTTP_RESP_OK, pv, picture_url, user,
                                 create_time, str_json);
    } else {
        encodeBrowselPictureJson(HTTP_RESP_FAIL, pv, picture_url, user,
                                 create_time, str_json);
    }

    return 0;

}

//解析的json包
int decodePictureListJson(string &str_json, string &user_name, string &token,
                          int &start, int &count) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }

    if (root["token"].isNull()) {
        LOG_ERROR << "token null";
        return -1;
    }
    token = root["token"].asString();

    if (root["user"].isNull()) {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

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

    return 0;
}

//获取共享图片个数
int getSharePicturesCount(CDBConn *db_conn,  string &user_name, int &count) {
    int ret = 0;
 
    // 从mysql加载
    if (DBGetSharePictureCountByUsername(db_conn, user_name, count) < 0) {
        LOG_ERROR << "DBGetSharePictureCountByUsername failed";
        return -1;
    }

    return ret;
}

void handleGetSharePicturesList(const char *user, int start, int count, string &str_json) 
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    CResultSet *result_set = NULL;
    int total = 0;
    int file_count = 0;
    Json::Value root;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);

    total = 0;
    string temp_user = user;
    ret = getSharePicturesCount(db_conn, temp_user, total);
    if (ret < 0) {
        LOG_ERROR << "getSharePicturesCount failed";
        ret = -1;
        goto END;
    }
     // sql语句
    sprintf(
        sql_cmd,
        "select share_picture_list.user, share_picture_list.filemd5, share_picture_list.file_name,share_picture_list.urlmd5, share_picture_list.pv, \
        share_picture_list.create_time, file_info.size from file_info, share_picture_list where share_picture_list.user = '%s' and  \
        file_info.md5 = share_picture_list.filemd5 limit %d, %d", user, start, count);
    LOG_INFO << "执行: " <<  sql_cmd;
    result_set = db_conn->ExecuteQuery(sql_cmd);
    if (result_set) {
        // 遍历所有的内容
        // 获取大小
        Json::Value files;
        while (result_set->Next()) {
            Json::Value file;
            file["user"] = result_set->GetString("user");
            file["filemd5"] = result_set->GetString("filemd5");
            file["file_name"] = result_set->GetString("file_name");
            file["urlmd5"] = result_set->GetString("urlmd5");
            file["pv"] = result_set->GetInt("pv");
            file["create_time"] = result_set->GetString("create_time");
            file["size"] = result_set->GetInt("size");
            files[file_count] = file;
            file_count++;
        }
        if (file_count > 0)
            root["files"] = files;

        ret = 0;
        delete result_set;
    } else {
        ret = -1;
    }
END:
    if (ret != 0) {
        Json::Value root;
        root["code"] = 1;
    } else {
        root["code"] = 0;
        root["count"] = file_count;
        root["total"] = total;
    }
    str_json = root.toStyledString();
    LOG_INFO << "str_json: " << str_json;

    return;   

}


int decodeCancelPictureJson(string &str_json, string &user_name,
                            string &urlmd5) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }

    if (root["user"].isNull()) {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

    if (root["urlmd5"].isNull()) {
        LOG_ERROR << "urlmd5 null";
        return -1;
    }
    urlmd5 = root["urlmd5"].asString();

    return 0;
}

int encodeCancelPictureJson(int ret, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}


//取消分享文件
void handleCancelSharePicture(const char *user, const char *urlmd5,
                              string &str_json) {
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
 
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);
 

    //删除在共享图片列表的数据
    sprintf(sql_cmd,
        "delete from share_picture_list where user = '%s' and urlmd5 = '%s'",
        user, urlmd5);
    LOG_INFO << "执行: " <<  sql_cmd;
    if (!db_conn->ExecutePassQuery(sql_cmd)) {
        LOG_ERROR << sql_cmd << " 操作失败";
        encodeCancelPictureJson(HTTP_RESP_FAIL, str_json);
    } else {
        encodeCancelPictureJson(HTTP_RESP_OK, str_json);
    }            
}


int ApiSharepicture(string &url, string &post_data, string &str_json){
    char cmd[20];
    string user_name; //用户名
    string md5;       //文件md5码
    string urlmd5;
    string shortUrl;
    string filename; //文件名字
    string token;
    int ret = 0;
     //解析命令
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    LOG_INFO << "cmd = " <<  cmd;

    if (strcmp(cmd, "share") == 0) //分享文件
    {
        ret = decodeSharePictureJson(post_data, user_name, token, md5, filename);
        if (ret == 0) {
            cout << "user_name: " << user_name << ", md5: " << md5 << ", filename: " << filename << endl;
            handleSharePicture(user_name.c_str(), md5.c_str(), filename.c_str(), str_json);
        } else {
            // 回复请求格式错误
            encodeSharePictureJson(HTTP_RESP_FAIL, urlmd5, str_json);
        }
    } else if (strcmp(cmd, "browse") == 0) //请求浏览图片
    {
        ret = decodeBrowsePictureJson(post_data, urlmd5);

        if (ret == 0) {
            handleBrowsePicture(urlmd5.c_str(), str_json);
        } else {
            // 回复请求格式错误
            encodeSharePictureJson(HTTP_RESP_FAIL, urlmd5, str_json);
        }
    } else if (strcmp(cmd, "normal") == 0)  
    {
        int start = 0;
        int count = 0;
        ret = decodePictureListJson(post_data, user_name, token, start, count);
        if (ret == 0) {
            handleGetSharePicturesList(user_name.c_str(), start, count,  str_json);
        } else {
            // 回复请求格式错误
            encodeSharePictureJson(HTTP_RESP_FAIL, urlmd5, str_json);
        }
    }else if (strcmp(cmd, "cancel") == 0) //取消分享文件
    {
        //理论上需要token
        ret = decodeCancelPictureJson(post_data, user_name, urlmd5);
        if (ret == 0) {
            handleCancelSharePicture(user_name.c_str(), urlmd5.c_str(), str_json);
        } else {
            // 回复请求格式错误
            encodeCancelPictureJson(1, str_json);
        }
    }
    else {
        LOG_WARN << "un handle " << cmd;
    }
    return 0;
}