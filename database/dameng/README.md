# Dameng DM8 setup / 达梦 DM8 部署

[English README](../../README.md) · [中文 README](../../README.zh-CN.md)

The examples use a fresh DM8 instance in native mode (`COMPATIBLE_MODE=0`) with UTF-8, case-sensitive identifiers and 32 KiB pages. Use a dedicated empty schema. Run database initialization as its owner. Use separate logins for Server and Web.

以下示例使用原生模式（`COMPATIBLE_MODE=0`）、UTF-8、大小写敏感、32 KiB 页大小的全新 DM8 实例。使用独立空 schema，由所有者初始化。Server 和 Web 分别使用独立的运行账号。

## 1. Build Server and generate SQL / 构建与生成 SQL

Install the [upstream source build dependencies](https://www.zabbix.com/documentation/8.0/en/manual/installation/install), Python 3, Perl and the unixODBC development package (for example, `unixodbc-dev` on Debian). From the repository root:

安装上游构建依赖、Python 3、Perl 和 unixODBC 开发包。在仓库根目录执行：

```sh
./bootstrap.sh
./configure --enable-server --with-dameng --with-libcurl --with-libxml2 --with-openssl
make dbschema_postgresql dbschema_c
make -j4
python3 database/dameng/generate.py . /path/to/generated-sql
```

The Server binary is `src/zabbix_server/zabbix_server`. The Web files are under `ui/`. Deploy the Web files using the usual Zabbix PHP configuration and enable PDO_ODBC in the PHP runtime that serves requests.

Server 二进制位于 `src/zabbix_server/zabbix_server`。部署 `ui/` 下的 Web 文件，并在实际提供 Web 服务的 PHP 运行环境启用 PDO_ODBC。

## 2. Install the schema / 初始化数据库

Provision the schema owner (`ZBX_OWNER` here) with permissions to create the schema objects. Create `ZBX_SERVER` and `ZBX_WEB` with `CREATE SESSION`. Configure the DM client character set as shown below before running DIsql. Set `DM_PASSWORD` in the shell from your secret store. Run the first command with DIsql available, then apply the generated grants through DIsql as the same owner. The verifier checks the matching mandatory and optional schema version (`8000000`).

给 schema 所有者（示例为 `ZBX_OWNER`）授予建库所需权限，另建具有 `CREATE SESSION` 权限的 `ZBX_SERVER` 和 `ZBX_WEB`。先配置下文的客户端字符集，再通过环境变量提供所有者密码。运行第一条命令完成建库后，用同一所有者通过 DIsql 执行生成的授权脚本。初始化会核对两个数据库版本字段均为 `8000000`。

```sh
DM_HOME=/opt/dmdbms DM_ADDRESS=dm.example.internal:5236 DM_USER=ZBX_OWNER \
  database/dameng/apply.sh /path/to/generated-sql
python3 database/dameng/grant-runtime.py /path/to/generated-sql ZBX_OWNER ZBX_SERVER ZBX_WEB \
  > /path/to/generated-sql/grants.sql
```

## 3. Register ODBC and configure Zabbix / 配置 ODBC 和应用

Install the matching Dameng ODBC and DPI libraries and their dependencies in each application runtime. Make the libraries available to the service account's loader. The Server and PHP must both be able to see the DSN. Use `odbcinst -j` to find the active ODBC configuration paths.

在 Server 和 PHP 所在环境安装匹配的达梦 ODBC、DPI 库及其依赖，确保服务进程可加载这些库并读取同名 DSN。用 `odbcinst -j` 查看实际生效的配置位置。

`odbcinst.ini`

```ini
[DM8]
Driver=/opt/dm-client/libdodbc.so
```

`odbc.ini`

```ini
[ZBXDM]
Driver=DM8
SERVER=dm.example.internal
TCP_PORT=5236
CHARACTER_CODE=PG_UTF8
LANGUAGE=ENGLISH
```

`dm_svc.conf`

```ini
CHAR_CODE=(PG_UTF8)
LANGUAGE=(en)
```

Set these keys in `zabbix_server.conf` / 在 `zabbix_server.conf` 中设置：

```ini
DBName=ZBXDM
DBSchema=ZBX_OWNER
DBUser=ZBX_SERVER
DBPassword=REPLACE_WITH_SERVER_PASSWORD
```

In the Web wizard select **Dameng (ODBC)** and enter DSN `ZBXDM`, schema `ZBX_OWNER` and the `ZBX_WEB` credentials. The equivalent keys in `ui/conf/zabbix.conf.php` are:

Web 安装向导选择 **Dameng (ODBC)**，填写 DSN `ZBXDM`、schema `ZBX_OWNER` 和 `ZBX_WEB` 的凭据。也可以在 `ui/conf/zabbix.conf.php` 中配置：

```php
$DB['TYPE'] = 'DAMENG';
$DB['SERVER'] = '';
$DB['PORT'] = '0';
$DB['DATABASE'] = 'ZBXDM';
$DB['SCHEMA'] = 'ZBX_OWNER';
$DB['USER'] = 'ZBX_WEB';
$DB['PASSWORD'] = 'REPLACE_WITH_WEB_PASSWORD';
$DB['ENCRYPTION'] = false;
```

Zabbix's application-level database TLS options are unavailable for this backend. The endpoint and client settings come from the DSN and the Dameng client.

此后端暂未实现 Zabbix 应用层的数据库 TLS 配置项。端点与客户端参数由 DSN 和达梦客户端配置提供。

## 4. Optional ClickHouse history / 可选的 ClickHouse 历史存储

Use the [upstream ClickHouse setup instructions](https://www.zabbix.com/documentation/8.0/en/manual/appendix/install/clickhouse_setup) to create history tables. Set the same history types on both the Server (`HistoryProvider`) and Web (`$HISTORY_PROVIDERS`). The tested route is `uint`. On the single-node test table `zabbix.history_uint`, [the example SQL](clickhouse-dedup-uint.sql) sets a deduplication window of 1,024 insert blocks.

按上游说明创建历史表，在 Server 的 `HistoryProvider` 和 Web 的 `$HISTORY_PROVIDERS` 配置一致的历史类型。已测试的路由是 `uint`。对于单节点测试表 `zabbix.history_uint`，[示例 SQL](clickhouse-dedup-uint.sql) 将去重窗口设为最近 1,024 个插入块。
