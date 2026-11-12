/*
** Copyright (C) 2001-2026 Zabbix SIA
**
** This program is free software: you can redistribute it and/or modify it under the terms of
** the GNU Affero General Public License as published by the Free Software Foundation, version 3.
**
** This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
** without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
** See the GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License along with this program.
** If not, see <https://www.gnu.org/licenses/>.
**/

#include "dbconn.h"

#if defined(HAVE_DAMENG)
#include "dbdameng.h"
#include "zbxstr.h"

struct zbx_db_result
{
	zbx_db_row_t	*rows;
	int		nrow;
	int		ncolumn;
	int		cursor;
};

static int	dm_diagnostic(zbx_dbconn_t *db, SQLSMALLINT type, SQLHANDLE handle, zbx_err_codes_t errcode,
		const char *context)
{
	SQLCHAR		state[6] = "HY000", message[SQL_MAX_MESSAGE_LENGTH] = "ODBC operation failed";
	SQLINTEGER	code = 0;
	char		*diagnostic;
	int		ret;

	if (NULL != handle)
		SQLGetDiagRec(type, handle, 1, state, &code, message, sizeof(message), NULL);

	if (-6602 == code)
		errcode = ERR_Z3008;

	diagnostic = zbx_dsprintf(NULL, "%s/%ld %s", state, (long)code, message);
	dbconn_errlog(db, errcode, (int)code, diagnostic, context);
	zbx_free(diagnostic);

	ret = (0 == strncmp((const char *)state, "08", 2) ? ZBX_DB_DOWN : ZBX_DB_FAIL);
	return ret;
}

void	dbdameng_close(zbx_dbconn_t *db)
{
	if (SQL_NULL_HDBC != db->conn)
	{
		if (0 != db->dm_connected)
		{
			SQLEndTran(SQL_HANDLE_DBC, db->conn, SQL_ROLLBACK);
			SQLDisconnect(db->conn);
		}
		SQLFreeHandle(SQL_HANDLE_DBC, db->conn);
		db->conn = SQL_NULL_HDBC;
		db->dm_connected = 0;
	}

	if (SQL_NULL_HENV != db->dm_env)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, db->dm_env);
		db->dm_env = SQL_NULL_HENV;
	}
}

/* Apply DBSchema on every new ODBC session, including reconnects. */
static int	dm_set_schema(zbx_dbconn_t *db, const char *schema)
{
	static const char	letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	static const char	identifier[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
	SQLHSTMT		stmt = SQL_NULL_HSTMT;
	SQLRETURN		rc;
	int			ret = ZBX_DB_OK;
	char			*name, *sql;
	size_t			length = strlen(schema);

	if (128 < length || NULL == strchr(letters, *schema) || length != strspn(schema, identifier))
	{
		dbconn_errlog(db, ERR_Z3001, 0, "invalid Dameng DBSchema identifier", db->config->dbname);
		return ZBX_DB_FAIL;
	}

	if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, db->conn, &stmt)))
		return dm_diagnostic(db, SQL_HANDLE_DBC, db->conn, ERR_Z3001, "allocating SET SCHEMA statement");

	name = zbx_strdup(NULL, schema);
	zbx_strupper(name);
	sql = zbx_dsprintf(NULL, "set schema \"%s\"", name);
	rc = SQLExecDirect(stmt, (SQLCHAR *)sql, SQL_NTS);

	if (!SQL_SUCCEEDED(rc))
		ret = dm_diagnostic(db, SQL_HANDLE_STMT, stmt, ERR_Z3001, sql);

	zbx_free(sql);
	zbx_free(name);
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

	return ret;
}

int	dbdameng_open(zbx_dbconn_t *db)
{
	const zbx_db_config_t	*config = db->config;
	const char		*user = ZBX_NULL2EMPTY_STR(config->dbuser),
				*password = ZBX_NULL2EMPTY_STR(config->dbpassword);
	SQLRETURN		rc;

	if (NULL == config->dbname || '\0' == *config->dbname)
	{
		dbconn_errlog(db, ERR_Z3001, 0, "DBName must name an ODBC DSN", "");
		return ZBX_DB_FAIL;
	}

	if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &db->dm_env)) ||
			!SQL_SUCCEEDED(SQLSetEnvAttr(db->dm_env, SQL_ATTR_ODBC_VERSION,
					(SQLPOINTER)SQL_OV_ODBC3, 0)) ||
			!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, db->dm_env, &db->conn)))
	{
		return dm_diagnostic(db, SQL_HANDLE_ENV, db->dm_env, ERR_Z3001, config->dbname);
	}

	rc = SQLConnect(db->conn, (SQLCHAR *)config->dbname, SQL_NTS, (SQLCHAR *)user, SQL_NTS,
			(SQLCHAR *)password, SQL_NTS);
	if (!SQL_SUCCEEDED(rc))
		return dm_diagnostic(db, SQL_HANDLE_DBC, db->conn, ERR_Z3001, config->dbname);

	db->dm_connected = 1;

	if (NULL != config->dbschema && '\0' != *config->dbschema)
	{
		int	ret = dm_set_schema(db, config->dbschema);

		if (ZBX_DB_OK != ret)
			dbdameng_close(db);

		return ret;
	}

	return ZBX_DB_OK;
}

