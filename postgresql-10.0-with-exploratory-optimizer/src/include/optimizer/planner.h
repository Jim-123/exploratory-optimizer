/*-------------------------------------------------------------------------
 *
 * planner.h
 *	  prototypes for planner.c.
 *
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/optimizer/planner.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PLANNER_H
#define PLANNER_H

#include "nodes/plannodes.h"
#include "nodes/relation.h"


/* Hook for plugins to get control in planner() */
typedef PlannedStmt *(*planner_hook_type) (Query *parse,
										   int cursorOptions,
										   ParamListInfo boundParams);
extern PGDLLIMPORT planner_hook_type planner_hook;

/* Hook for plugins to get control when grouping_planner() plans upper rels */
typedef void (*create_upper_paths_hook_type) (PlannerInfo *root,
											  UpperRelationKind stage,
											  RelOptInfo *input_rel,
											  RelOptInfo *output_rel);
extern PGDLLIMPORT create_upper_paths_hook_type create_upper_paths_hook;


extern PlannedStmt *planner(Query *parse, int cursorOptions,
		ParamListInfo boundParams);
extern PlannedStmt *standard_planner(Query *parse, int cursorOptions,
				 ParamListInfo boundParams);

extern PlannerInfo *subquery_planner(PlannerGlobal *glob, Query *parse,
				 PlannerInfo *parent_root,
				 bool hasRecursion, double tuple_fraction);

extern bool is_dummy_plan(Plan *plan);

extern RowMarkType select_rowmark_type(RangeTblEntry *rte,
					LockClauseStrength strength);

extern void mark_partial_aggref(Aggref *agg, AggSplit aggsplit);

extern Path *get_cheapest_fractional_path(RelOptInfo *rel,
							 double tuple_fraction);
/**
 * written by jim, balace explore value and cost
 * note: rate mean is the threshold, like cost(best_plan)*(1+rate).
 */
extern Path *get_cheapest_explore_fractional_path(RelOptInfo *rel,
							 double tuple_fraction, double rate);
extern Path *get_cheapest_explore_fractional_path2(RelOptInfo *rel,
							 double tuple_fraction, double rate1, Cost best_estimated_cost, double rate2);
extern Path *get_cheapest_explore_fractional_path3(RelOptInfo *rel,
							 double tuple_fraction, double rate1, Cost best_estimated_cost, double rate2);

extern Expr *expression_planner(Expr *expr);

extern Expr *preprocess_phv_expression(PlannerInfo *root, Expr *expr);

extern bool plan_cluster_use_sort(Oid tableOid, Oid indexOid);

extern List *get_partitioned_child_rels(PlannerInfo *root, Index rti);

/* modified by jim 2021.3.11*/
typedef void (*set_estimated_cost_hook_type) (Path *path);

extern PGDLLIMPORT set_estimated_cost_hook_type estimated_cost_hook;

#endif							/* PLANNER_H */
