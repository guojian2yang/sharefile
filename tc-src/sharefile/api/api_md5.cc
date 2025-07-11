#include "api_md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

 enum Md5State {
    Md5Ok = 0,
    Md5Failed = 1,
    Md5TokenFaild = 4,
    Md5FileExit = 5,
};

int decodeMd5Json(string &str_json, string &user_name, string &token,
                  string &md5, string &filename) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse md5 json failed ";
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

int encodeMd5Json(int ret, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

//秒传处理
void handleDealMd5(const char *user, const char *md5, const char *filename,
                   string &str_json) {
    Md5State md5_state = Md5Failed;
    int ret = 0;
    int file_ref_count = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);


    //查看此用户是否已经有此文件，如果存在说明此文件已上传，无需再上传
    sprintf(sql_cmd,
            "select * from user_file_list where user = '%s' and md5 = '%s' "
            "and file_name = '%s'",
            user, md5, filename);
    LOG_INFO << "执行: " << sql_cmd;
    //返回值： 1: 表示已经存储了，有这个文件记录
    ret = CheckwhetherHaveRecord(db_conn, sql_cmd); // 检测个人是否有记录
    if (ret == 1) //如果有结果，说明此用户已经保存此文件
    {
        LOG_WARN << "user: " << user << "->  filename: " << filename << ", md5: " <<md5 << "已存在";
        md5_state = Md5FileExit; // 此用户已经有该文件了，不能重复上传
        goto END;
    }



      
    {
        ScopedFileInfoLock lock(FileInfoLock::GetInstance(), 1000);
        if (lock.IsLocked()) {
            // 修改file_info中的count字段，+1 （count  文件引用计数），多了一个用户拥有该文件
            sprintf(sql_cmd, "update file_info set count = count +1 where md5 = '%s'",  md5);
            LOG_INFO << "执行: " << sql_cmd;
            if (!db_conn->ExecutePassQuery(sql_cmd)) {
                LOG_ERROR << sql_cmd << " 操作失败";
                md5_state = Md5Failed; // 更新文件引用计数失败这里也认为秒传失败，宁愿他再次上传文件：1.该文件确实不存在; 2.数据库异常
                goto END;
            } else {
                LOG_INFO << "秒传失败";
                md5_state = Md5Failed;
                goto END;
            }
        } else {
            md5_state = Md5Failed;
            LOG_ERROR << "FileInfoLock TryLockFor" << "超时";
            goto END;
        }
    }
        

    //  user_file_list, 用户文件列表插入一条数据
    //当前时间戳
    struct timeval tv;
    struct tm *ptm;
    char time_str[128];

    //使用函数gettimeofday()函数来得到时间。它的精度可以达到微妙
    gettimeofday(&tv, NULL);
    ptm = localtime(
        &tv.tv_sec); //把从1970-1-1零点零分到当前时间系统所偏移的秒数时间转换为本地时间
    // strftime()
    // 函数根据区域设置格式化本地时间/日期，函数的功能将时间格式化，或者说格式化一个时间字符串
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);

    // 用户列表增加一个文件记录
    sprintf(sql_cmd,
            "insert into user_file_list(user, md5, create_time, file_name, "
            "shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)", user, md5, time_str, filename, 0, 0);
    LOG_INFO << "执行: " << sql_cmd;
    if (!db_conn->ExecuteCreate(sql_cmd)) {
        LOG_ERROR << sql_cmd << " 操作失败";
        md5_state = Md5Failed;
        // 恢复引用计数
        ScopedFileInfoLock lock(FileInfoLock::GetInstance(), 1000);
        if (lock.IsLocked()) {
            sprintf(sql_cmd, "update file_info set count = count-1 where md5 = '%s'", md5);
            LOG_INFO << "执行: " << sql_cmd;
            if (!db_conn->ExecutePassQuery(sql_cmd)) {
                LOG_ERROR << sql_cmd << " 操作失败";
            }
        } else {
             LOG_ERROR << "FileInfoLock TryLockFor" << "超时";
        }
        goto END;
    }

    //查询用户文件数量, 用户数量+1
    if (CacheIncrCount(cache_conn, FILE_USER_COUNT + string(user)) < 0) {
        LOG_WARN << "CacheIncrCount failed"; // 这个可以在login的时候从mysql加载
    }

    md5_state = Md5Ok;
 
END:
    /*
    秒传文件：
        秒传成功：  {"code": 0}
        秒传失败：  {"code":1}
        文件已存在：{"code": 5}
    */
    int code = (int)md5_state;
    encodeMd5Json(code, str_json);
}

int ApiMd5(string &post_data, string &str_json) {
    //解析json中信息
    /*
        * {
        user:xxxx,
        token: xxxx,
        md5:xxx,
        fileName: xxx
        }
        */
    string user;
    string md5;
    string token;
    string filename;
    int ret = 0;
    ret = decodeMd5Json(post_data, user, token, md5, filename); //解析json中信息
    if (ret < 0) {
        LOG_ERROR << "decodeMd5Json() err";
        encodeMd5Json((int)Md5Failed, str_json);
        return 0;
    }

    //验证登陆token，成功返回0，失败-1
    ret = VerifyToken(user, token); //
    if (ret == 0) {
        handleDealMd5(user.c_str(), md5.c_str(), filename.c_str(), str_json); //秒传处理
        return 0;
    } else {
        LOG_ERROR << "VerifyToken failed";
        encodeMd5Json(HTTP_RESP_FAIL, str_json); // token验证失败错误码
        return 0;
    }
}
