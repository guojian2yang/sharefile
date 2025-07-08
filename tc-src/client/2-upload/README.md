# 说明

这个代码在运行之前，记得使用truncate生成测试文件
```
truncate -s 25M testfile.d
```
我们是使用25M的文件作为测试大小。

然后根据自己用户名和密码以及 服务器地址修改：
```
#define   TC_HTTP_SERVER_IP "192.168.1.27"
#define   USER_NAME "qingfu"        //用户名
#define   USER_PWD  "123456"        //密码
#define   CONCURRENT   4            //并发的上传线程，就是客户端一次允许多少个任务上传    
// 使用truncate命令生成测试文件, 比如以下命令生成25M的文件，文件名为testfile.d
// truncate -s 25M testfile.d
#define   FULL_PATH "testfile.d"     //测试上传的文件全名路径，可以是相对路径

```

还需要注意，这里修改了数据库的file_info表
```
DROP TABLE IF EXISTS `file_info`;
CREATE TABLE `file_info` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT COMMENT '文件序号，自动递增，主键',
  `md5` varchar(256) NOT NULL COMMENT '文件md5',
  `file_id` varchar(256) NOT NULL COMMENT '文件id:/group1/M00/00/00/xxx.png',
  `url` varchar(512) NOT NULL COMMENT '文件url 192.168.52.139:80/group1/M00/00/00/xxx.png',
  `size` bigint(20) DEFAULT '0' COMMENT '文件大小, 以字节为单位',
  `type` varchar(32) DEFAULT '' COMMENT '文件类型： png, zip, mp4……',
  `count` int(11) DEFAULT '0' COMMENT '文件引用计数,默认为1。每增加一个用户拥有此文件，此计数器+1',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_md5` (`md5`)
  -- KEY `uq_md5` (`md5`(8))  -- 前缀索引
) ENGINE=InnoDB AUTO_INCREMENT=70 DEFAULT CHARSET=utf8 COMMENT='文件信息表';
```
将前缀索引改成了全字符串索引，因为这里的前缀索引其实会导致md5存在冲突的可能。




# 参考
MD5源码参考：
https://www.cnblogs.com/luxiaoxun/archive/2013/04/08/3008808.html