#ifndef __HTTP_CONN_H__
#define __HTTP_CONN_H__
#include "http_parser_wrapper.h"
#include "muduo/net/TcpConnection.h"
#include "muduo/net/Buffer.h"

using namespace muduo;
using namespace muduo::net;
using namespace std;
class CHttpConn :  public std::enable_shared_from_this<CHttpConn>
{
public:
    CHttpConn(TcpConnectionPtr tcp_conn);
    virtual ~CHttpConn(); 
    void OnRead(Buffer *buf);
private:
 // 账号注册处理
    int _HandleRegisterRequest(string &url, string &post_data);
    // 账号登陆处理
    int _HandleLoginRequest(string &url, string &post_data);

    int _HandleMd5Request(string &url, string &post_data);
    int _HandleUploadRequest(string &url, string &post_data);

    int _HandleMyFilesRequest(string &url, string &post_data);

    int _HandleSharepictureRequest(string &url, string &post_data);
    int _HandleDealfileRequest(string &url, string &post_data);   
    int _HandleSharefilesRequest(string &url, string &post_data); 
    int _HandleDealsharefileRequest(string &url, string &post_data);

    int _HandleHtml(string &url, string &post_data);
    int _HandleMemHtml(string &url, string &post_data);
    TcpConnectionPtr tcp_conn_;
    uint32_t uuid_ = 0;
     CHttpParserWrapper http_parser_;
};

using CHttpConnPtr = std::shared_ptr<CHttpConn>;

#endif