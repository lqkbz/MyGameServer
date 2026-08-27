#!/usr/bin/env bash
# 启动 redis/mysql 并确保 gs 库与账号存在(需 root:wsl -d Ubuntu -u root)
set -u
service redis-server start >/dev/null 2>&1
service mysql start >/dev/null 2>&1
sleep 1
redis-cli ping
mysql <<'SQL'
CREATE DATABASE IF NOT EXISTS gs;
CREATE USER IF NOT EXISTS 'gs'@'localhost' IDENTIFIED WITH mysql_native_password BY 'gs123';
GRANT ALL ON gs.* TO 'gs'@'localhost';
FLUSH PRIVILEGES;
SQL
mysql -ugs -pgs123 gs -e 'SELECT 1' >/dev/null 2>&1 && echo MYSQL_OK || echo MYSQL_FAIL
