#include "api_dealfile.h"
int decodeDealfileJson(string &str_json, string &user_name, string &token,
                       string &md5, string &filename) {
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

    if (root["token"].isNull()) {
        LOG_ERROR << "token null";
        return -1;
    }
    token = root["token"].asString();

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
int encodeDealfileJson(int ret, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);

    // LOG_INFO << "str_json: " <<  str_json;
    return 0;
}

// 分享文件
int handleShareFile(string &user, string &md5, string &filename)
{
    int share_state = 0;
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char fileid[1024] = {0};    //md5+文件名
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);

    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("ranking_list");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    //文件标示，md5+文件名  member
    sprintf(fileid, "%s%s", md5.c_str(), filename.c_str());     //使用md5+文件名作为唯一的key

    //先先在redis查询 zset 
    if (cache_conn) {
        // ret=1 说明member存在
        ret = cache_conn->ZsetExit(FILE_PUBLIC_ZSET, fileid);
    } else {
        ret = 0;
    }

    LOG_INFO << "fileid: " << fileid << ", ZsetExit: " <<  ret;
    if (ret == 1) {
         LOG_WARN << "别人已经分享此文件";
        share_state = 3;
        goto END;
    } 

    //如果不存在 还要在mysql
    //查看此文件别人是否已经分享了
    sprintf(sql_cmd, "select * from share_file_list where md5 = '%s' and file_name = '%s'",
                md5.c_str(), filename.c_str());   
    //返回值：1有记录
    ret = CheckwhetherHaveRecord(db_conn, sql_cmd); //执行sql语句, 最后一个参数为NULL             
    if (ret == 1) //说明有结果，别人已经分享此文件
    {
        // redis保存此文件信息
        cache_conn->ZsetAdd(FILE_PUBLIC_ZSET, 0, fileid);
        cache_conn->Hset(FILE_NAME_HASH, fileid, filename);
        LOG_WARN << "别人已经分享此文件";
        share_state = 3;
        goto END;
    }

    //到这里 说明没有分享过了
    // user_file_list
    // sql语句, 更新共享标志字段
    sprintf(sql_cmd, "update user_file_list set shared_status = 1 where user = '%s' and "
            "md5 = '%s' and file_name = '%s'", user.c_str(), md5.c_str(), filename.c_str());
    LOG_INFO << "执行 " << sql_cmd;
    if (!db_conn->ExecuteUpdate(sql_cmd, false)) {
        LOG_ERROR << sql_cmd << " 操作失败";
        
        share_state = 1;
        goto END;
    }

    time_t now;
    char create_time[TIME_STRING_LEN];
    //获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S", localtime(&now));

    //插入一条share_file_list记录
    sprintf(sql_cmd,  "insert into share_file_list (user, md5, create_time, file_name, pv) values ('%s', '%s', '%s', '%s', %d)",
            user.c_str(), md5.c_str(), create_time, filename.c_str(), 0);
    if (!db_conn->ExecuteCreate(sql_cmd)) {
        LOG_ERROR << sql_cmd << " 操作失败";
        share_state = 1;
        goto END;
    }
    // 一级缓存key失效
    //设置redis 
    // redis保存此文件信息
    KVStore::getInstance().deleteKeysWithPrefix("ranking_list_");
    cache_conn->ZsetAdd(FILE_PUBLIC_ZSET, 0, fileid);
    if (cache_conn->Hset(FILE_NAME_HASH, fileid, filename) < 0) {
        LOG_WARN << "Hset FILE_NAME_HASH failed";
    }
    share_state = 0;

END:
    return share_state;

}

