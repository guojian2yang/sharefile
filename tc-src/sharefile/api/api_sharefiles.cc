#include "api_sharefiles.h"

int encodeSharefilesJson(int ret, int total, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    if (ret == 0) {
        root["total"] = total; // 正常返回的时候才写入token
    }
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int handleGetSharefilesCount(int &count) {
    int ret = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);

    string str_sql = "select count(*) from share_file_list";
    CResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set && result_set->Next()) {
        // 存在在返回
        count = result_set->GetInt("count(*)");
        LOG_INFO << "count: " << count;
        ret = 0;
        delete result_set;
    } else if (!result_set) {
        // 操作失败
        LOG_ERROR << str_sql << " 操作失败";
        ret = 1;
    }
    return ret;
}

int decodeShareFileslistJson(string &str_json, int &start, int &count) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }

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
    LOG_INFO << "start: " << start << " count: " << count;

    return 0;
}

void handleGetShareFilelist(int start, int count, string &str_json) {
    //share_file_list
    // file_info
    int ret = 0;
    string str_sql;
    int total = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    
    CResultSet *result_set = NULL;
    int file_index = 0;
    Json::Value root, files;

    //获取总的文件数量
    ret =  handleGetSharefilesCount(total);
    if(ret != 0) {
        ret = -1;
        goto END;
    }
    
    // sql语句
    str_sql = FormatString(
        "select share_file_list.*, file_info.url, file_info.size, file_info.type from file_info, \
        share_file_list where file_info.md5 = share_file_list.md5 limit %d, %d",
        start, count);
    LOG_INFO << "执行: " <<  str_sql;

    result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set) { 
        // 遍历所有的内容
        // 获取大小
        file_index = 0;
        root["total"] = total;
        while (result_set->Next()) {
            Json::Value file;
            file["user"] = result_set->GetString("user");
            file["md5"] = result_set->GetString("md5");
            file["file_name"] = result_set->GetString("file_name");
            file["share_status"] = result_set->GetInt("share_status");
            file["pv"] = result_set->GetInt("pv");
            file["create_time"] = result_set->GetString("create_time");
            file["url"] = result_set->GetString("url");
            file["size"] = result_set->GetInt("size");
            file["type"] = result_set->GetString("type");
            files[file_index++] = file;  
        }
        root["count"] = file_index;
        if(file_index > 0) {
            root["files"] = files;    
        } else {
            LOG_WARN << "no files";
        }
        ret = 0;
        delete result_set;
    } else {
        ret = -1;
    }

END:
    if (ret == 0) {
        root["code"] = 0;
    } else {
        root["code"] = 1;
    }
    str_json = root.toStyledString();
}

