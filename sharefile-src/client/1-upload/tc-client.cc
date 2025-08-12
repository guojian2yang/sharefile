//
//  client.cc
//
//  Copyright (c) 2019 Yuji Hirose. All rights reserved.
//  MIT License
//

#include "httplib.h"
#include <iostream>
#include <fstream>
 
#include <sys/time.h>

static uint64_t getMicroseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long microseconds = tv.tv_sec * 1000000LL + tv.tv_usec;
    return microseconds;
}  

static uint64_t getMs() {
    return getMicroseconds()/1000;
}  

using namespace std;


int main(void) {


    const size_t kHttpChunkSize = 1024*16;
    // httplib::Client cli = httplib::Client{"localhost", 80};
        httplib::Client cli = httplib::Client{"localhost", 1234};

    //   httplib::Client cli = httplib::Client{"192.168.1.27", 80};
 
//     struct MultipartFormData {
//   std::string name;
//   std::string content;
//   std::string filename;
//   std::string content_type;
// };
#if 1
//换顺序 nginx也是能识别的，但自己写的程序不能识别
  httplib::MultipartFormDataItems items = {
        {"file", "123456789", "abcd.txt", "multipart/form-data"},
        {"user", "qingfu", "", ""},
        {"md5", "25f9e794323b453885f5181f1b624d0b", "", ""},
        {"size", "9", "", ""}
       
    };
    //  httplib::MultipartFormDataItems items = {
         
    //     {"user", "qingfu", "", ""},
    //     {"md5", "25f9e794323b453885f5181f1b624d0b", "", ""},
    //     {"size", "9", "", ""},
    //     {"file", "123456789", "abcd.txt", "multipart/form-data"}
    // };
    // 小文件一次性发送
    // httplib::MultipartFormData form;
    // form.name = "gcc-14.1.0.tar.gz";    //接收端收到的名字
    // form.content = "hello world";
    // // form.filename = "../httplib.h"; 
    // form.filename = "/home/lqf/3rd_src/gcc-14.1.0.tar.gz"; 
    
    // form.content_type = "multipart/form-data";
   
    // form_items.push_back(form);
    httplib::Result res = cli.Post("/api/upload", items);
#else
  
  httplib::MultipartFormDataProvider provider2;
    provider2.name = "user";
    provider2.filename = "";
    provider2.provider = [&](size_t offset, httplib::DataSink &sink){
        // offset 是已发送的偏移量
        const char arr[] = "darren";
        auto ret = sink.write(arr + offset, sizeof(arr));
        std::cerr << "Client write:" << arr << std::endl;
        // 发送完成
        sink.done();
        return !!ret;
    };
    // provider2.content_type = "multipart/form-data";
 

    // 大文件用流式接口
    httplib::MultipartFormDataProvider provider;
   
     provider.name = "file";    //接收端收到name的值
     // form.filename = "../httplib.h"; 
    provider.filename = "abcd.txt"; 
    std::ifstream input_file(provider.name, std::ios::binary);

    uint64_t start_time = getMs();
    provider.provider = [&](size_t offset, httplib::DataSink &sink){

        char buffer[kHttpChunkSize];  // 定义一个缓冲区，大小为1024字节，可以根据需求调整
        std::streamsize bytesRead;
        bytesRead = input_file.read(buffer, kHttpChunkSize).gcount();
        size_t ret = sink.write(buffer, bytesRead);    //返回真正写入的数量？
         std::cout << "offset: " <<  offset << ", read: " << bytesRead << ", write: " << ret << std::endl;
        if(bytesRead < kHttpChunkSize) 
        {
            // 发送完成
            sink.done();

            // std::cout << bytesRead << ":" <<  kHttpChunkSize<< ", upload done\n";
            uint64_t need_time = getMs() - start_time;
            float bps = 0;
            if(need_time > 0) 
                bps = 1.0* (offset + bytesRead)/(need_time/1000.0) / 1000.0 *8;  // 单位kbps
            std::cout <<"t: "<< need_time << ", bps = " << bps << "kbps\n";
        }
         
        
        return !!ret;
    };
    provider.content_type = "multipart/form-data";
     httplib::MultipartFormDataProviderItems provider_items;
    // provider_items.push_back(provider2);
    provider_items.push_back(provider);
    httplib::MultipartFormDataItems items = {
        {"user", "qingfu", "", ""},
        {"md5", "25f9e794323b453885f5181f1b624d0b", "", ""},
        {"size", "9", "", ""}
    };



    // header 的 Content-Type 会默认设置为 multipart/form-data，且自动加上 boundary
    httplib::Result res = cli.Post("/api/upload", {}, items, provider_items);
#endif
    std::cerr << __FUNCTION__ << "\tpath:/api/upload\tres:" << (!!res) << std::endl;
    // printResult(res);

 

  return 0;
}
