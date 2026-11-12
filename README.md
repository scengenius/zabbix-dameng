# Zabbix with a Dameng DM8 Backend

[简体中文](README.zh-CN.md) · [Upstream Zabbix](https://github.com/zabbix/zabbix)

This project targets Xinchuang (IT application innovation) deployments that require a Chinese database. It enables Zabbix 8.0.0 Server and Web/API to use Dameng DM8 in native mode (`COMPATIBLE_MODE=0`) as Zabbix's primary database for monitoring configuration, users, events, and audit records. ScenGenius maintains this adaptation.

## What changed

- **Server** adds the `--with-dameng` build option and a unixODBC backend for the Dameng ODBC driver. It handles transactions, reconnects, SQL dialect differences and per-session schema selection.
- **Web and API** add a `DAMENG` backend through PHP PDO_ODBC. The setup wizard accepts an ODBC data source name (DSN) and schema. The adapter handles queries, transactions and image data. Template export and trigger dependency filtering skip empty queries that fail with restricted DM8 accounts.
- **Database initialization** converts the 8.0.0 PostgreSQL schema, seed data and icons into native DM8 SQL. The scripts verify tables, indexes, foreign keys and triggers and generate grants for separate Server and Web logins.
- **Optional ClickHouse history** waits for asynchronous inserts to complete before acknowledging success and enables insert deduplication. The Web API preserves unsigned 64-bit integers when decoding ClickHouse JSON.

## Build and install

Use the [Dameng setup guide](database/dameng/README.md) to build from source, generate and install the schema, register the ODBC DSN, and configure both applications. It also covers optional ClickHouse history. Obtain the DM8 server and matching ODBC client libraries from Dameng.

## Verified scope

This adaptation is experimental. The integration baseline is Zabbix 8.0.0 with DM8 8.1.4.80 Pack29 (build 288894), Linux ARM64, unixODBC 2.3.11 and PHP 8.2 with PDO_ODBC. The lab checks cover fresh initialization, separate restricted database logins, the setup wizard, API operations, image byte round-trips, and collection through problem and recovery. Both DM8 and optional ClickHouse 26.4.4.38 passed history readback at the maximum UInt64 value.

The Dameng build is for Zabbix Server. Use an upstream-supported backend for Zabbix Proxy. The current build requires the matching 8.0.0 schema and disables automatic DM8 schema upgrades. Other DM8 builds and platforms, production load, high availability, and in-place database upgrades need separate validation.

## License

This fork retains the upstream [GNU Affero General Public License v3.0 only](COPYING). The [Zabbix 8.0 manual](https://www.zabbix.com/documentation/8.0/en/) covers general operation and upstream components.
