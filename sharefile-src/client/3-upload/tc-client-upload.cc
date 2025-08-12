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
#include <filesystem>  // 新增头文件，用于文件系统操作
#include <mutex>       // 引入互斥锁头文件
#include <ctime>       // 用于初始化随机数种子

#define   TC_HTTP_SERVER_IP "192.168.31.43"
// #define   TC_HTTP_SERVER_IP "127.0.0.1"

// 定义 5 个用户信息
#define   USER_NAMES {"guojian", "wenhao", "yukun", "junxi", "xinxin"}
#define   USER_PWDS  {"123456", "123456", "123456", "123456", "123456"}

// 3-upload 目录路径
#define   UPLOAD_DIR "3-upload"

using namespace std;
namespace fs = std::filesystem;

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
    void SetToken(const string& token) {
        token_ = token;
    }
    const string& GetName() const {
        return name_;
    }
    const string& GetPassword() const {
        return password_;
    }
    const string& getToken() const {
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

    size_t GetFileSize() const {
        return file_content_.length();
    }
    const string& GetFileName() const {
        return file_name_;
    }

    const string& GetFileContent() const {
        return file_content_;
    }

    const string& GetFileMd5() const {
        return file_md5_;
    }
private:
    string full_path_;  //文件完整路径名
    string file_name_;  //单纯的文件名
    size_t  file_size_ = 0; //文件大小
    std::string file_content_;
    string file_md5_;
};


