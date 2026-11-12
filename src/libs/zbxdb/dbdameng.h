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

#ifndef ZABBIX_DBDAMENG_H
#define ZABBIX_DBDAMENG_H

#include "dbconn.h"

int		dbdameng_open(zbx_dbconn_t *db);
void		dbdameng_close(zbx_dbconn_t *db);
int		dbdameng_begin(zbx_dbconn_t *db);
int		dbdameng_end(zbx_dbconn_t *db, SQLSMALLINT completion);
int		dbdameng_execute(zbx_dbconn_t *db, const char *input);
zbx_db_result_t	dbdameng_select(zbx_dbconn_t *db, const char *input);
zbx_db_row_t	dbdameng_fetch(zbx_db_result_t result);
void		dbdameng_free_result(zbx_db_result_t result);
int		dbdameng_row_count(zbx_db_result_t result);

#endif
