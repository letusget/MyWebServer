#include <unistd.h>
#include "server/webserver.h"

int main()
{
    /* 守护进程 后台运行 */
    // daemon(1, 0);

    WebServer server(
        9596, 3, 60000, false,                     /* 端口 ET模式 timeoutMs 退出  */
        3306, "root", "test", "test", /* Mysql配置,谨防数据库密码泄露 */
        12, 8, true, 1, 1024);                     /* 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */

    // 启动服务器
    server.Start();
}
