// 支持用户名登录上传指定的文件
// 因为这个代码目的是测试上传性能，所以这里不会做md5秒传的请求
// 我们使用同一个文件进行上传测试，测试结果返回code:1 不影响上传性能，
// 因为文件已经走完了上传流程，只是在file_info写入失败(因为md5这里要作为唯一值)
#include "httplib.h"
#include <iostream>
#include <fstream>
#include <sys/time.h>
#include "muduo/base/md5.h"
#include "muduo/base/Logging.h"

#include <json/json.h> // jsoncpp头文件

#define   TC_HTTP_SERVER_IP "192.168.31.43"
// #define   TC_HTTP_SERVER_IP "127.0.0.1"
#define   USER_NAME "guojian"        //用户名
#define   USER_PWD  "123456"        //密码
#define   CONCURRENT   1           //并发的上传线程，就是客户端一次允许多少个任务上传    

// 使用truncate命令生成测试文件, 比如以下命令生成25M的文件，文件名为testfile.d
// truncate -s 25M testfile.d
#define   FULL_PATH "testfile.d"     //测试上传的文件全名路径，可以是相对路径

using namespace std;
// 随机字符串
static string RandomString(const int len) /*参数为字符串的长度*/
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

class User {
public:
    User(string name, string password) {
        name_ = name;
        MD5 md5(password);
        password_ = md5.toString();
        LOG_INFO << "name: " << name_ << ", password: " << password_;
    }
    void SetToken(string &token) {
        token_ = token;
    }
    string &GetName() {
        return name_;
    }
    string &GetPassword() {
        return password_;
    }
    string &getToken() {
        return token_;
    }
private:
    string name_;           //用户名
    string password_;       //密码
    string token_;
    string  file_full_path_;    //文件路径
};

//注意，这里是需要将文件内容读取到内存，所以注意文件的大小，不要去读取那种几个G的文件
class FileUpload 
{
public:
    FileUpload(string full_path) 
        :full_path_(full_path)
    {   //传入文件路径
        size_t pos = full_path_.find_last_of('/');  // 对于Linux等系统路径分隔符是'/'
        if (pos == std::string::npos) {
            pos = full_path_.find_last_of('\\');  // 对于Windows系统路径分隔符是'\\'
        }
        if (pos == std::string::npos) {  //说明当前的文件路径并没有/或者\\ , 即他只是一个文件名而已
            file_name_ = full_path_;
        } else {
            file_name_ = full_path_.substr(pos + 1);
        }
        LOG_INFO << "full_path: " << full_path << ", file_name: "<< file_name_;
    }
    //将文件内容读取到内存
    bool ReadAllContent() {
        // 以二进制模式打开文件
        std::ifstream file(full_path_, std::ios::in | std::ios::binary);
        if (!file) {
            // 打开文件失败，输出错误信息并返回空字符串
            LOG_ERROR << "无法打开文件: " << full_path_;
            return false;
        }

        // 将文件指针移动到文件末尾，获取文件大小
        file.seekg(0, std::ios::end);
        std::streamsize fileSize = file.tellg();
        file_size_ = fileSize;
        // 将文件指针移回文件开头
        file.seekg(0, std::ios::beg);

        // 根据文件大小调整字符串的大小，预留足够的空间
        std::string content;
        file_content_.resize(fileSize);

        // 读取文件内容到字符串中
        file.read(&file_content_[0], fileSize);

        // 关闭文件
        file.close();

        if(file_content_.length() != file_size_) {
            LOG_ERROR << "read no finish";
            return false;
        }
        return true;
    }
    // 需要在文件读取完毕后再继续md5，其实也可以便读取文件边计算
    bool CalMd5() {
        if(file_content_.empty()) {
            LOG_ERROR << "file_content_ empty";
            return false;
        }
        MD5 md5(file_content_);
        file_md5_ = md5.toString();
        return true;
    }

    size_t GetFileSize() {
        return file_content_.length();
    }
    string &GetFileName() {
        return file_name_;
    }

    string &GetFileContent() {
        return file_content_;
    }

    string &GetFileMd5() {
        return file_md5_;
    }
private:
    string full_path_;  //文件完整路径名
    string file_name_;  //单纯的文件名
    size_t  file_size_ = 0; //文件大小
    std::string file_content_;
    string file_md5_;
};


int decodeLoginRespone(string &str_json, int &code, string &token) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse reg json failed ";
        return -1;
    }
  
    if (root["code"].isNull()) {
        LOG_ERROR << "code null";
        return -1;
    }
    code = root["code"].asInt();
 
    if (root["token"].isNull()) {
        LOG_ERROR << "token null";
        return -1;
    }
    token = root["token"].asString();

    return 0;
}

// 封装我的文件请求的参数
int encodeMyfilesRequest(string user, string token, int start, int count, string &str_json) {
    Json::Value root;
    root["user"] = user;
    root["token"] = token;
    root["start"] = start;
    root["count"] = count;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

static uint64_t getMicroseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long microseconds = tv.tv_sec * 1000000LL + tv.tv_usec;
    return microseconds;
}  