//删除文件 这里是删除文件，不是取消分享
int handleDeleteFile(string &user, string &md5, string &filename) 
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char fileid[1024] = {0};
    
    int shared_status = 0;        //共享状态
    int redis_has_record = 0; //标志redis是否有记录
     int count = 0;
    std::string file_id;
    
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);

    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("ranking_list");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);



    // 查看自己是否分享过这个文件 如果你没有分享过
    // sql语句
    //查看该文件是否已经分享了
    sprintf(sql_cmd,
            "select shared_status from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'",
            user.c_str(), md5.c_str(), filename.c_str());
    LOG_INFO << "执行: " <<  sql_cmd;

    ret = GetResultOneStatus(db_conn, sql_cmd, shared_status); //执行sql语句
    if (ret == 0) {
        LOG_INFO << "GetResultOneCount share  = " <<  shared_status;
    } else {
         LOG_ERROR << "GetResultOneStatus" << " 操作失败";
        ret = -1;
        goto END;
    }

    //如果分享过 那也从共享文件删除
    if(1 == shared_status) {
            //从redis
          //文件标识，文件md5+文件名
        KVStore::getInstance().deleteKeysWithPrefix("ranking_list_");
        sprintf(fileid, "%s%s", md5.c_str(), filename.c_str());
          //有序集合删除指定成员
        cache_conn->ZsetZrem(FILE_PUBLIC_ZSET, fileid);
        //从hash移除相应记录
        cache_conn->Hdel(FILE_NAME_HASH, fileid);
         // 删除在共享列表的数据, 如果自己分享了这个文件，那同时从分享列表删除掉
          // share_file_list删除 自己分享的才能删除
        sprintf(sql_cmd,
                "delete from share_file_list where user = '%s' and md5 = '%s' "
                "and file_name = '%s'",
                user.c_str(), md5.c_str(), filename.c_str());
        LOG_INFO << "执行: " <<  sql_cmd;
        if (!db_conn->ExecuteDrop(sql_cmd)) {
            LOG_ERROR << sql_cmd << " 操作失败";
            ret = -1;
            goto END;
        }
    }

    // 锁住
    //mysql删除
    // user_file_list删除
    //删除用户文件列表数据
    sprintf(sql_cmd,
            "delete from user_file_list where user = '%s' and md5 = '%s' and "
            "file_name = '%s'",
            user.c_str(), md5.c_str(), filename.c_str());
    LOG_INFO << "执行: " <<  sql_cmd;
    if (!db_conn->ExecuteDrop(sql_cmd)) {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }
    
   
    {
        FileInfoLock& file_lock = FileInfoLock::GetInstance();
        ScopedFileInfoLock lock(file_lock, 1000);
        if (lock.IsLocked()) {
            sprintf(sql_cmd, "select count, file_id from file_info where md5 = '%s'",  md5.c_str());
            LOG_INFO << "执行: " <<  sql_cmd;  
            CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);
            if (result_set && result_set->Next()) {
                count = result_set->GetInt("count");
                file_id = result_set->GetString("file_id");
                if (count > 1) {
                    snprintf(sql_cmd, sizeof(sql_cmd), "update file_info SET count = count - 1 WHERE md5 = '%s'", md5.c_str());
                    LOG_INFO << "执行: " << sql_cmd;
                    if (!db_conn->ExecuteUpdate(sql_cmd)) {
                        LOG_ERROR << sql_cmd << " 操作失败";
                        ret = -1;
                        goto END;
                    }
                }
            }else if (count == 1) {
                // 如果 count == 1，删除记录并从 FastDFS 删除文件
                snprintf(sql_cmd, sizeof(sql_cmd), "delete from file_info WHERE md5 = '%s'", md5.c_str());
                LOG_INFO << "执行: " << sql_cmd;
                if (!db_conn->ExecuteDrop(sql_cmd)) {
                    LOG_ERROR << sql_cmd << " 操作失败";
                    ret = -1;
                    goto END;
                }
            }
        } else {
            //超时
            LOG_ERROR << "FileInfoLock TryLockFor" << "超时";
            ret = 1;
            goto END;
        }
    }

    if(count == 1) {  //这个不需要加锁
         // 从 FastDFS 删除文件
        ret = RemoveFileFromFastDfs(file_id.c_str());
        if (ret != 0) {
            LOG_ERROR << "RemoveFileFromFastDfs err: " << ret;
            ret = -1;
            goto END;
        }
    }
   
    
    ret = 0;
    
END:
     /*
    删除文件：
        成功：{"code":"013"}
        失败：{"code":"014"}
    */
    if (ret == 0) {
        return 0;
    } else {
        return 1;
    }

}

static int handlePvFile(string &user, string &md5, string &filename) {
     int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    int pv = 0;

    
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);
    // sql语句
    //查看该文件的pv字段
    sprintf(sql_cmd,
            "select pv from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'",
            user.c_str(), md5.c_str(), filename.c_str());
     LOG_INFO << "执行: " <<  sql_cmd;
    CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);
    if (result_set && result_set->Next()) {
        pv = result_set->GetInt("pv");
    } else {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }
     //更新该文件pv字段，+1
    sprintf(sql_cmd,  "update user_file_list set pv = %d where user = '%s' and md5 = "
            "'%s' and file_name = '%s'",
            pv + 1, user.c_str(), md5.c_str(), filename.c_str());
    LOG_INFO << "执行: " <<  sql_cmd;
    if (!db_conn->ExecuteUpdate(sql_cmd)) {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }    
END:
    /*
    下载文件pv字段处理
        成功：{"code":0}
        失败：{"code":1}
    */

    if (ret == 0) {
        return (0);
    } else {
        return (1);
    }
}


int ApiDealfile(string &url, string &post_data, string &str_json) {
    char cmd[20];
    string user_name;
    string token;
    string md5;      //文件md5码
    string filename; //文件名字
    int ret = 0;
    //解析命令
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    LOG_INFO << "cmd = " <<  cmd;

    if (strcmp(cmd, "share") == 0) //分享文件
    {
        // 反序列化
        if(decodeDealfileJson(post_data, user_name, token, md5, filename) < 0) {
            encodeDealfileJson(1, str_json);
            return -1;
        }
        //token校验  
        //处理分享逻辑   序列化
        ret = handleShareFile(user_name , md5, filename);
        encodeDealfileJson(ret, str_json);
    } else if (strcmp(cmd, "del") == 0) //删除文件
    {
        // 反序列化
        if(decodeDealfileJson(post_data, user_name, token, md5, filename) < 0) {
            encodeDealfileJson(1, str_json);
            return -1;
        }
         //token校验 
         ret = handleDeleteFile(user_name, md5, filename);
         encodeDealfileJson(ret, str_json);
    } else if (strcmp(cmd, "pv") == 0) //文件下载标志处理
    {
        // 反序列化
        if(decodeDealfileJson(post_data, user_name, token, md5, filename) < 0) {
            encodeDealfileJson(1, str_json);
            return -1;
        }
        ret = handlePvFile(user_name, md5, filename);
       encodeDealfileJson(ret, str_json);
    }else {
        LOG_ERROR << "un handle cmd " << cmd;
    }
    return 0;
}