int	dbdameng_begin(zbx_dbconn_t *db)
{
	if (SQL_NULL_HDBC == db->conn)
		return ZBX_DB_DOWN;

	if (SQL_SUCCEEDED(SQLSetConnectAttr(db->conn, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0)))
		return ZBX_DB_OK;

	return dm_diagnostic(db, SQL_HANDLE_DBC, db->conn, ERR_Z3005, "disabling autocommit");
}

int	dbdameng_end(zbx_dbconn_t *db, SQLSMALLINT completion)
{
	if (SQL_NULL_HDBC == db->conn)
		return (SQL_ROLLBACK == completion ? ZBX_DB_OK : ZBX_DB_FAIL);

	if (!SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, db->conn, completion)))
	{
		int	ret;

		ret = dm_diagnostic(db, SQL_HANDLE_DBC, db->conn, ERR_Z3005,
				SQL_COMMIT == completion ? "commit outcome requires verification" : "rollback");
		dbdameng_close(db);
		return (SQL_COMMIT == completion ? ZBX_DB_FAIL : ret);
	}

	if (!SQL_SUCCEEDED(SQLSetConnectAttr(db->conn, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0)))
	{
		dm_diagnostic(db, SQL_HANDLE_DBC, db->conn, ERR_Z3005, "restoring autocommit on completed transaction");
		dbdameng_close(db);
	}

	return ZBX_DB_OK;
}

static int	dm_is_identifier(char c)
{
	return 0 != isalnum((unsigned char)c) || '_' == c || '$' == c || '#' == c;
}

/* Zabbix emits SQL batches with top-level semicolons and quotes string literals by doubling apostrophes. */
static char	*dm_next_statement(const char **input)
{
	char		*sql = NULL;
	size_t		alloc = 0, offset = 0;
	const char	*p = *input;

	while ('\0' != *p && (';' == *p || 0 != isspace((unsigned char)*p)))
		p++;

	while ('\0' != *p && ';' != *p)
	{
		const char	*start = p;

		if ('\'' == *p || '"' == *p)
		{
			char	quote = *p++;

			while ('\0' != *p)
			{
				if (quote != *p++)
					continue;
				if (quote != *p)
					break;
				p++;
			}
		}
		else if ('-' == p[0] && '-' == p[1])
		{
			while ('\0' != *p && '\n' != *p)
				p++;
		}
		else if ('/' == p[0] && '*' == p[1])
		{
			const char	*end = strstr(p + 2, "*/");

			p = NULL != end ? end + 2 : p + strlen(p);
		}
		else if (0 != dm_is_identifier(*p))
		{
			while (0 != dm_is_identifier(*p))
				p++;

			if (6 == p - start && 0 == zbx_strncasecmp(start, "object", 6))
			{
				zbx_strcpy_alloc(&sql, &alloc, &offset, "\"OBJECT\"");
				continue;
			}

			if (5 == p - start && 0 == zbx_strncasecmp(start, "model", 5))
			{
				zbx_strcpy_alloc(&sql, &alloc, &offset, "\"MODEL\"");
				continue;
			}
		}
		else
			p++;

		zbx_strncpy_alloc(&sql, &alloc, &offset, start, (size_t)(p - start));
	}

	*input = p;
	return sql;
}

static int	dm_statement(zbx_dbconn_t *db, const char *sql, SQLHSTMT *stmt, int write)
{
	SQLRETURN	rc;

	if (SQL_NULL_HDBC == db->conn)
		return ZBX_DB_DOWN;

	if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, db->conn, stmt)))
		return dm_diagnostic(db, SQL_HANDLE_DBC, db->conn, ERR_Z3005, "allocating statement");

	rc = SQLExecDirect(*stmt, (SQLCHAR *)sql, SQL_NTS);
	if (!SQL_SUCCEEDED(rc) && SQL_NO_DATA != rc)
	{
		int	ret = dm_diagnostic(db, SQL_HANDLE_STMT, *stmt, ERR_Z3005, sql);

		SQLFreeHandle(SQL_HANDLE_STMT, *stmt);
		*stmt = SQL_NULL_HSTMT;

		if (ZBX_DB_DOWN == ret && 0 != write && 0 == db->txn_level)
		{
			zabbix_log(LOG_LEVEL_WARNING, "autocommit statement outcome requires verification");
			dbdameng_close(db);
			return ZBX_DB_FAIL;
		}

		return ret;
	}

	return ZBX_DB_OK;
}