static uint64_t getMs() {
    return getMicroseconds()/1000;
}  




int main(int argc,char *argv[]) {

    httplib::Client cli = httplib::Client{TC_HTTP_SERVER_IP, 80};
    int concurrent = CONCURRENT;
    User user(USER_NAME, USER_PWD);
    if(argc >= 2) {
        concurrent = atoi(argv[1]);
    }
    LOG_INFO << "用户名: " << USER_NAME << ", 密码: " << USER_PWD;
    LOG_INFO << "客户端并发数量: " << concurrent << ", 文件全路径: " << FULL_PATH;
    LOG_INFO << "服务器地址: " << TC_HTTP_SERVER_IP << ":" << 80;
    //请求登录
    //封装请求
    Json::Value root;
    root["user"] = user.GetName();
    root["pwd"] = user.GetPassword();
  
    Json::FastWriter writer;
    string req_json = writer.write(root);

    // 创建header
    httplib::Headers headers = {
        { "content-type", "application/json" }
    };

    httplib::Result res = cli.Post("/api/login", headers, req_json, "application/json");
    LOG_INFO << "status:" <<  res->status;
    LOG_INFO << "body:" << res->body;

    if(res->body.empty()) {
        LOG_ERROR << "res->body is empty, no data can parse";
        exit(1);
    }
    //解析返回结果 code和token
    int code = 1;
    string token;
    int ret = decodeLoginRespone(res->body, code, token);
    if(ret < 0 ) {
        LOG_ERROR << "res->body parse failed";
        exit(1);
    }
    if(code != 0) {
        LOG_ERROR << "login verify failed";
        exit(1);
    }
    user.SetToken(token);   //存储token

    // 获取我的文件
    //封装请求
    encodeMyfilesRequest(user.GetName(), user.getToken(), 0, 10, req_json);
    res = cli.Post("/api/myfiles&cmd=normal", headers, req_json, "application/json");
    LOG_INFO << "status:" <<  res->status;
    LOG_INFO << "body:" << res->body;
    ///api/myfiles&cmd=normal

    

    //测试文件上传
    
    // 读取文件内容，测试25M的文件
   
    FileUpload file_upload(FULL_PATH);
    if(!file_upload.ReadAllContent()) {     //读取文件的全部内容到内存里
        LOG_ERROR << "read file failed";
        exit(1);
    }
    if(!file_upload.CalMd5()) { //计算文件内存的MD5
        LOG_ERROR << "cal file md5 failed";
        exit(1);
    }



    // 多线程上传，计算每个线程的上传速率， 然后综合计算整体的上传效率
    // 存储线程对象的向量
    std::vector<std::thread> threads;

    
    uint64_t start_time = getMs();
    // 创建并启动5个线程，每个线程执行一个简单的lambda任务
    for (int i = 0; i < concurrent; ++i) {
        threads.push_back(std::thread([i,start_time,&user, &file_upload]() {
            httplib::MultipartFormDataItems items = {
                {"file", file_upload.GetFileContent(), file_upload.GetFileName(), "multipart/form-data"},   //【0】
                {"user", user.GetName(), "", ""},                       //【1】
                // {"md5", file_upload.GetFileMd5(), "", ""},                       //【2】
                // 但其实这里修改并没有作用，因为nginx upload插件 有重新计算md5值，除非改成md5字段透传模式, 但即是返回code=1不影响上传性能的测试
                 {"md5", RandomString(32), "", ""},                       //【2】//这里是md5的值，我们先故意修改成不同的值，因为server端的file_info表只允许唯一的md5值。
                {"size", std::to_string(file_upload.GetFileSize()), "", ""}     //【3】
            };
            httplib::Client cli2 = httplib::Client{TC_HTTP_SERVER_IP, 80};
            httplib::Result res2 = cli2.Post("/api/upload", items);
        
            uint64_t need_time = getMs() - start_time;
            LOG_INFO << "status:" <<  res2->status;
            LOG_INFO << "body:" << res2->body;

            float bps = 0;
            if(need_time > 0) 
                bps = 1.0* file_upload.GetFileSize()/(need_time/1000.0) / 1000.0 *8;  // 单位kbps
            LOG_INFO <<"线程 " << i << " 完成任务 upload: " << file_upload.GetFileSize() << " bytes,  t: "<< need_time << "ms, bps = " << bps << "kbps\n"; 
        }));
    }

    // 遍历线程向量，调用join方法等待每个线程结束
    for (auto& th : threads) {
        if (th.joinable()) {
            th.join();
        }
    }
    uint64_t need_time = getMs() - start_time;
    float bps = 0;
    if(need_time > 0) 
        bps = 1.0* file_upload.GetFileSize()*concurrent /(need_time/1000.0) / 1000.0 *8;  // 单位kbps
    LOG_INFO <<"上传并发数: " << concurrent << ",  任务完成, upload: " << file_upload.GetFileSize()*concurrent << " bytes,  t: "<< need_time << "ms, bps = " << std::floor(bps) << "kbps\n"; 

    return 0;
}
