#include "aqo.h"
#include "access/parallel.h"

/*****************************************************************************
 *
 *	QUERY PREPROCESSING HOOKS
 *
 * The main point of this module is to recognize somehow settings for the query.
 * It may be considered as user interface.
 *
 * The configurable settings are:
 *		'query_hash': hash of the type of the given query
 *		'use_aqo': whether to use AQO estimations in query optimization
 *		'learn_aqo': whether to update AQO data based on query execution
 *					 statistics
 *		'fspace_hash': hash of feature space to use with given query
 *		'auto_tuning': whether AQO may change use_aqo and learn_aqo values
 *					   for the next execution of such type of query using
 *					   its self-tuning algorithm
 *
 * Currently the module works as follows:
 * 1. Query type determination. We consider that two queries are of the same
 *		type if and only if they are equal or their difference is only in their
 *		constants. We use hash function, which returns the same value for all
 *		queries of the same type. This typing strategy is not the only possible
 *		one for adaptive query optimization. One can easily implement another
 *		typing strategy by changing hash function.
 * 2. New query type proceeding. The handling policy for new query types is
 *		contained in variable 'aqo.mode'. It accepts five values:
 *		"intelligent", "forced", "controlled", "learn" and "disabled".
 *		Intelligent linking strategy means that for each new query type the new
 *		separate feature space is created. The hash of new feature space is
 *		set the same as the hash of new query type. Auto tuning is on by
 *		default in this mode. AQO also automatically memorizes query hash to
 *		query text mapping in aqo_query_texts table.
 *		Forced linking strategy means that every new query type is linked to
 *		the common feature space with hash 0, but we don't memorize
 *		the hash and the settings for this query.
 *		Controlled linking strategy means that new query types do not induce
 *		new feature spaces neither interact AQO somehow. In this mode the
 *		configuration of settings for different query types lies completely on
 *		user.
 *		Learn linking strategy is the same as intelligent one. The only
 *		difference is the default settings for the new query type:
 *		auto tuning is disabled.
 *		Disabled strategy means that AQO is disabled for all queries.
 * 3. For given query type we determine its query_hash, use_aqo, learn_aqo,
 *		fspace_hash and auto_tuning parameters.
 * 4. For given fspace_hash we may use its machine learning settings, but now
 *		the machine learning setting are fixed for all feature spaces.
 *
 *****************************************************************************/

#define CREATE_EXTENSION_STARTSTRING_0 \
"-- complain if script is sourced in psql, rather than via CREATE EXTENSION"
#define CREATE_EXTENSION_STARTSTRING_1 \
"SELECT 1 FROM ONLY \"public\".\"aqo_queries\" x WHERE \"query_hash\"\
 OPERATOR(pg_catalog.=) $1 FOR KEY SHARE OF x"

static bool isQueryUsingSystemRelation(Query *query);
static bool isQueryUsingSystemRelation_walker(Node *node, void *context);

/*
 * Saves query text into query_text variable.
 * Query text field in aqo_queries table is for user.
 */
void
get_query_text(ParseState *pstate, Query *query)
{
	MemoryContext	oldCxt;

	/*
	 * Duplicate query string into private AQO memory context for guard
	 * from possible memory context switching.
	 */
	oldCxt = MemoryContextSwitchTo(AQOMemoryContext);
	if (pstate)
		query_text = pstrdup(pstate->p_sourcetext);
	MemoryContextSwitchTo(oldCxt);

	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query);
}

/*
 * Calls standard query planner or its previous hook.
 */
PlannedStmt *
call_default_planner(Query *parse,
					 int cursorOptions,
					 ParamListInfo boundParams)
{
	if (prev_planner_hook)
		return prev_planner_hook(parse, cursorOptions, boundParams);
	else
		return standard_planner(parse, cursorOptions, boundParams);
}

/*
 * Before query optimization we determine machine learning settings
 * for the query.
 * This hook computes query_hash, and sets values of learn_aqo,
 * use_aqo and is_common flags for given query.
 * Creates an entry in aqo_queries for new type of query if it is
 * necessary, i. e. AQO mode is "intelligent".
 */
