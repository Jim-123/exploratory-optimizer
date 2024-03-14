#include "aqo.h"

PG_MODULE_MAGIC;

void _PG_init(void);


/* Strategy of determining feature space for new queries. */
int			aqo_mode;

/* GUC variables */
static const struct config_enum_entry format_options[] = {
	{"intelligent", AQO_MODE_INTELLIGENT, false},
	{"forced", AQO_MODE_FORCED, false},
	{"controlled", AQO_MODE_CONTROLLED, false},
	{"learn", AQO_MODE_LEARN, false},
	{"disabled", AQO_MODE_DISABLED, false},
	{NULL, 0, false}
};

/* Parameters of autotuning */
int			aqo_stat_size = 20;
int			auto_tuning_window_size = 5;
double		auto_tuning_exploration = 0.1;
int			auto_tuning_max_iterations = 50;
int			auto_tuning_infinite_loop = 8;

/* stat_size > infinite_loop + window_size + 3 is required for auto_tuning*/

/* Machine learning parameters */
double		object_selection_prediction_threshold = 0.3;
double		object_selection_object_threshold = 0.1;
double		learning_rate = 1e-1;
double		log_selectivity_lower_bound = -30; //used for statistic features
int			aqo_k = 2;
int			aqo_K = 30;

////////////////////////////////key parameters used for exploratory optimizer//////////////////////////////
int         num_history_data_compute_probability_rf = 10;
double      confidence_bound_percentile = 0.01;
double      rate_between_explore_value = 0.6;     //the rate between MV(V) and FV(V)
double      rate_to_generate_explore_plan = 0.01; //teh rate used for exploratory optimizer
double      outer_future_value = 1; // the default future value for outer data
/* this parameters control how to combine ml-based ce and default ce, maybe future work*/
double      use_aqo_threshold = 1e-5;
int         num_pred_error_history = 2;
double      avg_error_threshold = 1;
/*the number of history queries that used to predict the distribution of future queries(use markov model)*/
int         num_history_data_compute_probability_fs = 3;
int         num_query_pattern = 7;
/*use our learned cost model?--->maybe future work*/
int         num_two_costs_save = 66; /*the number of query's two best costs we need to save for each query template*/
double      rate_to_compare_best_est_cost = 1;
/*prune the plan with higher cost for given workload*/
double      prune_rate_for_add_path_explore = 1.01;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * Currently we use it only to store query_text string which is initialized
 * after a query parsing and is used during the query planning.
 */
MemoryContext		AQOMemoryContext;
QueryContextData	query_context;
char				*query_text = NULL;

/* Saved hook values */
post_parse_analyze_hook_type				prev_post_parse_analyze_hook;
planner_hook_type							prev_planner_hook;
ExecutorStart_hook_type						prev_ExecutorStart_hook;
ExecutorEnd_hook_type						prev_ExecutorEnd_hook;
set_baserel_rows_estimate_hook_type			prev_set_baserel_rows_estimate_hook;
get_parameterized_baserel_size_hook_type	prev_get_parameterized_baserel_size_hook;
set_joinrel_size_estimates_hook_type		prev_set_joinrel_size_estimates_hook;
get_parameterized_joinrel_size_hook_type	prev_get_parameterized_joinrel_size_hook;
copy_generic_path_info_hook_type			prev_copy_generic_path_info_hook;
ExplainOnePlan_hook_type					prev_ExplainOnePlan_hook;
//path
join_search_hook_type                       prev_join_search_hook;
set_rel_pathlist_hook_type                  prev_set_rel_pathlist_hook;
/*****************************************************************************
 *
 *	CREATE/DROP EXTENSION FUNCTIONS
 *
 *****************************************************************************/

void
_PG_init(void)
{
	DefineCustomEnumVariable("aqo.mode",
							 "Mode of aqo usage.",
							 NULL,
							 &aqo_mode,
							 AQO_MODE_CONTROLLED,
							 format_options,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	prev_planner_hook							= planner_hook;
	planner_hook								= aqo_planner;
	prev_post_parse_analyze_hook				= post_parse_analyze_hook;
	post_parse_analyze_hook						= get_query_text;
	prev_ExecutorStart_hook						= ExecutorStart_hook;
	ExecutorStart_hook							= aqo_ExecutorStart;
	prev_ExecutorEnd_hook						= ExecutorEnd_hook;
	ExecutorEnd_hook							= learn_query_stat;
	prev_set_baserel_rows_estimate_hook			= set_baserel_rows_estimate_hook;
	set_baserel_rows_estimate_hook				= aqo_set_baserel_rows_estimate;
	prev_get_parameterized_baserel_size_hook	= get_parameterized_baserel_size_hook;
	get_parameterized_baserel_size_hook			= aqo_get_parameterized_baserel_size;
	prev_set_joinrel_size_estimates_hook		= set_joinrel_size_estimates_hook;
	set_joinrel_size_estimates_hook				= aqo_set_joinrel_size_estimates;
	prev_get_parameterized_joinrel_size_hook	= get_parameterized_joinrel_size_hook;
	get_parameterized_joinrel_size_hook			= aqo_get_parameterized_joinrel_size;
	prev_copy_generic_path_info_hook			= copy_generic_path_info_hook;
	copy_generic_path_info_hook					= aqo_copy_generic_path_info;
	prev_ExplainOnePlan_hook					= ExplainOnePlan_hook;
	ExplainOnePlan_hook							= print_into_explain;
	//join hook
	prev_join_search_hook                       = join_search_hook;
	join_search_hook                            = aqo_join_search;
	//set the explore value of base table
    prev_set_rel_pathlist_hook                  = set_rel_pathlist_hook;
	set_rel_pathlist_hook                       = aqo_set_rel_pathlist;
	parampathinfo_postinit_hook					= ppi_hook;
	estimated_cost_hook                         = aqo_estimated_cost_hook;
	init_deactivated_queries_storage();
	AQOMemoryContext = AllocSetContextCreate(TopMemoryContext, "AQOMemoryContext", ALLOCSET_DEFAULT_SIZES);
}

PG_FUNCTION_INFO_V1(invalidate_deactivated_queries_cache);

/*
 * Clears the cache of deactivated queries if the user changed aqo_queries
 * manually.
 */
Datum
invalidate_deactivated_queries_cache(PG_FUNCTION_ARGS)
{
	fini_deactivated_queries_storage();
	init_deactivated_queries_storage();
	PG_RETURN_POINTER(NULL);
}

/*我们需要的hash函数,在存储过程中需要调用*/
PG_FUNCTION_INFO_V1(imdb_get_array_hash);
Datum imdb_get_array_hash(PG_FUNCTION_ARGS)
{
   int32   num = PG_GETARG_INT32(1);
   double *array;
   array = palloc0(sizeof(*array) * num);
   deform_vector(PG_GETARG_DATUM(0), array, &num);
   int32   hash = get_int_array_hash2(array, num);
   PG_RETURN_INT32(hash);
}