int decodeLoginResponse(const string &str_json, int &code, string &token) {
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
int encodeMyfilesRequest(const string& user, const string& token, int start, int count, string &str_json) {
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
    srand(time(NULL)); // 初始化随机数种子
    if (argc < 2) {
        LOG_ERROR << "请提供线程数作为命令行参数";
        return 1;
    }
    int threadCount = atoi(argv[1]);
    if (threadCount <= 0) {
        LOG_ERROR << "线程数必须为正整数";
        return 1;
    }

    if (!fs::exists(UPLOAD_DIR) || !fs::is_directory(UPLOAD_DIR)) {
        LOG_ERROR << "指定的目录 " << UPLOAD_DIR << " 不存在，请检查路径。";
        return 1;
    }

    vector<string> filePaths;
    int fileCount = 0;
    for (const auto& entry : fs::directory_iterator(UPLOAD_DIR)) {
        if (entry.is_regular_file() && fileCount < 10) {
            filePaths.push_back(entry.path().string());
            ++fileCount;
        }
    }

    if (filePaths.size() < 10) {
        LOG_ERROR << UPLOAD_DIR << " 目录下文件不足 10 个";
        return 1;
    }

    const vector<string> userNames = USER_NAMES;
    const vector<string> userPasswords = USER_PWDS;
    vector<User> users;

    // 多个用户登录
    for (size_t i = 0; i < userNames.size(); ++i) {
        httplib::Client cli = httplib::Client{TC_HTTP_SERVER_IP, 80};
        User user(userNames[i], userPasswords[i]);

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
        if (res) {
            LOG_INFO << "用户 " << user.GetName() << " status:" <<  res->status;
            LOG_INFO << "用户 " << user.GetName() << " body:" << res->body;
        } else {
            LOG_ERROR << "用户 " << user.GetName() << " 登录请求失败，可能是网络问题或超时";
            continue;
        }

        if(res->body.empty()) {
            LOG_ERROR << "用户 " << user.GetName() << " res->body is empty, no data can parse";
            continue;
        }

        int code = 1;
        string token;
        int ret = decodeLoginResponse(res->body, code, token);
        if(ret < 0 ) {
            LOG_ERROR << "用户 " << user.GetName() << " res->body parse failed, body: " << res->body;
            continue;
        }
        if(code != 0) {
            LOG_ERROR << "用户 " << user.GetName() << " login verify failed, code: " << code;
            continue;
        }
        user.SetToken(token);
        users.push_back(user);
    }

    if (users.empty()) {
        LOG_ERROR << "没有用户登录成功";
        return 1;
    }

    // 多线程上传
    std::vector<std::thread> userThreads;
    uint64_t start_time = getMs();
    std::atomic<size_t> totalSize(0); // 原子变量用于统计总上传大小
    std::mutex logMutex;

    for (const auto& user : users) {
        userThreads.emplace_back([user, filePaths, start_time, &totalSize, threadCount, &logMutex]() {
            std::vector<std::thread> fileThreads;
            for (const auto& filePath : filePaths) {
                for (int i = 0; i < threadCount; ++i) {
                    fileThreads.emplace_back([user, filePath, start_time, &totalSize, &logMutex]() {
                        {
                            std::lock_guard<std::mutex> lock(logMutex);
                            LOG_INFO << "用户 " << user.GetName() << " 开始上传文件 " << filePath;
                        }

                        FileUpload file_upload(filePath);
                        if (!file_upload.ReadAllContent()) {
                            std::lock_guard<std::mutex> lock(logMutex);
                            LOG_ERROR << "用户 " << user.GetName() << " 读取文件 " << filePath << " 失败";
                            return;
                        }
                        if (!file_upload.CalMd5()) {
                            std::lock_guard<std::mutex> lock(logMutex);
                            LOG_ERROR << "用户 " << user.GetName() << " 计算文件 " << filePath << " MD5 失败";
                            return;
                        }

                        httplib::Client cli2 = httplib::Client{TC_HTTP_SERVER_IP, 80};
                        httplib::MultipartFormDataItems items = {
                            {"file", file_upload.GetFileContent(), file_upload.GetFileName(), "multipart/form-data"},
                            {"user", user.GetName(), "", ""},
                            {"md5", file_upload.GetFileMd5(), "", ""},
                            {"size", std::to_string(file_upload.GetFileSize()), "", ""}
                        };

                        httplib::Result res2 = cli2.Post("/api/upload", items);
                        uint64_t need_time = getMs() - start_time;

                        if (!res2) {
                            std::lock_guard<std::mutex> lock(logMutex);
                            LOG_ERROR << "用户 " << user.GetName() << " 上传文件 " << filePath << " 请求失败";
                            return;
                        }

                        if (res2->status != 200) {
                            std::lock_guard<std::mutex> lock(logMutex);
                            LOG_ERROR << "用户 " << user.GetName() << " 上传文件 " << filePath << " 失败，状态码: " << res2->status;
                            return;
                        }

                        {
                            std::lock_guard<std::mutex> lock(logMutex);
                            LOG_INFO << "用户 " << user.GetName() << " 上传文件 " << filePath << " status:" <<  res2->status;
                            LOG_INFO << "用户 " << user.GetName() << " 上传文件 " << filePath << " body:" << res2->body;
                        }

                        float bps = 0;
                        if(need_time > 0) 
                            bps = 1.0 * file_upload.GetFileSize() / (need_time / 1000.0) / 1000.0 * 8;
                        {
                            std::lock_guard<std::mutex> lock(logMutex);
                            LOG_INFO <<"用户 " << user.GetName() << " 上传文件 " << filePath << " 完成, upload: " << file_upload.GetFileSize() << " bytes,  t: "<< need_time << "ms, bps = " << bps << "kbps\n"; 
                        }
                        totalSize += file_upload.GetFileSize();

                        {
                            std::lock_guard<std::mutex> lock(logMutex);
                            LOG_INFO << "用户 " << user.GetName() << " 结束上传文件 " << filePath;
                        }
                    });
                }
            }

            // 等待该用户的所有文件上传线程结束
            for (auto& th : fileThreads) {
                if (th.joinable()) {
                    th.join();
                }
            }
            {
                std::lock_guard<std::mutex> lock(logMutex);
                LOG_INFO << "用户 " << user.GetName() << " 完成上传所有文件";
            }
        });
    }

    // 等待所有用户线程结束
    for (auto& th : userThreads) {
        if (th.joinable()) {
            th.join();
        }
    }

    uint64_t need_time = getMs() - start_time;
    float bps = 0;
    if(need_time > 0) 
        bps = 1.0 * static_cast<float>(totalSize) / (need_time / 1000.0) / 1000.0 * 8;
    LOG_INFO <<"上传并发数: " << threadCount * users.size() << ",  任务完成, upload: " << totalSize << " bytes,  t: "<< need_time << "ms, bps = " << std::floor(bps) << "kbps\n"; 

    return 0;
}