PlannedStmt *
aqo_planner(Query *parse,
			int cursorOptions,
			ParamListInfo boundParams)
{
	bool		query_is_stored;
	Datum		query_params[10];
	bool		query_nulls[10] = {false, false, false, false, false, false, false, false, false, false};
    double     *query_history;
	int         num_history_data = 1;
	int         total_num = 1;
	int         test_vector_num=0;
	double     *current_query;
	int         num_feature;
	selectivity_cache_clear();
	query_context.explain_aqo = false;

	 /*
	  * We do not work inside an parallel worker now by reason of insert into
	  * heap during planning. Transactions is synchronized between parallel
	  * section. See GetCurrentCommandId() comments also.
	  */
	if ((parse->commandType != CMD_SELECT && parse->commandType != CMD_INSERT &&
	 parse->commandType != CMD_UPDATE && parse->commandType != CMD_DELETE) ||
		strncmp(query_text, CREATE_EXTENSION_STARTSTRING_0,
				strlen(CREATE_EXTENSION_STARTSTRING_0)) == 0 ||
		strncmp(query_text, CREATE_EXTENSION_STARTSTRING_1,
				strlen(CREATE_EXTENSION_STARTSTRING_1)) == 0 ||
		IsInParallelMode() || IsParallelWorker() ||
		aqo_mode == AQO_MODE_DISABLED || isQueryUsingSystemRelation(parse))
	{
		disable_aqo_for_query();
		return call_default_planner(parse, cursorOptions, boundParams);
	}

	INSTR_TIME_SET_CURRENT(query_context.query_starttime);
    
	//初始化query_context的相关内容
	query_context.nfeatures = 0;
	//判定查询的类型
	query_context.current_query_hash = get_query_hash(parse, query_text);

	//imdb 7 templates
    if(query_context.current_query_hash == 1370653224){
		query_context.current_query_hash = 1;
	}else if (query_context.current_query_hash == 164935887)
	{
		query_context.current_query_hash = 2;
	}else if (query_context.current_query_hash == -1029517898)
	{
		query_context.current_query_hash = 3;
	}else if (query_context.current_query_hash == 1392879147)
	{
		query_context.current_query_hash = 4;
	}else if (query_context.current_query_hash == -63216440)
	{
		query_context.current_query_hash = 5;
	}else if (query_context.current_query_hash == 933944866)
	{
		query_context.current_query_hash = 6;
	}else if (query_context.current_query_hash == 1409061130)
	{
		query_context.current_query_hash = 7;
	}else{
		query_context.current_query_hash = 0;
	}

	// 我们只处理特定的几个查询模板，其它查询均使用原估计方法，并且关闭aqo
	if (query_context.current_query_hash == 0)
	{
		disable_aqo_for_query();
		return call_default_planner(parse, cursorOptions, boundParams);
	}
    //我们令query_hash = 1，多个查询共同使用多个模型
	
	query_context.query_hash = 1;
	query_is_stored = find_query(query_context.query_hash, &query_params[0],
															&query_nulls[0]);
 
	if (!query_is_stored)
	{
		switch (aqo_mode)
		{
			case AQO_MODE_INTELLIGENT:
				query_context.adding_query = true;
				query_context.learn_aqo = true;
				query_context.use_aqo = false;
				query_context.fspace_hash = query_context.query_hash;
				query_context.auto_tuning = true;
				query_context.collect_stat = true;
				break;
			case AQO_MODE_FORCED:
				query_context.adding_query = false;
				query_context.learn_aqo = true;
				query_context.use_aqo = true;
				query_context.auto_tuning = false;
				query_context.fspace_hash = 0;
				query_context.collect_stat = false;
				break;
			case AQO_MODE_CONTROLLED:
				query_context.adding_query = false;
				query_context.learn_aqo = false;
				query_context.use_aqo = false;
				query_context.collect_stat = false;
				break;
			case AQO_MODE_LEARN:
				query_context.adding_query = true;
				query_context.learn_aqo = true;
				query_context.use_aqo = true;
				query_context.fspace_hash = query_context.query_hash;
				query_context.auto_tuning = true;
				query_context.collect_stat = true;
				break;
			case AQO_MODE_DISABLED:
				/* Should never happen */
				break;
			default:
				elog(WARNING,
					 "unrecognized mode in AQO: %d",
					 aqo_mode);
				break;
		}
		if (RecoveryInProgress())
		{
			if (aqo_mode == AQO_MODE_FORCED)
			{
				query_context.adding_query = false;
				query_context.learn_aqo = false;
				query_context.auto_tuning = false;
				query_context.collect_stat = false;
			}
			else
			{
				disable_aqo_for_query();
				return call_default_planner(parse, cursorOptions, boundParams);
			}
		}
		if (query_context.adding_query)
		{
			/*first, we construct a array to store history query*/
			query_history = palloc0(sizeof(*query_history) * num_history_data_compute_probability_fs);
			query_history[0] = query_context.current_query_hash;
			current_query = palloc0(sizeof(*current_query)*2);
			num_feature = 0;
			add_query(query_context.query_hash, query_context.learn_aqo, query_context.use_aqo, query_context.fspace_hash, query_context.auto_tuning, query_history, num_history_data, total_num, current_query, num_feature);
			add_query_text(query_context.query_hash, query_text);
		}
	}
	else
	{
		query_context.adding_query = false;
		query_context.learn_aqo = DatumGetBool(query_params[1]);
		query_context.use_aqo = DatumGetBool(query_params[2]);
		query_context.fspace_hash = DatumGetInt32(query_params[3]);
		query_context.auto_tuning = DatumGetBool(query_params[4]);
		num_history_data = DatumGetInt32(query_params[6]);
		total_num = DatumGetInt32(query_params[7]);
		query_context.collect_stat = query_context.auto_tuning;
		/* we also get the query history, modified by jim 2021.2.13*/
		query_history = palloc0(sizeof(*query_history) * num_history_data_compute_probability_fs);
		deform_vector(query_params[5], query_history, &test_vector_num);
		current_query = palloc0(sizeof(*current_query) * 2);
		deform_vector(query_params[8], current_query, &test_vector_num);
		num_feature = DatumGetInt32(query_params[9]);
        /*next we modified query_histroy, and also predict the next query template distribution*/
		if(num_history_data<num_history_data_compute_probability_fs){

			query_history[num_history_data] = query_context.current_query_hash;
			num_history_data += 1;
		}else
		{
			for(int j=1;j<num_history_data;j++){
				query_history[j-1] = query_history[j];
			}
			query_history[num_history_data-1] = query_context.current_query_hash;
		}
		//更新total_num, modified by jim 2021.3.9
		total_num = total_num + 1;
		update_query2(query_context.query_hash, query_context.learn_aqo, query_context.use_aqo, query_context.fspace_hash, query_context.auto_tuning, query_history, num_history_data, total_num);
		int query_history_hash = get_int_array_hash2(query_history, num_history_data_compute_probability_fs);
		load_query_distribution(num_history_data, query_history_hash, &query_context);
		//modified by jim in 2022.7.4, we consider the cost of each query
		calculate_current_best_estimate_cost(&query_context, query_context.current_query_hash, num_feature, current_query);
		query_context.nfeatures = num_feature;
		query_context.current_query_features = palloc0(sizeof(*query_context.current_query_features) * num_feature);
		for(int i=0;i<num_feature;i++){
			query_context.current_query_features[i] = current_query[i];
		}
		if (RecoveryInProgress())
		{
			query_context.learn_aqo = false;
			query_context.auto_tuning = false;
			query_context.collect_stat = false;
		}
	}
	query_context.explain_aqo = query_context.use_aqo;

	return call_default_planner(parse, cursorOptions, boundParams);
}

