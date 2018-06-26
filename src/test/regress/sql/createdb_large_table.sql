-- Create database using a template database containing a heap relation that
-- has been extended. The create database operation will read the heap relation
-- and write out to the destination file. That destination file must also be
-- extended as the copying of data happens.

DROP DATABASE IF EXISTS db_with_large_table;
DROP DATABASE IF EXISTS copy_of_db_with_large_table;
CREATE DATABASE db_with_large_table;
\c db_with_large_table

CREATE TABLE large_table(a) WITH (fillfactor=10) AS SELECT 1 FROM generate_series(1, 3100000)i DISTRIBUTED BY (a);
-- When table size is greater than 1G we expect the heap relation to extend.
SELECT pg_relation_size('large_table') > 1073741824;
\c regression

CREATE DATABASE copy_of_db_with_large_table TEMPLATE db_with_large_table;
