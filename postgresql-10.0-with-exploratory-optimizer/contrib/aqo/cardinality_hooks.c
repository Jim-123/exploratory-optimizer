#include "aqo.h"

/*****************************************************************************
 *
 *	CARDINALITY ESTIMATION HOOKS
 *
 * This functions controls cardinality prediction in query optimization.
 * If use_aqo flag is false, then hooks just call default postgresql
 * cardinality estimator. Otherwise, they try to use AQO cardinality
 * prediction engine.
 * If use_aqo flag in true, hooks generate set of all clauses and all
 * absolute relids used in the relation being built and pass this
 * information to predict_for_relation function. Also these hooks compute
 * and pass to predict_for_relation marginal cardinalities for clauses.
 * If predict_for_relation returns non-negative value, then hooks assume it
 * to be true cardinality for given relation. Negative returned value means
 * refusal to predict cardinality. In this case hooks also use default
 * postgreSQL cardinality estimator.
 * 
 * update by jim for the exploratory optimizer
 *
 *****************************************************************************/
double ppi_explore_value = 0;

static void call_default_set_baserel_rows_estimate(PlannerInfo *root,
									   RelOptInfo *rel);
static double call_default_get_parameterized_baserel_size(PlannerInfo *root,
											RelOptInfo *rel,
											List *param_clauses);
static void call_default_set_joinrel_size_estimates(PlannerInfo *root,
										RelOptInfo *rel,
										RelOptInfo *outer_rel,
										RelOptInfo *inner_rel,
										SpecialJoinInfo *sjinfo,
										List *restrictlist);
static double call_default_get_parameterized_joinrel_size(PlannerInfo *root,
											RelOptInfo *rel,
											Path *outer_path,
											Path *inner_path,
											SpecialJoinInfo *sjinfo,
											List *restrict_clauses);


/*
 * Calls standard set_baserel_rows_estimate or its previous hook.
 */
void
call_default_set_baserel_rows_estimate(PlannerInfo *root, RelOptInfo *rel)
{
	if (prev_set_baserel_rows_estimate_hook)
		prev_set_baserel_rows_estimate_hook(root, rel);
	else
		set_baserel_rows_estimate_standard(root, rel);
}

/*
 * Calls standard get_parameterized_baserel_size or its previous hook.
 */
double
call_default_get_parameterized_baserel_size(PlannerInfo *root,
											RelOptInfo *rel,
											List *param_clauses)
{
	if (prev_get_parameterized_baserel_size_hook)
		return prev_get_parameterized_baserel_size_hook(root, rel, param_clauses);
	else
		return get_parameterized_baserel_size_standard(root, rel, param_clauses);
}

/*
 * Calls standard get_parameterized_joinrel_size or its previous hook.
 */
double
call_default_get_parameterized_joinrel_size(PlannerInfo *root,
											RelOptInfo *rel,
											Path *outer_path,
											Path *inner_path,
											SpecialJoinInfo *sjinfo,
											List *restrict_clauses)
{
	if (prev_get_parameterized_joinrel_size_hook)
		return prev_get_parameterized_joinrel_size_hook(root, rel,
														outer_path,
														inner_path,
														sjinfo,
														restrict_clauses);
	else
		return get_parameterized_joinrel_size_standard(root, rel,
													   outer_path,
													   inner_path,
													   sjinfo,
													   restrict_clauses);
}

/*
 * Calls standard set_joinrel_size_estimates or its previous hook.
 */
void
call_default_set_joinrel_size_estimates(PlannerInfo *root, RelOptInfo *rel,
										RelOptInfo *outer_rel,
										RelOptInfo *inner_rel,
										SpecialJoinInfo *sjinfo,
										List *restrictlist)
{
	if (prev_set_joinrel_size_estimates_hook)
		prev_set_joinrel_size_estimates_hook(root, rel,
											 outer_rel,
											 inner_rel,
											 sjinfo,
											 restrictlist);
	else
		set_joinrel_size_estimates_standard(root, rel,
											outer_rel,
											inner_rel,
											sjinfo,
											restrictlist);
}

/*
 * Our hook for setting baserel rows estimate.
 * Extracts clauses, their selectivities and list of relation relids and
 * passes them to predict_for_relation.
 */
