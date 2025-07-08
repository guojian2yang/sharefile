//
//  upload.cc
//
//  Copyright (c) 2019 Yuji Hirose. All rights reserved.
//  MIT License
//

#include <fstream>
#include <httplib.h>
#include <iostream>



using namespace httplib;
using namespace std;
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
//   // <input type="file" name="image_file" accept="image/*">
  // <input type="file" name="text_file" accept="text/*">
    // <input type="file" name="user" content="qingfu">
  // <input type="file" name="md5" content="13f8870a8d1da09a66ce04f94b8c0740">
  // formData.append('file', formElem);
  //  {"user", "qingfu", "", ""},
        // {"md5", "25f9e794323b453885f5181f1b624d0b", "", ""},
        // {"size", "9", "", ""}
const char *html = R"(
<form id="formElem">

  <input type="file" name="file" accept="">
  <input type="submit">
</form>
<script>
  formElem.onsubmit = async (e) => {
    e.preventDefault();
    const fileInput = formElem.querySelector('input[type="file"]');
     const uploadParams = {
          user: "qingfu",
          md5: "25f9e794323b453885f5181f1b624d0b",
          size: 23232,
          file: fileInput.files[0]
      }
      let formData = new FormData();
       
      
      for(let param in uploadParams) {
          formData.append(param, uploadParams[param])
      }
      let res = await fetch('/api/upload', {
      method: 'POST',
      body: formData
    });
    console.log(await res.text());
  };
</script>
)";