/*
 * Prints if the plan was constructed with AQO.
 */
void print_into_explain(PlannedStmt *plannedstmt, IntoClause *into,
			   ExplainState *es, const char *queryString,
			   ParamListInfo params, const instr_time *planduration)
{
	if (prev_ExplainOnePlan_hook)
		prev_ExplainOnePlan_hook(plannedstmt, into, es, queryString,
								params, planduration);
	if (query_context.explain_aqo)
	{
		/* Report to user about aqo state only in verbose mode */
		if (es->verbose)
			ExplainPropertyBool("Using aqo", true, es);
		query_context.explain_aqo = false;
	}
}

/*
 * Turn off all AQO functionality for the current query.
 */
void
disable_aqo_for_query(void)
{
	query_context.adding_query = false;
	query_context.learn_aqo = false;
	query_context.use_aqo = false;
	query_context.auto_tuning = false;
	query_context.collect_stat = false;
}

/*
 * Examine a fully-parsed query, and return TRUE iff any relation underlying
 * the query is a system relation.
 */
bool
isQueryUsingSystemRelation(Query *query)
{
	return isQueryUsingSystemRelation_walker((Node *) query, NULL);
}

bool
isQueryUsingSystemRelation_walker(Node *node, void *context)
{
	if (node == NULL)
		return false;

	if (IsA(node, Query))
	{
		Query	   *query = (Query *) node;
		ListCell   *rtable;

		foreach(rtable, query->rtable)
		{
			RangeTblEntry *rte = lfirst(rtable);

			if (rte->rtekind == RTE_RELATION)
			{
				Relation	rel = heap_open(rte->relid, AccessShareLock);
				bool		is_catalog = IsCatalogRelation(rel);

				heap_close(rel, AccessShareLock);
				if (is_catalog)
					return true;
			}
		}

		return query_tree_walker(query,
								 isQueryUsingSystemRelation_walker,
								 context,
								 0);
	}

	return expression_tree_walker(node,
								  isQueryUsingSystemRelation_walker,
								  context);
}
