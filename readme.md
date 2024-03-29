# 个人 WebServer 服务器项目

- 使用 线程池 + 非阻塞socket + epoll(LT实现) + 事件处理(Proactor模式)并发模型
- 使用状态机解析HTTP请求报文，支持解析GET和POST请求
- 访问服务器数据库实现web端用户请求页面，可以请求服务器图片或其他数据信息
- 经Webbench压力测试可以实现上万的并发连接数据交换

## Denon演示

gif演示效果如下：

![Demo](https://tvax3.sinaimg.cn/large/006x3t5Xgy1h0iwn6cg82g30ja0aqe81.gif)

## 项目运行

- 服务器测试环境
    
    CentOS Linux release 8.5.2111
    
    Ubuntu 20.04.3 LTS

- 浏览器测试环境

    Linux、windows环境均可

    Chrome、Firefox 访问效果符合预期。

    其他环境与浏览器暂未测试

- 编译

    ```cpp
    g++ *.cpp -o webserver -pthread
    ```

- 运行

    运行需要指定端口号，如果没有指定端口号，在运行时会给出运行提示，关于端口，如果使用云服务器，需要开放指定端口访问和开启系统防火墙指定端口

    下面演示的是 指定 9999端口

    ```cpp
    ./webserver 9999
    ```

- 访问

    浏览器端打开 ip+port+页面

    ```cpp
    ip:port/index.html
    ```
