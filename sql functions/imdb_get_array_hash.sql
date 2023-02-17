CREATE OR REPLACE FUNCTION "public"."imdb_get_array_hash"(_float8, int4)
  RETURNS "pg_catalog"."int4" AS '$libdir/aqo', 'imdb_get_array_hash'
  LANGUAGE c VOLATILE STRICT
  COST 1