void
aqo_set_baserel_rows_estimate(PlannerInfo *root, RelOptInfo *rel)
{
	double		predicted;
	Oid			relid;
	List	   *relids;
	List	   *selectivities = NULL;
	Explore_Value ev;
	lwpr_men_alloc_ev(&ev);
	if (query_context.use_aqo || query_context.learn_aqo)
		selectivities = get_selectivities(root, rel->baserestrictinfo, 0,
										  JOIN_INNER, NULL);

	if (!query_context.use_aqo)
	{
		if (query_context.learn_aqo)
			list_free_deep(selectivities);
		call_default_set_baserel_rows_estimate(root, rel);
		return;
	}

	relid = planner_rt_fetch(rel->relid, root)->relid;
	relids = list_make1_int(relid);
    if (aqo_mode == AQO_MODE_LEARN){
		// calculate the explore value of rel(not just cardinality)
		predict_for_relation_lwpr_explore(list_copy(rel->baserestrictinfo),
										selectivities,
										relids, &ev);
		if (ev.rows >= 0)
		{
			rel->rows = ev.rows;
			/* maybe some day, we will change the calculation method */
			rel->explore_value = (ev.est_uncof*ev.rate + ev.est_future*(1-ev.rate))*1;
            //rel->explore_value = 0;
		}else
		{
			//when this is no corresponding cardinality model, we just set explore_value = 1.(maybe some day we will optimize it)
			//modified by jim 20201228: when the base table have no caluse, then, rel->explore_value=0
			if(rel->baserestrictinfo == NIL){
				rel->explore_value = 0;
				call_default_set_baserel_rows_estimate(root, rel);
			}else{
				// whenever the base table have no corresponding model or the current model have lower accuacy for current query, we set explore_value = 1;
				rel->explore_value = 0.5;
				//rel->explore_value = ev.est_uncof*ev.rate + ev.est_future*(1-ev.rate);
				call_default_set_baserel_rows_estimate(root, rel);

			}
		}
	}
}

/*
 * Our hook for estimating parameterized baserel rows estimate.
 * Extracts clauses (including parametrization ones), their selectivities
 * and list of relation relids and passes them to predict_for_relation.
 */
double
aqo_get_parameterized_baserel_size(PlannerInfo *root,
								   RelOptInfo *rel,
								   List *param_clauses)
{
	double		predicted;
	Oid			relid = InvalidOid;
	List	   *relids = NULL;
	List	   *allclauses = NULL;
	List	   *selectivities = NULL;
	ListCell   *l;
	ListCell   *l2;
	int			nargs;
	int		   *args_hash;
	int		   *eclass_hash;
	int			current_hash;
	/* also try explore_value ,when get_selectivities, we take 0 instead of rel->relid*/
	Explore_Value ev;
    lwpr_men_alloc_ev(&ev);
	if (query_context.use_aqo || query_context.learn_aqo)
	{
		allclauses = list_concat(list_copy(param_clauses),
								 list_copy(rel->baserestrictinfo));
		selectivities = get_selectivities2(root,
										  allclauses,
										  0,
										  JOIN_INNER,
										  NULL);
		// selectivities = get_selectivities(root, allclauses, rel->relid,
		// 								  JOIN_INNER, NULL);
		relid = planner_rt_fetch(rel->relid, root)->relid;
		get_eclasses(allclauses, &nargs, &args_hash, &eclass_hash);
		forboth(l, allclauses, l2, selectivities)
		{
			current_hash = get_clause_hash2(
										((RestrictInfo *) lfirst(l))->clause,
										   nargs, args_hash, eclass_hash);
			cache_selectivity(current_hash, rel->relid, relid,
							  *((double *) lfirst(l2)));
		}
		pfree(args_hash);
		pfree(eclass_hash);
	}

	if (!query_context.use_aqo)
	{
		if (query_context.learn_aqo)
		{
			list_free_deep(selectivities);
			list_free(allclauses);
		}
		//rel->explore_value = 1;
		ppi_explore_value = 0;
		return call_default_get_parameterized_baserel_size(root, rel,
														   param_clauses);
	}

	relids = list_make1_int(relid);
    
    if (aqo_mode == AQO_MODE_LEARN){
		predict_for_relation_lwpr_explore(allclauses, selectivities, relids, &ev);
		if (ev.rows >= 0)
		{
			/* set the value of rel's explore value */
			ppi_explore_value = ev.est_uncof*ev.rate + ev.est_future*(1-ev.rate);
			return ev.rows;
		}
		else
		{
			ppi_explore_value = 1;
			return call_default_get_parameterized_baserel_size(root, rel,
															param_clauses);
		}
	}
}

/*
 * Our hook for setting joinrel rows estimate.
 * Extracts clauses, their selectivities and list of relation relids and
 * passes them to predict_for_relation.
 */