void handleGetRankingFilelist(int start, int count, string &str_json) {
    // 定义缓存键
    std::string kv_key = "ranking_list_" + std::to_string(start) + "_" + std::to_string(count);
    // 先从一级缓存（KV 存储）获取数据
    std::string kv_data = KVStore::getInstance().get(kv_key);
    // cout << "kv_key: " << kv_key << " kv_data: " << kv_data;
    if (!kv_data.empty()) {
        // 缓存命中，直接返回缓存数据
        str_json = kv_data;
        // cout << "kv_key: " << kv_key << "缓存命中" << endl;
        return;
    }

    // cout << "kv_key: " << kv_key << "缓存未命中" << endl;

    // redis缓存和mysql的文件数量是不是一样
    //  如果不一样 要加载从mysql -> redis

    //从redis读取 下载榜单 做序列化
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    int total = 0;
    char filename[512] = {0};
    int sql_num;
    int redis_num;
    int score;
    int end;
    RVALUES value = NULL;
    Json::Value root;
    Json::Value files;
    
    int file_count = 0;
     CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CResultSet *pCResultSet = NULL;

    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("ranking_list");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    //获取共享文件数量从mysql
    ret = handleGetSharefilesCount(total);
    if (ret != 0) {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }
     //===1、mysql共享文件数量
    sql_num = total;
    //===2、redis共享文件数量
    redis_num = cache_conn->ZsetZcard(FILE_PUBLIC_ZSET); // Zcard 命令用于计算集合中元素的数量。
    if (redis_num == -1) {
        LOG_ERROR << "ZsetZcard  操作失败";
        ret = -1;
        goto END;
    }
    LOG_INFO << "sql_num: " << sql_num << ", redis_num: " <<  redis_num;
    if (redis_num != sql_num) // 如果数量太多会导致阻塞， redis mysql数据不一致怎么处理？
    { //===4、如果不相等，清空redis数据，重新从mysql中导入数据到redis
      //(mysql和redis交互)
         // a) 清空redis有序数据
        cache_conn->Del(FILE_PUBLIC_ZSET); // 删除集合
        cache_conn->Del(FILE_NAME_HASH); // 删除hash， 理解 这里hash和集合的关系

        // b) 从mysql中导入数据到redis  如果所有共享文件都加载redis
        // sql语句
        strcpy( sql_cmd, "select md5, file_name, pv from share_file_list order by pv desc");
        LOG_INFO << "执行: " <<  sql_cmd;
        pCResultSet = db_conn->ExecuteQuery(sql_cmd);
        if (!pCResultSet) {
            LOG_ERROR << sql_cmd << " 操作失败";
            ret = -1;
            goto END;
        }
         // mysql_fetch_row从使用mysql_store_result得到的结果结构中提取一行，并把它放到一个行结构中。
        // 当数据用完或发生错误时返回NULL.
        while ( pCResultSet->Next()) // 这里如果文件数量特别多，导致耗时严重，
                          // 可以这么去改进当
                          // mysql的记录和redis不一致的时候，开启一个后台线程去做同步
        {
            char field[1024] = {0};
            string md5 = pCResultSet->GetString("md5"); // 文件的MD5
            string file_name = pCResultSet->GetString("file_name"); // 文件名
            int pv = pCResultSet->GetInt("pv");
            sprintf(field, "%s%s", md5.c_str(),
                    file_name.c_str()); //文件标示，md5+文件名

            //增加有序集合成员
            cache_conn->ZsetAdd(FILE_PUBLIC_ZSET, pv, field);

            //增加hash记录
            cache_conn->Hset(FILE_NAME_HASH, field, file_name);
        }
        delete pCResultSet;
    }
    // 从redis读取 下载榜单
    //===5、从redis读取数据，给前端反馈相应信息
    // char value[count][1024];
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
        root["total"] = sql_num;
        root["count"]  = file_count;
        root["files"] = files;
    } else {
         root["code"] = 1;
    }
    str_json = root.toStyledString();
    // 缓存共享文件列表
    KVStore::getInstance().set(kv_key, str_json, std::chrono::seconds(60));
}


int ApiSharefiles(string &url, string &post_data, string &str_json){
    char cmd[20];
    string user_name;
    string token;
    int start = 0; //文件起点
    int count = 0; //文件个数
    int total = 0;
    int ret = 0;
    LOG_INFO << "post_data: " <<  post_data;
     //解析命令 解析url获取自定义参数
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    LOG_INFO << "cmd = " <<  cmd;

    if (strcmp(cmd, "count") == 0) // count 获取用户文件个数
    {
        ret = handleGetSharefilesCount(total);
        encodeSharefilesJson(ret, total, str_json);
    } else if (strcmp(cmd, "normal") == 0) {
        
         if (decodeShareFileslistJson(post_data, start, count) < 0){ //通过json包获取信息
            encodeSharefilesJson(1, 0, str_json);
            return 0;
        }
        // 获取共享文件
        handleGetShareFilelist(start, count, str_json);
    } else if (strcmp(cmd, "pvdesc") == 0) {
        if (decodeShareFileslistJson(post_data, start, count) < 0){ //通过json包获取信息
            encodeSharefilesJson(1, 0, str_json);
            return 0;
        }
        // 获取排行榜
        handleGetRankingFilelist(start, count, str_json);
    }



    return 0;
}