int	dbdameng_execute(zbx_dbconn_t *db, const char *input)
{
	char	*sql;
	int	total = 0, completed = 0;

	while (NULL != (sql = dm_next_statement(&input)))
	{
		SQLHSTMT	stmt = SQL_NULL_HSTMT;
		SQLLEN		rows = 0;
		int		ret = dm_statement(db, sql, &stmt, 1);

		zbx_free(sql);
		if (ZBX_DB_OK != ret)
		{
			/* An earlier statement may already be committed outside a transaction. */
			if (0 != completed && 0 == db->txn_level && ZBX_DB_DOWN == ret)
			{
				dbdameng_close(db);
				return ZBX_DB_FAIL;
			}

			return ret;
		}

		if (SQL_SUCCEEDED(SQLRowCount(stmt, &rows)) && 0 < rows)
			total += (int)MIN(rows, INT_MAX - total);

		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		completed++;
	}

	return total;
}

static int	dm_get_column(zbx_dbconn_t *db, SQLHSTMT stmt, SQLUSMALLINT column, char **value)
{
	SQLRETURN	rc;
	size_t		alloc = 0, offset = 0;

	do
	{
		SQLLEN	length;
		char	buf[4096];

		rc = SQLGetData(stmt, column, SQL_C_CHAR, buf, sizeof(buf), &length);
		if (SQL_NO_DATA == rc)
			break;

		if (!SQL_SUCCEEDED(rc))
			return dm_diagnostic(db, SQL_HANDLE_STMT, stmt, ERR_Z3005, "reading result column");

		if (SQL_NULL_DATA == length)
			return ZBX_DB_OK;

		zbx_strcpy_alloc(value, &alloc, &offset, buf);
	}
	while (SQL_SUCCESS_WITH_INFO == rc);

	return ZBX_DB_OK;
}

zbx_db_result_t	dbdameng_select(zbx_dbconn_t *db, const char *input)
{
	char		*sql = dm_next_statement(&input);
	SQLHSTMT	stmt = SQL_NULL_HSTMT;
	SQLSMALLINT	columns;
	SQLRETURN	rc;
	zbx_db_result_t	result = NULL;
	int		ret = ZBX_DB_FAIL, capacity = 0;

	if (NULL == sql)
		return NULL;

	ret = dm_statement(db, sql, &stmt, 0);
	zbx_free(sql);

	if (ZBX_DB_OK != ret)
		return ZBX_DB_DOWN == ret ? (zbx_db_result_t)ZBX_DB_DOWN : NULL;

	if (!SQL_SUCCEEDED(SQLNumResultCols(stmt, &columns)) || 0 >= columns)
	{
		ret = dm_diagnostic(db, SQL_HANDLE_STMT, stmt, ERR_Z3005, "counting result columns");
		goto out;
	}

	result = zbx_malloc(NULL, sizeof(*result));
	memset(result, 0, sizeof(*result));
	result->ncolumn = columns;

	while (SQL_SUCCEEDED(rc = SQLFetch(stmt)))
	{
		zbx_db_row_t	row;

		if (capacity == result->nrow)
		{
			capacity = 0 == capacity ? 64 : capacity * 2;
			result->rows = zbx_realloc(result->rows, sizeof(*result->rows) * (size_t)capacity);
		}

		row = zbx_malloc(NULL, sizeof(*row) * (size_t)columns);
		memset(row, 0, sizeof(*row) * (size_t)columns);
		result->rows[result->nrow++] = row;

		for (SQLSMALLINT i = 0; i < columns; i++)
		{
			if (ZBX_DB_OK != (ret = dm_get_column(db, stmt, (SQLUSMALLINT)(i + 1), &row[i])))
				goto out;
		}
	}

	if (SQL_NO_DATA != rc)
		ret = dm_diagnostic(db, SQL_HANDLE_STMT, stmt, ERR_Z3005, "fetching result rows");

out:
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

	if (ZBX_DB_OK != ret)
	{
		dbdameng_free_result(result);
		return ZBX_DB_DOWN == ret ? (zbx_db_result_t)ZBX_DB_DOWN : NULL;
	}

	return result;
}

zbx_db_row_t	dbdameng_fetch(zbx_db_result_t result)
{
	return result->cursor < result->nrow ? result->rows[result->cursor++] : NULL;
}

void	dbdameng_free_result(zbx_db_result_t result)
{
	if (NULL == result || (zbx_db_result_t)ZBX_DB_DOWN == result)
		return;

	for (int i = 0; i < result->nrow; i++)
	{
		for (int j = 0; j < result->ncolumn; j++)
			zbx_free(result->rows[i][j]);
		zbx_free(result->rows[i]);
	}

	zbx_free(result->rows);
	zbx_free(result);
}

int	dbdameng_row_count(zbx_db_result_t result)
{
	return NULL != result && (zbx_db_result_t)ZBX_DB_DOWN != result ? result->nrow : 0;
}
#endif