void
aqo_set_joinrel_size_estimates(PlannerInfo *root, RelOptInfo *rel,
							   RelOptInfo *outer_rel,
							   RelOptInfo *inner_rel,
							   SpecialJoinInfo *sjinfo,
							   List *restrictlist)
{
	double		predicted;
	List	   *relids;
	List	   *outer_clauses;
	List	   *inner_clauses;
	List	   *allclauses;
	List	   *selectivities;
	List	   *inner_selectivities;
	List	   *outer_selectivities;
	List	   *current_selectivities = NULL;
	bool        is_top_join = false;
	Explore_Value ev;
	lwpr_men_alloc_ev(&ev);
	if (query_context.use_aqo || query_context.learn_aqo)
		current_selectivities = get_selectivities(root, restrictlist, 0,
												  sjinfo->jointype, sjinfo);

	if (!query_context.use_aqo)
	{
		if (query_context.learn_aqo)
			list_free_deep(current_selectivities);
		//default 1
		//rel->explore_value = 1;
		call_default_set_joinrel_size_estimates(root, rel,
												outer_rel,
												inner_rel,
												sjinfo,
												restrictlist);
		return;
	}

	relids = get_list_of_relids(root, rel->relids);
	outer_clauses = get_path_clauses(outer_rel->cheapest_total_path, root,
									 &outer_selectivities);
	inner_clauses = get_path_clauses(inner_rel->cheapest_total_path, root,
									 &inner_selectivities);
	allclauses = list_concat(list_copy(restrictlist),
							 list_concat(outer_clauses, inner_clauses));
	selectivities = list_concat(current_selectivities,
								list_concat(outer_selectivities,
											inner_selectivities));
	if (aqo_mode == AQO_MODE_LEARN){
		predict_for_relation_lwpr_explore(allclauses, selectivities, relids, &ev);
		if (ev.rows >= 0)
		{
			rel->rows = ev.rows;
			rel->explore_value = (ev.est_uncof*ev.rate + ev.est_future*(1-ev.rate))*1;
		}else
		{
			//when this is no corresponding cardinality model, we just set explore_value = 1.(maybe some day we will optimize it)
			rel->explore_value = 1;
			call_default_set_joinrel_size_estimates(root, rel,
													outer_rel,
													inner_rel,
													sjinfo,
													restrictlist);
		}
	}
}

/*
 * Our hook for estimating parameterized joinrel rows estimate.
 * Extracts clauses (including parametrization ones), their selectivities
 * and list of relation relids and passes them to predict_for_relation.
 */
double
aqo_get_parameterized_joinrel_size(PlannerInfo *root,
								   RelOptInfo *rel,
								   Path *outer_path,
								   Path *inner_path,
								   SpecialJoinInfo *sjinfo,
								   List *restrict_clauses)
{
	double		predicted;
	List	   *relids;
	List	   *outer_clauses;
	List	   *inner_clauses;
	List	   *allclauses;
	List	   *selectivities;
	List	   *inner_selectivities;
	List	   *outer_selectivities;
	List	   *current_selectivities = NULL;
	Explore_Value ev;
    lwpr_men_alloc_ev(&ev);
	if (query_context.use_aqo || query_context.learn_aqo)
		current_selectivities = get_selectivities(root, restrict_clauses, 0,
												  sjinfo->jointype, sjinfo);

	if (!query_context.use_aqo)
	{
		if (query_context.learn_aqo)
			list_free_deep(current_selectivities);
		ppi_explore_value = 0;
		return call_default_get_parameterized_joinrel_size(root, rel,
														   outer_path,
														   inner_path,
														   sjinfo,
														   restrict_clauses);
	}

	relids = get_list_of_relids(root, rel->relids);
	outer_clauses = get_path_clauses(outer_path, root, &outer_selectivities);
	inner_clauses = get_path_clauses(inner_path, root, &inner_selectivities);
	allclauses = list_concat(list_copy(restrict_clauses),
							 list_concat(outer_clauses, inner_clauses));
	selectivities = list_concat(current_selectivities,
								list_concat(outer_selectivities,
											inner_selectivities));
	if (aqo_mode == AQO_MODE_LEARN){
		predict_for_relation_lwpr_explore(allclauses, selectivities, relids, &ev);
		if (ev.rows >= 0)
		{
			ppi_explore_value = ev.est_uncof*ev.rate + ev.est_future*(1-ev.rate);
			return ev.rows;
		}
		else
		{
			ppi_explore_value = 1;
			return call_default_get_parameterized_joinrel_size(root, rel,
															outer_path,
															inner_path,
															sjinfo,
															restrict_clauses);
		}
	}
}
/**
 *  used to pass parametered's explore value
 * */
void
ppi_hook(ParamPathInfo *ppi)
{
	ppi->ppi_explore_value = ppi_explore_value;
}
