<?php
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


/**
 * Database backend for DM8 native mode through PDO_ODBC.
 */
class DamengDbBackend extends DbBackend {

	public function connect($host, $port, $user, $password, $dbname, $schema) {
		if ($this->tls_encryption) {
			$this->setError(_('Database TLS options are unavailable for the Dameng ODBC backend.'));
			return null;
		}

		if (!preg_match('/\A[A-Za-z0-9_.-]+\z/D', $dbname)) {
			$this->setError(_('Specify a configured Dameng ODBC DSN name.'));
			return null;
		}

		try {
			return new PDO('odbc:'.$dbname, $user, $password, [
				PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
				PDO::ATTR_CASE => PDO::CASE_LOWER,
				PDO::ATTR_STRINGIFY_FETCHES => true
			]);
		}
		catch (PDOException $e) {
			$this->setError(_('Unable to connect to the Dameng ODBC data source.').' '.self::diagnostic($e));
			return null;
		}
	}

	public function init(): bool {
		global $DB;

		$schema = $DB['SCHEMA'];
		if ($schema !== '') {
			if (!preg_match('/\A[A-Za-z][A-Za-z0-9_]*\z/D', $schema)) {
				$this->setError(_('Invalid Dameng database schema name.'));
				return false;
			}

			if (!DBexecute('SET SCHEMA "'.strtoupper($schema).'"')) {
				$this->setError(_('Unable to select the Dameng database schema.'));
				return false;
			}
		}

		return $this->checkEncoding();
	}

	protected function checkDbVersionTable(): bool {
		$exists = DBfetch(DBselect("SELECT TABLE_NAME FROM ALL_TABLES WHERE OWNER=SYS_CONTEXT('USERENV',".
			"'CURRENT_SCHEMA') AND TABLE_NAME='DBVERSION'"));

		if (!$exists) {
			$this->setError(_('The Zabbix database version table was not found in the active Dameng schema.'));
			return false;
		}

		return true;
	}

	public function checkEncoding(): bool {
		$row = DBfetch(DBselect('SELECT SF_GET_UNICODE_FLAG() AS unicode_flag'));

		if (!$row || $row['unicode_flag'] !== '1') {
			$this->setWarning(_('Zabbix requires a Dameng database initialized with UTF-8.'));
			$this->setError($this->getWarning());
			return false;
		}

		return true;
	}

	/**
	 * Quote the two reserved column names used by the converted schema.
	 * Preserve quoted literals, quoted identifiers and comments verbatim.
	 */
	public static function quoteReservedIdentifiers(string $sql): string {
		$pattern = <<<'REGEX'
~'(?:[^']|'')*'|"(?:[^"]|"")*"|/\*.*?\*/|--[^\r\n]*|\b(object|model)\b~is
REGEX;

		return preg_replace_callback($pattern,
			static fn(array $match): string => isset($match[1]) ? '"'.strtoupper($match[1]).'"' : $match[0],
			$sql
		);
	}

	public static function diagnostic(PDOException $e): string {
		return sprintf('[SQLSTATE %s, DM %s]', $e->errorInfo[0] ?? $e->getCode(), $e->errorInfo[1] ?? '?');
	}

	public static function decodeBlob($value): string {
		if (is_resource($value)) {
			$value = stream_get_contents($value);
		}

		if ($value === '') {
			return '';
		}

		if (!is_string($value) || strlen($value) % 2 !== 0 || !ctype_xdigit($value)) {
			throw new DBException(_('Invalid hexadecimal BLOB returned by the Dameng ODBC driver.'));
		}

		return hex2bin($value);
	}

	/**
	 * Read a large image as bounded VARBINARY slices in a single database snapshot.
	 */
	public static function readLargeImage(string $imageid): string {
		$columns = ['DBMS_LOB.GETLENGTH(i.image) AS image_length'];

		for ($offset = 1; $offset <= ZBX_MAX_IMAGE_SIZE; $offset += 16384) {
			$columns[] = 'DBMS_LOB.SUBSTR(i.image,16384,'.$offset.') AS part_'.$offset;
		}

		$row = DBfetch(DBselect('SELECT '.implode(',', $columns).' FROM images i WHERE i.imageid='
			.zbx_dbstr($imageid)
		), false);

		if (!$row) {
			throw new DBException(_('Unable to read the Dameng image.'));
		}

		$length = (int) $row['image_length'];
		unset($row['image_length']);
		$image = self::decodeBlob(implode('', $row));

		if (strlen($image) !== $length) {
			throw new DBException(_('Incomplete Dameng image data.'));
		}

		return $image;
	}
}
