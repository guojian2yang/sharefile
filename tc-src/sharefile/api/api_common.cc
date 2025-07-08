#include "api_common.h"

string s_dfs_path_client;
// string s_web_server_ip;
// string s_web_server_port;
string s_storage_web_server_ip;
string s_storage_web_server_port;
string s_shorturl_server_address;
string s_shorturl_server_access_token;
 

//验证登陆token，成功返回0，失败-1
int VerifyToken(string &user_name, string &token) {
    int ret = 0;
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    if (cache_conn) {
        string temp_user_name  = cache_conn->Get(token);    //校验token和用户名的关系
        if (temp_user_name == user_name) {
            ret = 0;
        } else {
            ret = -1;
        }
    } else {
        ret = -1;
    }

    return ret;
}

int CacheSetCount(CacheConn *cache_conn, string key, int64_t count) {
    string ret = cache_conn->Set(key, std::to_string(count));
    if (!ret.empty()) {
        return 0;
    } else {
        return -1;
    }
}
int CacheGetCount(CacheConn *cache_conn, string key, int64_t &count) {
    count = 0;
    string str_count = cache_conn->Get(key);
    if (!str_count.empty()) {
        count = atoll(str_count.c_str());
        return 0;
    } else {
        return -1;
    }
}

int CacheIncrCount(CacheConn *cache_conn, string key) {
    int64_t count = 0;
    int ret = cache_conn->Incr(key, count);
    if (ret < 0) {
        return -1;
    }
    LOG_INFO << key << "-" << count;
    
    return 0;
}
// 这里最小我们只允许为0
int CacheDecrCount(CacheConn *cache_conn, string key) {
    int64_t count = 0;
    int ret = cache_conn->Decr(key, count);
    if (ret < 0) {
        return -1;
    }
    LOG_INFO << key << "-" << count;
    if (count < 0) {
        LOG_ERROR << key << "请检测你的逻辑 decr  count < 0  -> " << count;
        ret = CacheSetCount(cache_conn, key, 0); // 文件数量最小为0值
        if (ret < 0) {
            return -1;
        }
    }
    return 0;
}


//处理数据库查询结果，结果集保存在count，如果要读取count值则设置为0，如果设置为-1则不读取
//返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
int GetResultOneCount(CDBConn *db_conn, char *sql_cmd, int &count) {
    int ret = -1;
    CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);

    if (!result_set) {
        ret = -1;
    }

    if (count == 0) {
        // 读取
        if (result_set->Next()) {
            ret = 0;
            // 存在在返回
            count = result_set->GetInt("count");
            LOG_INFO << "count: " << count;
        } else {
            ret = 1; // 没有记录
        }
    } else {
        if (result_set->Next()) {
            ret = 2;
        } else {
            ret = 1; // 没有记录
        }
    }

    delete result_set;

    return ret;
}

int CheckwhetherHaveRecord(CDBConn *db_conn, char *sql_cmd) {
    int ret = -1;
    CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);

    if (!result_set) {
        ret = -1;
    } else if (result_set && result_set->Next()) {
        ret = 1;
    } else {
        ret = 0;
    }

    delete result_set;

    return ret;
}

int GetResultOneStatus(CDBConn *db_conn, char *sql_cmd, int &shared_status) {
    int ret = 0;
    CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);

    if (!result_set) {
        LOG_ERROR << "result_set is NULL";
        ret = -1;
    }

    if (result_set->Next()) {
        ret = 0;
        // 存在在返回
        shared_status = result_set->GetInt("shared_status");
        LOG_INFO << "shared_status: " << shared_status;
    } else {
        LOG_ERROR<< "result_set->Next() is NULL";
        ret = -1;
    }

    delete result_set;

    return ret;
}
//获取用户文件个数
int DBGetUserFilesCountByUsername(CDBConn *db_conn, string user_name, int &count) {
    count = 0;
    int ret = 0;
    // 先查看用户是否存在
    string str_sql;

    str_sql = FormatString("select count(*) from user_file_list where user='%s'",
                     user_name.c_str());
    LOG_INFO << "执行: " << str_sql;
    CResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set && result_set->Next()) {
        // 存在在返回
        count = result_set->GetInt("count(*)");
        LOG_INFO << "count: " << count;
        ret = 0;
        delete result_set;
    } else if (!result_set) { // 操作失败
        LOG_ERROR << "操作失败" << str_sql;
        LOG_ERROR << "操作失败" << str_sql;
        ret = -1;
    } else {
        // 没有记录则初始化记录数量为0
        ret = 0;
        LOG_INFO << "没有记录: count: " << count;
    }
    return ret;
}

