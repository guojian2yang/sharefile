#include "shorturl_web_service.h"
#include "short_rpc.h"  // 用于调用 RPC 解析短链
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <json/json.h>
#include <iostream>
#include "shorturl_rpc.pb.h"

#define PROTOBUF_ENABLE 1
#define JSON_ENABLE 0

// 短链服务主函数（需实现 HTTP 监听和请求处理）
void RunShortUrlWebService() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // 创建 socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return;
    }

    // 设置 socket 选项
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        return;
    }

    // 绑定端口（假设短链服务监听 8082 端口）
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8082);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return;
    }

    // 监听连接
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        return;
    }

    std::cout << "Short URL Web Service listening on port 8082" << std::endl;

    // 处理客户端请求
    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        // 读取 HTTP 请求（简化处理）
        char buffer[1024] = {0};
        read(new_socket, buffer, 1024);

        // 解析短链标识（示例：从路径 /p/abc123 提取 abc123）
        std::string request(buffer);
        std::cout << "request: " << request << std::endl;
        size_t path_start = request.find("GET /p/") + 7;
        size_t path_end = request.find(" ", path_start);
        std::string short_key = request.substr(path_start, path_end - path_start);
        // 调用 RPC 解析短链（使用现有 RPC 客户端逻辑）
        #if PROTOBUF_ENABLE
        shorturl_rpc::ResolveShortUrlRequest rpc_request;
        rpc_request.set_short_url("http://192.168.31.43/p/" + short_key);
        std::string rpc_request_str;
        rpc_request.SerializeToString(&rpc_request_str);
        std::string rpc_response = handleresolveRequest(rpc_request_str); // 复用现有 RPC 处理函数

        shorturl_rpc::ResolveShortUrlResponse rpc_pb_response;
        if (rpc_pb_response.ParseFromString(rpc_response) && rpc_pb_response.code() == 0) {
            std::string full_url = rpc_pb_response.full_url();
            std::string http_response = "HTTP/1.1 302 Found\r\nLocation: " + full_url + "\r\nContent-Length: 0\r\n\r\n";
            send(new_socket, http_response.c_str(), http_response.length(), 0);
        } else {
            std::string http_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(new_socket, http_response.c_str(), http_response.length(), 0);
        }
#elif JSON_ENABLE
        Json::Value rpc_request;
        rpc_request["method"] = "ResolveShortUrl";
        rpc_request["params"]["short_url"] = "http://192.168.31.43/p/" + short_key;
        Json::FastWriter writer;
        std::string rpc_request_str = writer.write(rpc_request);
        std::string rpc_response = handleRequest(rpc_request_str); // 复用现有 RPC 处理函数

        // 解析 RPC 响应并返回重定向
        Json::Value rpc_json;
        Json::Reader reader;
        if (reader.parse(rpc_response, rpc_json) && rpc_json["code"].asInt() == 0) {
            std::string full_url = rpc_json["full_url"].asString();
            std::string http_response = "HTTP/1.1 302 Found\r\nLocation: " + full_url + "\r\nContent-Length: 0\r\n\r\n";
            send(new_socket, http_response.c_str(), http_response.length(), 0);
        } else {
            std::string http_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(new_socket, http_response.c_str(), http_response.length(), 0);
        }
#endif

        close(new_socket);
    }
}
