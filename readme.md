# 个人 WebServer 服务器项目

- 使用 线程池 + 非阻塞socket + epoll(LT实现) + 事件处理(Proactor模式)并发模型
- 使用状态机解析HTTP请求报文，支持解析GET和POST请求
* 利用标准库容器封装char，实现自动增长的缓冲区；
* 基于小根堆实现的定时器，关闭超时的非活动连接；
* 利用单例模式与阻塞队列实现异步的日志系统，记录服务器运行状态；
* 利用RAII机制实现了数据库连接池，减少数据库连接建立与关闭的开销，同时实现了用户注册登录功能
- 经Webbench压力测试可以实现上万的并发连接数据交换

## Denon演示

gif演示效果如下：


## 项目运行

- 服务器测试环境:C++14 
    
    CentOS Linux release 8.5.2111
    
    Ubuntu 20.04.3 LTS

- 浏览器测试环境

    Linux、windows环境均可

    Chrome、Firefox 访问效果符合预期。

    其他环境与浏览器暂未测试

- 数据库准备
    ```mysql
    // 建立yourdb库
    create database yourdb;

    // 创建user表
    USE yourdb;
    CREATE TABLE user(
        username char(50) NULL,
        password char(50) NULL
    )ENGINE=InnoDB;

    // 添加数据
    INSERT INTO user(username, password) VALUES('name', 'password');
    ```

- 编译&运行

    ```cpp
    make
    ./bin/server
    ```

    **默认端口号为9596**

- 访问

    浏览器端打开 ip+port+页面

    ```cpp
    ip:port/index.html
    ```