/**
 * @brief  解析url query 类似 abc=123&bbb=456 字符串
 *          传入一个key,得到相应的value
 * @returns
 *          0 成功, -1 失败
 */
int QueryParseKeyValue(const char *query, const char *key, char *value,
                       int *value_len_p) {
    char *temp = NULL;
    char *end = NULL;
    int value_len = 0;

    //找到是否有key
    temp = (char *)strstr(query, key);
    if (temp == NULL) {
        return -1;
    }

    temp += strlen(key); //=
    temp++;              // value

    // get value
    end = temp;

    while ('\0' != *end && '#' != *end && '&' != *end) {
        end++;
    }

    value_len = end - temp;

    strncpy(value, temp, value_len);
    value[value_len] = '\0';

    if (value_len_p != NULL) {
        *value_len_p = value_len;
    }

    return 0;
}


//通过文件名file_name， 得到文件后缀字符串, 保存在suffix
//如果非法文件后缀,返回"null"
int GetFileSuffix(const char *file_name, char *suffix) {
    const char *p = file_name;
    int len = 0;
    const char *q = NULL;
    const char *k = NULL;

    if (p == NULL) {
        return -1;
    }

    q = p;

    // mike.doc.png
    //              ↑

    while (*q != '\0') {
        q++;
    }

    k = q;
    while (*k != '.' && k != p) {
        k--;
    }

    if (*k == '.') {
        k++;
        len = q - k;

        if (len != 0) {
            strncpy(suffix, k, len);
            suffix[len] = '\0';
        } else {
            strncpy(suffix, "null", 5);
        }
    } else {
        strncpy(suffix, "null", 5);
    }

    return 0;
}

/**
 * @brief  去掉一个字符串两边的空白字符
 *
 * @param inbuf确保inbuf可修改
 *
 * @returns
 *      0 成功
 *      -1 失败
 */
int TrimSpace(char *inbuf) {
    int i = 0;
    int j = strlen(inbuf) - 1;

    char *str = inbuf;

    int count = 0;

    if (str == NULL) {
        return -1;
    }

    while (isspace(str[i]) && str[i] != '\0') {
        i++;
    }

    while (isspace(str[j]) && j > i) {
        j--;
    }

    count = j - i + 1;

    strncpy(inbuf, str + i, count);

    inbuf[count] = '\0';

    return 0;
}

// 这个随机字符串是有问题的
string RandomString(const int len) /*参数为字符串的长度*/
{
    /*初始化*/
    string str; /*声明用来保存随机字符串的str*/
    char c;     /*声明字符c，用来保存随机生成的字符*/
    int idx;    /*用来循环的变量*/
    /*循环向字符串中添加随机生成的字符*/
    for (idx = 0; idx < len; idx++) {
        /*rand()%26是取余，余数为0~25加上'a',就是字母a~z,详见asc码表*/
        c = 'a' + rand() % 26;
        str.push_back(c); /*push_back()是string类尾插函数。这里插入随机字符c*/
    }
    return str; /*返回生成的随机字符串*/
}


//从storage删除指定的文件，参数为文件id
int RemoveFileFromFastDfs(const char *fileid) {
    int ret = 0;

    char cmd[1024 * 2] = {0};
    sprintf(cmd, "fdfs_delete_file %s %s", s_dfs_path_client.c_str(), fileid);

    ret = system(cmd);
    LOG_INFO << "RemoveFileFromFastDfs ret = " << ret;

    return ret;
}


//获取用户共享图片的数量
int DBGetSharePictureCountByUsername(CDBConn *db_conn, string user_name, int &count) {
    count = 0;
    int ret = 0;
    // 先查看用户是否存在
    string str_sql;

    str_sql = FormatString("select count(*) from share_picture_list where user='%s'",
                     user_name.c_str());
    LOG_INFO << "执行: " << str_sql;
    CResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set && result_set->Next()) {
        // 存在在返回
        count = result_set->GetInt("count(*)");
        LOG_INFO << "count: " << count;
        ret = 0;
        delete result_set;
    } else if (!result_set) { // 操作失败
        LOG_ERROR << str_sql << " 操作失败";
        ret = -1;
    } else {
        // 没有记录则初始化记录数量为0
        ret = 0;
        LOG_INFO << "没有记录: count: " << count;
    }
    return ret;
}