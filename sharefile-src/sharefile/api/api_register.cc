#include "api_register.h"
#include <iostream>

#include <json/json.h>
#include "muduo/base/Logging.h" // Logger日志头文件

using namespace std;

int encdoeRegisterJson(int code, string &str_json) {
    Json::Value root;
    root["code"] = code;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int decodeRegisterJson(const std::string &str_json, string &user_name,
                       string &nick_name, string &pwd, string &phone,
                       string &email)
{   
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);    
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }
    // 用户名
    if (root["userName"].isNull()) {
        LOG_ERROR << "userName null";
        return -1;
    }
    user_name = root["userName"].asString();

    // 昵称
    if (root["nickName"].isNull()) {
        LOG_ERROR << "nickName null";
        return -1;
    }
    nick_name = root["nickName"].asString();

    //密码
    if (root["firstPwd"].isNull()) {
        LOG_ERROR << "firstPwd null";
        return -1;
    }
    pwd = root["firstPwd"].asString();

     //电话  非必须
    if (root["phone"].isNull()) {
        LOG_WARN << "phone null";
    } else {
        phone = root["phone"].asString();
    }

    //邮箱 非必须
    if (root["email"].isNull()) {
        LOG_WARN << "email null";
    } else {
        email = root["email"].asString();
    }
    return 0;

}
/*
* 先根据用户名查询数据库该用户是否存在，不存在才插入
*/
int registerUser(string &user_name, string &nick_name, string &pwd,
                 string &phone, string &email) {
    int ret = 0; //ret = 2 用户已经存在  = 1注册异常  =0注册成功
    uint32_t user_id = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);
    if(!db_conn) {
        LOG_ERROR << "GetDBConn(tuchuang_slave) failed" ;
        return 1;
    }
    //查询数据库是否存在
    string str_sql = FormatString("select id from user_info where user_name='%s'", user_name.c_str());
    CResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if(result_set && result_set->Next()) {
        LOG_WARN << "id: " << result_set->GetInt("id") << ", user_name: " <<  user_name <<  "  已经存在";
        delete result_set;
        ret = 2; //已经存在对应的用户名
    } else {
        time_t now;
        char create_time[TIME_STRING_LEN];
        //获取当前时间
        now = time(NULL);
        strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S", localtime(&now)); // unix格式化后的时间
        str_sql = "insert into user_info "
                 "(`user_name`,`nick_name`,`password`,`phone`,`email`,`create_"
                 "time`) values(?,?,?,?,?,?)";
        LOG_INFO << "执行: " <<  str_sql;
         // 预处理方式写入数据
        CPrepareStatement *stmt = new CPrepareStatement();
        if (stmt->Init(db_conn->GetMysql(), str_sql)) {
            uint32_t index = 0;
            string c_time = create_time;
            stmt->SetParam(index++, user_name);
            stmt->SetParam(index++, nick_name);
            stmt->SetParam(index++, pwd);
            stmt->SetParam(index++, phone);
            stmt->SetParam(index++, email);
            stmt->SetParam(index++, c_time);
            bool bRet = stmt->ExecuteUpdate(); //真正提交要写入的数据
            if (bRet) {     //提交正常返回 true
                ret = 0;
                user_id = db_conn->GetInsertId();   
                LOG_INFO << "insert user_id: " <<  user_id <<  ", user_name: " <<  user_name ;
            } else {
                LOG_ERROR << "insert user_info failed. " <<  str_sql;
                ret = 1;
            }
        }
        delete stmt;
    }

    return ret;
}


int ApiRegisterUser(string &post_data, string &resp_json) {

    int ret = 0;
    string user_name;
    string nick_name;
    string pwd;
    string phone;
    string email;

    LOG_INFO << "post_data: " <<  post_data;


    
    // 判断数据是否为空
    if (post_data.empty()) {
        LOG_ERROR << "decodeRegisterJson failed";
        //序列化 把结果返回给客户端
        // code = 1
        encdoeRegisterJson(1, resp_json);
        return -1;
    }
    // json反序列化

    ret = decodeRegisterJson(post_data, user_name, nick_name, pwd, phone, email);
    if(ret < 0) {
        encdoeRegisterJson(1, resp_json);
        return -1;
    }

    // 注册账号
    // 先在数据库查询用户名 昵称 是否存在 如果不存在才去注册
    ret = registerUser(user_name, nick_name, pwd, phone, email); //先不操作数据库看看性能

    encdoeRegisterJson(ret, resp_json);

    return 0;
}