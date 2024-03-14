-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION aqo" to load this file. \quit

CREATE TABLE public.aqo_queries (
	query_hash		int PRIMARY KEY,
	learn_aqo		boolean NOT NULL,
	use_aqo			boolean NOT NULL,
	fspace_hash		int NOT NULL,
	auto_tuning		boolean NOT NULL,
	history_query   double precision[],
	num_history     int,
	total_num       int,
	current_query   double precision[],
	num_feature     int
);
/*the markov table, written by jim 2021.2.13*/
CREATE TABLE public.aqo_markov_table (
	hist_query_hash	   int PRIMARY KEY,
	distribution_query double precision[],
	frequent_query     double precision[]
);
/*the table records/updates best estimated cost and corresponing true cost respectivity, written by jim 2021.3.11*/
CREATE TABLE public.aqo_best_two_costs_table (
	query_pattern	   int PRIMARY KEY,
	nfeatures		   int NOT NULL,
	features           double precision[][],
	est_costs		   double precision[],
	true_costs		   double precision[]
);

CREATE TABLE public.aqo_query_texts (
	query_hash		int PRIMARY KEY REFERENCES public.aqo_queries ON DELETE CASCADE,
	query_text		varchar NOT NULL
);

CREATE TABLE public.aqo_query_stat (
	query_hash		int PRIMARY KEY REFERENCES public.aqo_queries ON DELETE CASCADE,
	execution_time_with_aqo					double precision[],
	execution_time_without_aqo				double precision[],
	planning_time_with_aqo					double precision[],
	planning_time_without_aqo				double precision[],
	cardinality_error_with_aqo				double precision[],
	cardinality_error_without_aqo			double precision[],
	executions_with_aqo						bigint,
	executions_without_aqo					bigint
);


CREATE TABLE public.aqo_data_lwpr (
	fspace_hash		int NOT NULL REFERENCES public.aqo_queries ON DELETE CASCADE,
	fsspace_hash	int NOT NULL,
	nfeatures		int NOT NULL,
	numRFS          int NOT NULL, 
	nReg            double precision[],
	trustworth      double precision[],
	slopeReady      double precision[],
	sum_e2          double precision[],
	beta0           double precision[],
	D               double precision[][],
	M               double precision[][],
    alpha           double precision[][],
	beta            double precision[][],
	c               double precision[][],
	SXresYres       double precision[][],
	SSs2            double precision[][],
    SSYres          double precision[][],
	SSXres          double precision[][],
	U               double precision[][],
	P               double precision[][],
	H               double precision[][],
	r               double precision[][],
	sum_w           double precision[][],
	sum_e_cv2       double precision[][],
	n_data          double precision[][],
	lambda          double precision[][],
	mean_x          double precision[][],
	var_x           double precision[][],
	s               double precision[][],
	slope           double precision[][],
	history_data_matrix   double precision[][],
	num_history_data      double precision[],
	ssp                   double precision[],
	history_error_matrix  double precision[][],
	num_history_error     double precision[],
	UNIQUE (fspace_hash, fsspace_hash)
);

CREATE TABLE public.aqo_data_house_lwpr (
	fspace_hash		int NOT NULL REFERENCES public.aqo_queries ON DELETE CASCADE,
	fsspace_hash	int NOT NULL,
	rf_hash         int NOT NULL,
	features		double precision[][],
	targets			double precision[],
	UNIQUE (fspace_hash, fsspace_hash, rf_hash)
);


CREATE INDEX aqo_queries_query_hash_idx ON public.aqo_queries (query_hash);
CREATE INDEX aqo_query_texts_query_hash_idx ON public.aqo_query_texts (query_hash);
CREATE INDEX aqo_query_stat_idx ON public.aqo_query_stat (query_hash);
CREATE INDEX aqo_fss_lwpr_access_idx ON public.aqo_data_lwpr (fspace_hash, fsspace_hash);
CREATE INDEX aqo_fss_lwpr_datahouse_idx ON public.aqo_data_house_lwpr (fspace_hash, fsspace_hash, rf_hash);
CREATE INDEX aqo_markov_table_idx ON public.aqo_markov_table (hist_query_hash);
CREATE INDEX aqo_best_two_costs_table_idx ON public.aqo_best_two_costs_table (query_pattern);

INSERT INTO public.aqo_queries VALUES (0, false, false, 0, false);
INSERT INTO public.aqo_query_texts VALUES (0, 'COMMON feature space (do not delete!)');
-- a virtual query for COMMON feature space

CREATE FUNCTION invalidate_deactivated_queries_cache() RETURNS trigger
	AS 'MODULE_PATHNAME' LANGUAGE C;

	
CREATE TRIGGER aqo_queries_invalidate AFTER UPDATE OR DELETE OR TRUNCATE
	ON public.aqo_queries FOR EACH STATEMENT
	EXECUTE PROCEDURE invalidate_deactivated_queries_cache();


ALTER TABLE public.aqo_query_texts ALTER COLUMN query_text TYPE text;


DROP INDEX public.aqo_queries_query_hash_idx CASCADE;
DROP INDEX public.aqo_query_texts_query_hash_idx CASCADE;
DROP INDEX public.aqo_query_stat_idx CASCADE;
DROP INDEX public.aqo_fss_lwpr_access_idx CASCADE;
DROP INDEX public.aqo_fss_lwpr_datahouse_idx CASCADE;
DROP INDEX public.aqo_markov_table_idx CASCADE;
DROP INDEX public.aqo_best_two_costs_table_idx CASCADE;

CREATE UNIQUE INDEX aqo_fss_lwpr_access_idx ON public.aqo_data_lwpr (fspace_hash, fsspace_hash);
CREATE UNIQUE INDEX aqo_fss_lwpr_datahouse_idx ON public.aqo_data_house_lwpr (fspace_hash, fsspace_hash, rf_hash);
CREATE UNIQUE INDEX aqo_markov_table_idx ON public.aqo_markov_table (hist_query_hash);
CREATE UNIQUE INDEX aqo_best_two_costs_table_idx ON public.aqo_best_two_costs_table (query_pattern);

CREATE OR REPLACE FUNCTION aqo_migrate_to_1_1_get_pk(rel regclass) RETURNS regclass AS $$
DECLARE
	idx regclass;
BEGIN
	SELECT i.indexrelid FROM pg_catalog.pg_index i JOIN
	pg_catalog.pg_attribute a ON a.attrelid = i.indrelid AND
								 a.attnum = ANY(i.indkey)
	WHERE i.indrelid = rel AND
		  i.indisprimary
	INTO idx;

	RETURN idx;
END
$$ LANGUAGE plpgsql;


DO $$
BEGIN
	EXECUTE format('ALTER TABLE %s RENAME to %s',
				   aqo_migrate_to_1_1_get_pk('public.aqo_queries'),
				   'aqo_queries_query_hash_idx');

	EXECUTE format('ALTER TABLE %s RENAME to %s',
				   aqo_migrate_to_1_1_get_pk('public.aqo_query_texts'),
				   'aqo_query_texts_query_hash_idx');

	EXECUTE format('ALTER TABLE %s RENAME to %s',
				   aqo_migrate_to_1_1_get_pk('public.aqo_query_stat'),
				   'aqo_query_stat_idx');
END
$$;


DROP FUNCTION aqo_migrate_to_1_1_get_pk(regclass);
