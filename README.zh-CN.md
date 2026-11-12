# Zabbix 达梦原生适配

[English](README.md) · [Zabbix 上游仓库](https://github.com/zabbix/zabbix)

这是由数璟科技（ScenGenius）维护的 Zabbix 8.0.0 实验性分支。Zabbix Server 和 Web/API 可以使用达梦 DM8 原生模式（`COMPATIBLE_MODE=0`）作为主数据库，存储监控配置、用户、事件和审计记录。

## 改造内容

- **Server** 增加 `--with-dameng` 编译选项，通过 unixODBC 和达梦 ODBC 驱动连接数据库，处理事务、断线重连、SQL 方言和会话 schema 切换。
- **Web 和 API** 增加基于 PHP PDO_ODBC 的 `DAMENG` 后端。安装向导支持 ODBC 数据源名（DSN）和 schema，适配层处理查询、事务和图标数据。模板导出和触发器依赖筛选会跳过在低权限 DM8 账号下报错的空集合查询。
- **建库** 将 Zabbix 8.0.0 的 PostgreSQL 表结构、种子数据和图标转换为达梦原生 SQL。脚本校验表、索引、外键和触发器，并为 Server、Web 两个独立账号生成授权。
- **可选的 ClickHouse 历史存储** 等待异步写入完成后再确认，并启用插入去重。Web API 解析 ClickHouse JSON 时保留完整的 64 位无符号整数。

## 构建和安装

按[达梦部署指南](database/dameng/README.md)从源码构建 Server，生成并初始化 schema，注册 ODBC 数据源，并配置 Server 和 Web。指南包含可选的 ClickHouse 历史配置。达梦服务端和匹配的 ODBC 客户端库由达梦提供。

## 实测范围

验证环境为 Zabbix 8.0.0、DM8 8.1.4.80 Pack29（build 288894）、Linux ARM64、unixODBC 2.3.11 和 PHP 8.2 PDO_ODBC。实验覆盖空库初始化、独立低权限账号、安装向导、API 操作、图标字节回读，以及采集到告警和恢复的链路。达梦直接存储历史和可选的 ClickHouse 26.4.4.38 均通过了 UInt64 最大值读回测试。

达梦后端用于 Zabbix Server。Proxy 继续使用上游支持的数据库。当前版本要求与 8.0.0 对应的 schema，并关闭达梦自动升级。其他 DM8 构建和平台、生产负载、高可用和存量达梦库就地升级均需独立验证。

## 许可证

本分支沿用上游 [GNU Affero General Public License v3.0 only](COPYING)。通用操作及上游组件请参阅 [Zabbix 8.0 手册](https://www.zabbix.com/documentation/8.0/zh/manual)。