int main(void) {
  Server svr;

  svr.Get("/", [](const Request & /*req*/, Response &res) {
    res.set_content(html, "text/html");
  });
#if 0
  svr.Post("/api/upload", [](const Request &req, Response &res, const httplib::ContentReader &content_reader) {
      
      if(req.is_multipart_form_data()){
        httplib::MultipartFormDataItems files;
        content_reader([&](const httplib::MultipartFormData &file){

                files.push_back(file);    //文件是一个一个上传的。
                return true;
        }, 
        [&](const char *data, size_t data_length){
              files.back().content.append(data, data_length);
              return true;
        });
        for(const auto& file : files){
            if(file.filename.empty()){
                std::cout << file.name << " : " << file.content << std::endl;
            }else{
                //C++ windows下utf-8转gbk
                // char* ansiStr = utf8_2_ansi(file.filename.c_str(), nullptr);
                 std::string fileName = file.filename;
                // free(ansiStr);
                // ansiStr = nullptr;

                std::ofstream ofs("upload-dir/" + fileName, std::ofstream::trunc | std::ostream::binary);
                if(ofs.is_open()){
                    std::cout << __FUNCTION__ << ":" << __LINE__ << " " << fileName  << " write " << file.content.size() << " bytes\n";
                    ofs.write(file.content.c_str(), file.content.size());
                    ofs.close();
                }
            }
        }
    }else{
        //这里好像并没有一个通用的请求头来标识上传文件的文件名,但是可以服务端和客户端约定一个请求头来标识上传的文件名
        std::string fileName = req.get_header_value("fileName");
        //C++ Windows环境下utf8转换gbk编码，才能正确打开文件
        // char* ansiStr = utf8_2_ansi(fileName.c_str(), nullptr);
        // fileName.assign(ansiStr);
        // free(ansiStr);
        // ansiStr = NULL;
        // std::string fileName = file.filename;
        std::ofstream ofs("upload-dir/" + fileName, std::ofstream::trunc | std::ofstream::binary);
        if(!ofs.is_open()){
            res.set_content(R"({"message":"open file failed"})", "appliation/json");
            return;
        }
        //里面的lambda函数可能会调用多次
        content_reader([&](const char *data, size_t data_length){
            std::cout << "data_length : " << data_length << std::endl;
            std::cout << __FUNCTION__ << ":" << __LINE__ << " " << fileName  << " write " << data_length << " bytes\n";
            ofs.write(data, data_length);
            return true;
        });
        ofs.close();
    }
    res.set_content(R"({"message":"upload successed"})", "appliation/json");
 
  });
#endif

  // 文件上传
  svr.Post("/api/upload", [](const httplib::Request &req, httplib::Response &res,
                         const httplib::ContentReader &content_reader) {
    std::cerr << "Server-log: upload\t" << req.get_header_value("Content-Type")
              << std::endl;
    // 二进制数据可以用：multipart/form-data 和 application/octet-stream
    if (req.is_multipart_form_data()) {
      uint64_t start_time = getMs();
      httplib::MultipartFormDataItems files;
      printf("%s(%d)\n", __FUNCTION__, __LINE__);
      // 先拿到 file 信息，再流式读取
      content_reader(
          [&](const httplib::MultipartFormData &file) {
            files.push_back(file);
             printf("%s(%d)", __FUNCTION__, __LINE__);
            std::cerr << "\t files.push_back filename:" << file.filename  << ", name:" << file.name << ", content:" << file.content << ", type: " << file.content_type << std::endl;
            return true;
          },
          [&](const char *data, size_t data_length) {
            files.back().content.append(data, data_length); // 读取到内容
            // if( files.back().name != "file") 
            {
              printf("%s(%d)", __FUNCTION__, __LINE__);
                std::cerr << "\t files.back filename:" << files.back().filename  << ", name:" << files.back().name << ", data_length:" << data_length <<  std::endl;
            }
            // std::cerr << "\t files.back() upload read:" << data_length  << ", t: "<< getMs() - start_time << ", data: "<< std::endl; // <<  data
            return true;
          });
      printf("%s(%d)\n", __FUNCTION__, __LINE__);

      int i = 0;
      for (const auto &file : files) {
        printf("%s(%d) [%d] - %s\n", __FUNCTION__, __LINE__, i++, file.name.c_str());
        if (file.filename.empty()) {
          std::cout << file.name << " : " << file.content << std::endl;
        } else {
          // C++ windows下utf-8转gbk
          //  char* ansiStr = utf8_2_ansi(file.filename.c_str(), nullptr);
          std::string fileName =  "upload-dir/" + file.filename;
          // free(ansiStr);
          // ansiStr = nullptr;
          std::cout << "fileName:" << fileName << std::endl; 
          std::ofstream ofs(fileName,
                            std::ofstream::trunc | std::ostream::binary);
          if (ofs.is_open()) {
            uint64_t need_time = getMs() - start_time;
            float bps = 0;
            if(need_time > 0) 
              bps = 1.0* file.content.size()/(need_time/1000.0) / 1000.0 *8;  // 单位kbps
            std::cout << __FUNCTION__ << ":" << __LINE__ << " " << fileName
                      << " write " << file.content.size() << " bytes,  t: "<< need_time << ", bps = " << bps << "kbps\n";
            ofs.write(file.content.c_str(), file.content.size());

            need_time = getMs() - start_time;
            if(need_time > 0) 
              bps = 1.0* file.content.size()/(need_time/1000.0) / 1000.0 *8;  // 单位kbps
            std::cout << __FUNCTION__ << ":" << __LINE__ << " " << fileName
                      << " write " << file.content.size() << " bytes,  t: "<< need_time << ", bps = " << bps << "kbps\n";
            ofs.close();
          }
        }
      }

    } else {
      std::string body;
      content_reader([&](const char *data, size_t data_length) {
        body.append(data, data_length);
        std::cerr << "\tupload read:" << data_length << std::endl;
        return true;
      });
      std::cerr << "\tupload read " << body << std::endl;
    }
    res.set_content(R"({"message":"upload result"})", "appliation/json");
  });

  svr.listen("0.0.0.0", 1234);
}


#if 0
Server-log: upload      multipart/form-data; boundary=----WebKitFormBoundaryB9gXpE1D9WTUTzE7
operator()(107)
         files.push_back upload read 注释:这里提交的时候是空，没有上传文件
         files.back() upload read:0  注释:所以这里读取为0
         files.push_back upload read 零声教育学员offer案例分享.html         注释: 下一个文件
         files.back() upload read:14236
         files.back() upload read:16384
         files.back() upload read:16384
         files.back() upload read:16384
         files.back() upload read:16384
         files.back() upload read:16384
         files.back() upload read:16384
         files.back() upload read:16384
         files.back() upload read:1463
         files.push_back upload read jmpeg转h264.c      注释: 下一个文件
         files.back() upload read:5598
operator()(122)
operator()(126) [0] - image_file            注释: 文件类型
image_file :                                  注释: 这个这里没有文件内容
operator()(126) [1] - text_file
operator():139 零声教育学员offer案例分享.html write 130387 bytes   注释: 这这里的文件大小
operator()(126) [2] - image_file
operator():139 jmpeg转h264.c write 5598 bytes
#endif