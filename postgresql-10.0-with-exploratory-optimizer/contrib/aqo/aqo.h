/*
 * aqo.h
 *		Adaptive query optimization extension
 *
 * Adaptive query optimization is the kind of query optimization in which
 * the execution statistics from previously executed queries is used.
 * Adaptive query optimization extends standard PostgreSQL cost-based query
 * optimization model.
 * This extension uses machine learning model built over the collected
 * statistics to improve cardinality estimations.
 *
 * The extension organized as follows.
 *
 * Query type or query hash is an integer number. Queries belongs to the same
 * type if they have the same structure, i. e. their difference lies only in
 * their constant values.
 * The settings of handling for query types are contained in aqo_queries table.
 * Examples of query texts for different query types are available in
 * aqo_query_texts table.
 * Query types are linked to feature spaces.
 *
 * Feature space is the set of machine learning models and their settings
 * used for cardinality prediction. The statistics of query types from one
 * feature space will interact. This interaction may be constructive or
 * destructive, which leads to performance improvement or performance
 * degradation respectively.
 * Feature spaces are described by their hashes (an integer value).
 *
 * This extension presents four default modes:
 * "intelligent" mode tries to automatically tune AQO settings for the current
 * workload. It creates separate feature space for each new type of query
 * and then tries to improve the performance of such query type execution.
 * The automatic tuning may be manually deactivated for some queries.
 * "learn" mode creates separate feature space and enabled aqo learning and
 * usage for each new type of query. In general it is similar to "intelligent"
 * mode, but without auto_tuning setting enabled by default.
 * "forced" mode makes no difference between query types and use AQO for them
 * all in the similar way. It considers each new query type as linked to special
 * feature space called COMMON with hash 0.
 * "controlled" mode ignores unknown query types. In this case AQO is completely
 * configured manually by user.
 * "disabled" mode ignores all queries.
 * Current mode is stored in aqo.mode variable.
 *
 * User can manually set up his own feature space configuration
 * for query types by changing settings in table aqo_queries.
 *
 * Module preprocessing.c determines how to handle the given query.
 * This includes following questions: whether to use AQO for this query,
 * whether to use execution statistics of this query to update machine
 * learning models, to what feature space the query belongs to, and whether
 * this query allows using intelligence autotuning for three previous questions.
 * This data is stored in aqo_queries table. Also this module links
 * new query types to their feature spaces according to aqo.mode.
 *
 * If it is supposed to use AQO for given type of query, the extension hooks
 * cardinality estimation functions in PostgreSQL. If the necessary statistics
 * for cardinality predictions using machine learning method is available,
 * the extension performs the prediction and returns its value. Otherwise it
 * refused to predict and returns control to standard PostgreSQL cardinality
 * estimator.
 * Modules cardinality_hooks.c and cardinality_estimation.c are responsible
 * for this part of work.
 *
 * If it is supposed to use execution statistics of given query for learning
 * models in AQO, the extension sets flag before execution to collect rows
 * statistics. After query execution the collected statistics is proceed in
 * the extension and the update of related feature space models is performed.
 * Module postprocessing.c is responsible for this part of work.
 * Also it saves query execution time and cardinality qualities of queries
 * for further analysis by AQO and DBA.
 *
 * Note that extension is transaction-dependent. That means that user has to
 * commit transaction to make model updates visible for all backends.
 *
 * More details on how cardinality estimation and models learning works.
 *
 * For each node we consider it induced feature subspace. Two nodes belongs
 * to the same feature subspace if their base relations are equal, their
 * clause sets are similar (i. e. their clauses may differ only by constant
 * values), and their classes of equivalence with size more than two are common.
 *
 * For each feature subspace we consider the selectivities of clauses which are
 * not in one of three-or-more-variables equivalence class as features of the
 * node. So each node is mapped into real-valued vector in the unit hypercube.
 * So our statistics for feature subspace is a set of such vectors with true
 * cardinalities of their corresponding nodes.
 *
 * That is how we state machine learning problem: we build the regressor from
 * each feature subspace (i. e. from clause selectivities) to cardinality.
 * More precisely, we regress vector of logarithms of clause selectivities to
 * logarithm of cardinality (that was done to set the scale which is  suitable
 * to the problem semantics and to the machine learning method). To aviod -infs
 * we lower bounded logarithms of cardinalities with 0 and logarithms of
 * selectivities with -30.
 *
 * The details of the machine learning method are available in module
 * machine_learning.c.
 *
 * Modules path_utils.c and utils.c are described by their names.
 *
 * Module hash.c computes hashes of queries and feature subspaces. The hashes
 * fulfill the properties described below.
 *
 * Module storage.c is responsible for storage query settings and models
 * (i. e. all information which is used in extension).
 *
 * Copyright (c) 2016-2018, Postgres Professional
 *
 * IDENTIFICATION
 *	  contrib/aqo/aqo.h
 */
#ifndef __ML_CARD_H__
#define __ML_CARD_H__

#include <math.h>

#include "postgres.h"

#include "fmgr.h"

#include "access/hash.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/namespace.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/pg_type.h"
#include "catalog/pg_operator.h"
#include "commands/explain.h"
#include "executor/executor.h"
#include "executor/execdesc.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/planmain.h"
#include "optimizer/planner.h"
#include "optimizer/cost.h"
#include "optimizer/paths.h"
#include "optimizer/pathnode.h"
#include "parser/analyze.h"
#include "parser/parsetree.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/tqual.h"
#include "utils/fmgroids.h"
#include "utils/snapmgr.h"


/* Check PostgreSQL version (9.6.0 contains important changes in planner) */
#if PG_VERSION_NUM < 90600
	#error "Cannot build aqo with PostgreSQL version lower than 9.6.0"
#endif


/* Strategy of determining feature space for new queries. */
typedef enum
{
	/* Creates new feature space for each query type with auto-tuning enabled */
	AQO_MODE_INTELLIGENT,
	/* Treats new query types as linked to the common feature space */
	AQO_MODE_FORCED,
	/* New query types are not linked with any feature space */
	AQO_MODE_CONTROLLED,
	/* Creates new feature space for each query type without auto-tuning */
	AQO_MODE_LEARN,
	/* Aqo is disabled for all queries */
	AQO_MODE_DISABLED,
}	AQO_MODE;
extern int	aqo_mode;

typedef struct
{
	double	   *execution_time_with_aqo;
	double	   *execution_time_without_aqo;
	double	   *planning_time_with_aqo;
	double	   *planning_time_without_aqo;
	double	   *cardinality_error_with_aqo;
	double	   *cardinality_error_without_aqo;
	int			execution_time_with_aqo_size;
	int			execution_time_without_aqo_size;
	int			planning_time_with_aqo_size;
	int			planning_time_without_aqo_size;
	int			cardinality_error_with_aqo_size;
	int			cardinality_error_without_aqo_size;
	int64		executions_with_aqo;
	int64		executions_without_aqo;
}	QueryStat;

/* Parameters for current query */
typedef struct QueryContextData
{
	int		query_hash;
	bool		learn_aqo;
	bool		use_aqo;
	int		fspace_hash;
	bool		auto_tuning;
	bool		collect_stat;
	bool		adding_query;
	bool		explain_only;
	bool		explain_aqo;
	/* Query execution time */
	instr_time	query_starttime;
	double		query_planning_time;
   /* Query_hash, modified by jim 2021.2.13*/
   int      current_query_hash;
   double  *query_distribution;
   int      nfeatures;
   double     *current_query_features; /*modified by jim 2021.3.11*/
   Cost    best_est_cost;
   Cost    best_pred_cost;
   /**/
} QueryContextData;

/* Parameters of autotuning */
extern int	aqo_stat_size;
extern int	auto_tuning_window_size;
extern double auto_tuning_exploration;
extern int	auto_tuning_max_iterations;
extern int	auto_tuning_infinite_loop;

/* Machine learning parameters */
extern double object_selection_prediction_threshold;
extern double object_selection_object_threshold;
extern double learning_rate;
extern int	aqo_k;
extern int	aqo_K;
extern double log_selectivity_lower_bound;
/* the num data to compute the happen probability of RF, which used to compute explore value of current query */
extern int num_history_data_compute_probability_rf;
extern double confidence_bound_percentile;
extern double rate_between_explore_value; /*the rate used to calculate explore value*/
extern double rate_to_generate_explore_plan; /* the rate satisfy cost(p)<= cost(opt_p)*(1+rate_to_generate_explore_plan)*/
extern double use_aqo_threshold; /* when use aqo, sometimes, it's better to use default estimation */
extern double outer_future_value; /* when the current query is an outiler, how to calculate the est_future(0 or 1)*/
extern int    num_pred_error_history; /*how much error of RF to save*/
extern double avg_error_threshold; /*this threshold is used to determine whether using this rf to predict*/
extern int    num_history_data_compute_probability_fs;
extern int    num_query_pattern;
extern int    num_two_costs_save;  
extern double rate_to_compare_best_est_cost; /* the rate between estimate cost, modified by jim 2021.3.15*/
extern double prune_rate_for_add_path_explore;
extern int    cardinality_type;
/* Locally weighted projection regression parameters */
//1. 定义 kernel 的类型
typedef enum {
   LWPR_GAUSSIAN_KERNEL, LWPR_BISQUARE_KERNEL
} LWPR_Kernel;

/*定义一个结构，用于输出3个值：基数值、不可信度、未来价值 */
typedef struct{
   double rows;
   double est_uncof;
   double est_future;
   double rate; /*range [0,1] represent the combine rate between est_conf and est_future*/
}Explore_Value;

// 2. 定义工作空间
typedef struct LWPR_Workspace {
   int *derivOk;           /**< \brief Used within lwpr_aux_update_distance_metric for storing which PLS directions can be trusted */
   double *storage;        /**< \brief Pointer to the allocated memory */
   double *dx;             /**< \brief Used to hold the difference between a normalised input vector and a RF's centre */
   double *dwdM;           /**< \brief Derivatives of the weight w with respect to LWPR_ReceptiveField.M */
   double *dJ2dM;          /**< \brief Derivatives of the cost J2 with respect to M */
   double *ddwdMdM;        /**< \brief 2nd derivatives of w wrt. M */
   double *ddJ2dMdM;       /**< \brief 2nd derivatives of J2 wrt. M */
   double *Ps;             /**< \brief Intermediate results used within lwpr_aux_update_distance_metric */
   double *Pse;            /**< \brief Intermediate results used within lwpr_aux_update_distance_metric */
   double *xu;             /**< \brief Used within PLS calculations (updated x) */
   double *yres;           /**< \brief Intermediate results used within lwpr_aux_update_regression */
   double *ytarget;        /**< \brief Intermediate results used within lwpr_aux_update_regression */
   double *xres;           /**< \brief Intermediate results used within lwpr_aux_update_regression */
   double *xc;             /**< \brief Used to hold the difference between a normalised input vector and a RF's centre */
   double *xmz;            /**< \brief Updated mean of a RF */
   double *e_cv;           /**< \brief Intermediate results used within lwpr_aux_update_regression */
   double *s;              /**< \brief Intermediate results used within lwpr_aux_update_regression */
   double *dsdx;           /**< \brief Intermediate results used within lwpr_aux_predict_one_J */
   double *Dx;             /**< \brief Used to store RF.D * (x-RF.c) */
   double *sum_dwdx;       /**< \brief Intermediate results used within lwpr_aux_predict_one_J */
   double *sum_ydwdx_wdydx;/**< \brief Intermediate results used within lwpr_aux_predict_one_J */
   double *sum_ddwdxdx;    /**< \brief Intermediate results used within lwpr_aux_predict_one_gH */
   double *sum_ddRdxdx;    /**< \brief Intermediate results used within lwpr_aux_predict_one_gH */   
} LWPR_Workspace;

//3. 定义有效域
typedef struct {
   int nReg;           /**< \brief The number of PLS regression directions */
   int nRegStore;      /**< \brief The number of PLS directions that can be stored before a re-allocation is necessary */
   
   double *fixStorage; /**< \brief A pointer to memory that is independent of nReg */
   double *varStorage; /**< \brief A pointer to memory that might have to be re-allocated (nReg) */
   
   int trustworthy;    /**< \brief This flag indicates whether a receptive field has "seen" enough data so that its predictions can be trusted */ 
   int slopeReady;     /**< \brief Indicates whether the vector "slope" can be used instead of doing PLS calculatations */    
   double w;           /**< \brief The current activation (weight) */
   double sum_e2;      /**< \brief The accumulated prediction error on the training data */
   double beta0;       /**< \brief Constant part of the PLS output */
   double SSp;         /**< \brief Sufficient statistics used for the confidence bounds */
           
   double *D;          /**< \brief Distance metric (NxN) */
   double *M;          /**< \brief Cholesky factorization of the distance metric (NxN) */
   double *alpha;      /**< \brief Learning rates for updates to M (NxN) */
   double *beta;       /**< \brief PLS regression coefficients (Rx1) */
   double *c;          /**< \brief The centre of the receptive field (Nx1) */
   double *SXresYres;  /**< \brief Sufficient statistics for the PLS regression axes LWPR_ReceptiveField.U (NxR) */
   double *SSs2;       /**< \brief Sufficient statistics for PLS loadings s (Rx1) */
   double *SSYres;     /**< \brief Sufficient statistics for PLS coefficients beta (Rx1) */
   double *SSXres;     /**< \brief Sufficient statistics for PLS input reductions P (NxR) */
   double *U;          /**< \brief PLS regression axes (NxR) */
   double *P;          /**< \brief PLS input reduction parameters (NxR) */
   double *H;          /**< \brief Sufficient statistics for distance metric updates (Rx1) */
   double *r;          /**< \brief Sufficient statistics for distance metric updates (Rx1) */
   double *h;          /**< \brief Sufficient statistics for 2nd order distance metric updates (NxN) */
   double *b;          /**< \brief Memory terms for 2nd order updates to M (NxN) */
   double *sum_w;      /**< \brief Accumulated activation w per PLS direction (Rx1) */
   double *sum_e_cv2;  /**< \brief Accumulated CV-error on training data (Rx1) */
   double *n_data;     /**< \brief Number of training data each PLS direction has seen (Rx1) */
   double *lambda;     /**< \brief Forgetting factor per PLS direction (Rx1) */
   double *mean_x;     /**< \brief Mean of the training data this RF has seen (Nx1) */
   double *var_x;      /**< \brief Variance of the training data this RF has seen (Nx1) */
   double *s;          /**< \brief Current PLS loadings (Rx1) */
   double *slope;      /**< \brief Slope of the local model (Nx1). This avoids PLS calculations when no updates are performed anymore. */
   /* also add happen probability of this RF in give timestamp */
   double conf_rf;     /**< \brief the confidence given the confidence bound */
   double *prob_rf;     /**< \brief the happen probability of this rf, modified by jim 2021.2.15 */
   /* we add predicted error array for deciding whether or not use this RF, add by jim 2021.1.21*/
   double *pred_error_history;
   double pred_error_num;
} LWPR_ReceptiveField;

//4. 定义 Lwpr 模型
typedef struct LWPR_Model {
   //输入的维数
   int nIn;             /**< \brief Number N of input dimensions */
   int nInStore;        /**< \brief Storage-size of any N-vector, for aligment purposes */
   //正则化输入输出
   double *norm_in;     /**< \brief Input normalisation (Nx1). Adjust this to the expected variation of your data. */
   double norm_out;    /**< \brief Output normalisation. Adjust this to the expected variation of your output data. */
   int  n_data;
   //超参数
   int diag_only;       /**< \brief Flag that determines whether distance matrices are handled as diagonal-only */
   double penalty;      /**< \brief Penalty factor used within distance metric updates */
   double *init_D;      /**< \brief Initial distance metric (NxN). This often requires some tuning (NxN) */
   double *init_M;      /**< \brief Cholesky factorisation of LWPR_Model.init_D (NxN) */
   double *init_alpha;  /**< \brief Initial Learning rate(NxN)*/
   double w_gen;        /**< \brief Threshold that determines the minimum activation before a new RF is created. */
   double w_prune;      /**< \brief Threshold that determines above which (second highest) activation a RF is pruned. */
   double init_lambda;  /**< \brief Initial forgetting factor */
   double final_lambda; /**< \brief Final forgetting factor */
   double tau_lambda;   /**< \brief This parameter describes the annealing schedule of the forgetting factor */
   double init_S2;      /**< \brief Initial value for sufficient statistics LWPR_ReceptiveField.SSs2 */
   double add_threshold;/**< \brief Threshold that determines when a new PLS regression axis is added */
   LWPR_Kernel kernel;  /**< \brief Describes which kernel function is used (Gaussian or BiSquare) */
   int update_D;        /**< \brief Flag that determines whether distance metric updates are performed (default: 1) */
   //接受域
   int numRFS;          /**< \brief The number of receptive fields (see LWPR_ReceptiveField) */
   int numPointers;     /**< \brief The number of RFs that can be stored before a re-allocation is necessary */
   int n_pruned;        /**< \brief Number of RFs that were pruned during training */
   //不需要存储
   LWPR_ReceptiveField **rf;
   LWPR_Workspace *ws;  /**< \brief Array of Workspaces, one for each thread (cf. LWPR_NUM_THREADS) */
   double *storage;     /**< \brief Pointer to allocated memory. Do not touch. */
   double *xn;          /**< \brief Used to hold a normalised input vector (Nx1) */
   double yn;          /**< \brief Used to hold a normalised output vector (Nx1) */
   int    fss_hash;    /** save current hash value*/
   //求探索价值
   Explore_Value *explore_values; /* save the explore value of current query */
   //保留最近的num_history_data_compute_probability_rf个查询
   double **history_data_matrix;
   //当前保存的历史数据个数
   double *num_history_data;
} LWPR_Model;



// 5.模型训练或者预测线程
typedef struct {
   //当前模型
   LWPR_Model *model;/**< \brief Pointer to the LWPR_Model */
   LWPR_Workspace *ws;
   //输入输出相关
   const double *xn;       /**< \brief Normalised input vector (Nx1) */
   double yn;              /**< \brief Normalised output, dim-th element of normalised output vector */
   double cutoff;          /**< \brief Threshold determining the minimal activation for updating a RF */
   double sum_w;           /**< \brief Sum of activations */
   double yp;              /**< \brief Sum of un-normalised predictions of the RFs handled by this thread */
   //最大和次大核距离以及其索引
   double w_max;           /**< \brief Largest activation encountered in this thread */
   double w_sec;           /**< \brief Second largest activation encountered in this thread */
   int ind_max;            /**< \brief Index of RF with largest activation */
   int ind_sec;            /**< \brief Index of RF with second largest activation */
} LWPR_ThreadData;


/* Parameters for current query */
extern QueryContextData query_context;
extern char				*query_text;

/* Memory context for long-live data */
extern MemoryContext AQOMemoryContext;

/* Saved hook values in case of unload */
extern post_parse_analyze_hook_type prev_post_parse_analyze_hook;
extern planner_hook_type prev_planner_hook;
extern ExecutorStart_hook_type prev_ExecutorStart_hook;
extern ExecutorEnd_hook_type prev_ExecutorEnd_hook;
extern		set_baserel_rows_estimate_hook_type
			prev_set_baserel_rows_estimate_hook;
extern		get_parameterized_baserel_size_hook_type
			prev_get_parameterized_baserel_size_hook;
extern		set_joinrel_size_estimates_hook_type
			prev_set_joinrel_size_estimates_hook;
extern		get_parameterized_joinrel_size_hook_type
			prev_get_parameterized_joinrel_size_hook;
extern		copy_generic_path_info_hook_type
			prev_copy_generic_path_info_hook;
extern ExplainOnePlan_hook_type prev_ExplainOnePlan_hook;
//our explore method to generate plan
extern join_search_hook_type prev_join_search_hook;
extern set_rel_pathlist_hook_type prev_set_rel_pathlist_hook;

/* Hash functions */
int			get_query_hash(Query *parse, const char *query_text);
void get_fss_for_object(List *clauselist, List *selectivities, List *relidslist,
				   int *nfeatures, int *fss_hash, double **features);
int  get_int_array_hash2(double *arr, int len);
void		get_eclasses(List *clauselist, int *nargs, int **args_hash, int **eclass_hash);
int			get_clause_hash(Expr *clause, int nargs, int *args_hash, int *eclass_hash);
int			get_clause_hash2(Expr *clause, int nargs, int *args_hash, int *eclass_hash);
/* Storage interaction */
bool find_query(int query_hash,
		   Datum *search_values,
		   bool *search_nulls);
bool add_query(int query_hash, bool learn_aqo, bool use_aqo,
		  int fspace_hash, bool auto_tuning, double *query_history, int num_history, int total_num, double *current_query, int num_feature);
bool update_query(int query_hash, bool learn_aqo, bool use_aqo,
			 int fspace_hash, bool auto_tuning);
bool update_query2(int query_hash, bool learn_aqo, bool use_aqo,
			 int fspace_hash, bool auto_tuning, double *query_history, int num_history, int total_num);
bool add_query_text(int query_hash, const char *query_text);
bool load_fss(int fss_hash, int ncols,
		 double **matrix, double *targets, int *rows);
bool load_rf_datahouse(int fss_hash, int rf_hash, double **matrix, double *targets, int *rows);
bool load_best_two_costs(int query_pattern, double **matrix, double *est_cost, double *true_cost, int *rows, int nfeatures); //modified by jim 2021.3.11
bool load_fss_rfwr(int fss_hash, int ncols, LWPR_Model *model);
/*add by jim 2021.2.13*/
bool load_query_distribution(int num_history_data, int query_history_hash, QueryContextData *query_context);
bool load_query_distribution2(int query_hash, QueryContextData *query_context);
// bool add_collect_data(int fss_hash, int nfeature,
// 		 double *features, double targets, double predicts);
// bool add_collect_data2(int fss_hash, List *relidslist, List *clauselist, List *selectivities, double targets);
bool update_fss(int fss_hash, int nrows, int ncols,
		   double **matrix, double *targets,
		   int old_nrows, List *changed_rows);
bool update_rf_datahouse(int fss_hash, int rf_hash, int ncols, int nrows, double **matrix, double *targets);
bool update_best_two_costs(int query_pattern, int ncols, int nrows, double **matrix, double *est_cost, double *true_cost); //modified by jim 2021.3.11
bool update_fss_rfwr(int fss_hash, int nfeature, LWPR_Model *model);
bool update_fss_rfwr2(int fss_hash, int nfeature, LWPR_Model *model);
QueryStat  *get_aqo_stat(int query_hash);
void		update_aqo_stat(int query_hash, QueryStat * stat);
void		init_deactivated_queries_storage(void);
void		fini_deactivated_queries_storage(void);
bool		query_is_deactivated(int query_hash);
void		add_deactivated_query(int query_hash);

/* Query preprocessing hooks */
void		get_query_text(ParseState *pstate, Query *query);
PlannedStmt *call_default_planner(Query *parse,
					 int cursorOptions,
					 ParamListInfo boundParams);
PlannedStmt *aqo_planner(Query *parse,
			int cursorOptions,
			ParamListInfo boundParams);
void print_into_explain(PlannedStmt *plannedstmt, IntoClause *into,
			   ExplainState *es, const char *queryString,
			   ParamListInfo params, const instr_time *planduration);
void		disable_aqo_for_query(void);

/* Cardinality estimation hooks */
void		aqo_set_baserel_rows_estimate(PlannerInfo *root, RelOptInfo *rel);
double aqo_get_parameterized_baserel_size(PlannerInfo *root,
								   RelOptInfo *rel,
								   List *param_clauses);
void aqo_set_joinrel_size_estimates(PlannerInfo *root, RelOptInfo *rel,
							   RelOptInfo *outer_rel,
							   RelOptInfo *inner_rel,
							   SpecialJoinInfo *sjinfo,
							   List *restrictlist);
double aqo_get_parameterized_joinrel_size(PlannerInfo *root,
								   RelOptInfo *rel,
								   Path *outer_path,
								   Path *inner_path,
								   SpecialJoinInfo *sjinfo,
								   List *restrict_clauses);
/*our plan generation approach */
RelOptInfo *aqo_join_search(PlannerInfo *root, int levels_needed,
					 List *initial_rels);
void aqo_set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, Index rti, RangeTblEntry *rte);
/*pass the parametered's explore value*/
extern void ppi_hook(ParamPathInfo *ppi);
extern void aqo_estimated_cost_hook(Path *path);
/* Extracting path information utilities */
List *get_selectivities(PlannerInfo *root,
				  List *clauses,
				  int varRelid,
				  JoinType jointype,
				  SpecialJoinInfo *sjinfo);
/* Extracting path information utilities */
List *get_selectivities2(PlannerInfo *root,
				  List *clauses,
				  int varRelid,
				  JoinType jointype,
				  SpecialJoinInfo *sjinfo);
List	   *get_list_of_relids(PlannerInfo *root, Relids relids);
List	   *get_path_clauses(Path *path, PlannerInfo *root, List **selectivities);

/* Cardinality estimation */
double predict_for_relation(List *restrict_clauses,
					 List *selectivities,
					 List *relids);
double predict_for_relation_lwpr(List *restrict_clauses, 
                List *selectivities,
                List *relids);
// 不仅输出基数值，也输出探索价值
void predict_for_relation_lwpr_explore(List *restrict_clauses, List *selectivities, List *relids, Explore_Value *ev);
//void calculate_current_best_estimate_cost(PlannerInfo *root, int query_pattern, int nfeatures, double *input_feature);
void calculate_current_best_estimate_cost(QueryContextData	*query_context2, int query_pattern, int nfeatures, double *input_feature);
void update_two_best_costs_record(int current_query_pattern, int nfeatures, double *current_query_features, Cost best_est_cost, double total_time);
/* Query execution statistics collecting hooks */
void		aqo_ExecutorStart(QueryDesc *queryDesc, int eflags);
void		aqo_copy_generic_path_info(PlannerInfo *root, Plan *dest, Path *src);
void		learn_query_stat(QueryDesc *queryDesc);

/* Machine learning techniques */
double OkNNr_predict(int matrix_rows, int matrix_cols,
			  double **matrix, double *targets,
			  double *nw_features, int aqo_k);
/*used for predicting best estimated cost for current query, modified by jim 2021.3.12 */
double OkNNr_predict2(int matrix_rows, int matrix_cols,
			  double **matrix, double *targets,
			  double *nw_features, int aqo_k);
List *OkNNr_learn(int matrix_rows, int matrix_cols,
			double **matrix, double *targets,
			double *nw_features, double nw_target, int aqo_K);
List *OkNNr_learn2(int matrix_rows, int matrix_cols,
			double **matrix, double *best_est_costs, double *best_true_costs,
			double *nw_features, double nw_est_cost, double nw_true_cost, int aqo_k);
////////////////////////////////////////lwpr begin
/* Locally weighted projection regression*/
/* 定义相关函数 */
/* Locally weighted projection regression*/
// 模型初始化
int lwpr_init_model(LWPR_Model *model, int nIn, int nOut);
// 模型更新
int lwpr_update(LWPR_Model *model, const double *x, const double y);
// 模型预测
double lwpr_predict(LWPR_Model *model, const double *x, double cutoff);
void lwpr_predict_explore(LWPR_Model *model, const double *x, double cutoff, Explore_Value *ev);
void lwpr_aux_predict_one_T(void *ptr);
void lwpr_aux_predict_conf_one_T(void *ptr);
int lwpr_aux_update_one(LWPR_Model *model, const double *xn, double yn);
void lwpr_aux_compute_projection(int nIn, int nInS, int nReg,
                                 double *s, const double *x, const double *U, const double *P, LWPR_Workspace *WS);
void lwpr_aux_update_one_T(void *ptr);
double lwpr_aux_update_means(LWPR_ReceptiveField *RF, const double *x, double y, double w, double *xmz, const LWPR_Model *model);
void lwpr_aux_update_regression(LWPR_ReceptiveField *RF, double *yp, double *e_cv_R, double *e,
                                const double *x, double y, double w, LWPR_Workspace *WS, const LWPR_Model *model);
double lwpr_aux_update_distance_metric(LWPR_ReceptiveField *RF,
                                       double w, double dwdq, double e_cv, double e, const double *xn, LWPR_Workspace *WS, const LWPR_Model *model);
int lwpr_aux_check_add_projection(LWPR_ReceptiveField *RF, const LWPR_Model *model);
void lwpr_aux_compute_projection_r(int nIn, int nInS, int nReg,
                                   double *s, double *xres, const double *x, const double *U, const double *P);
void lwpr_aux_dist_derivatives(int nIn, int nInS, double *dwdM, double *dJ2dM, double w, double dwdq, const double *RF_D, const double *RF_M, const double *dx, int diag_only, double penalty);
int lwpr_aux_update_one_add_prune(LWPR_Model *model, LWPR_ThreadData *TD, const double *xn, double yn);
LWPR_ReceptiveField *lwpr_aux_add_rf(LWPR_Model *model, int nReg);
int lwpr_aux_init_rf(LWPR_ReceptiveField *RF, const LWPR_Model *model, const LWPR_ReceptiveField *RFT, const double *xc, double y);
// math method
double lwpr_math_dot_product(const double *x, const double *y, int n);
void lwpr_math_add_scalar_vector(double *y, double a, const double *x, int n);
void lwpr_math_scalar_vector(double *y, double a, const double *x, int n);
void lwpr_math_scale_add_scalar_vector(double b, double *y, double a,const double *x,int n);
double lwpr_math_avg_vector(const double *x,int n);
//正态分布函数和求积分函数
double integral(double(*p)(double,double,double),double a,double b,int n,double a1,double b1);
double norm_ditribution_function(double a,double b,double x);
//memory method
// 定义下面内存函数
//分配内存
int lwpr_mem_alloc_model(LWPR_Model *model, int nIn, int storeRFS);
int lwpr_mem_alloc_ws(LWPR_Workspace *ws, int nIn);
int lwpr_mem_realloc_rf(LWPR_ReceptiveField *RF, int nRegStore, int nInStore);
int lwpr_mem_alloc_rf(LWPR_ReceptiveField *RF, const LWPR_Model *model, int nReg, int nRegStore);
//explore value
int lwpr_men_alloc_ev(Explore_Value *ev);
//释放内存
void lwpr_free_model(LWPR_Model *model);
void lwpr_mem_free_rf(LWPR_ReceptiveField *RF);
void lwpr_mem_free_ws(LWPR_Workspace *ws);
////////////////////////////////////////////////lwpr end
/* Automatic query tuning */
void		automatical_query_tuning(int query_hash, QueryStat * stat);

/* Utilities */
int			int_cmp(const void *a, const void *b);
int			double_cmp(const void *a, const void *b);
int *argsort(void *a, int n, size_t es,
		int (*cmp) (const void *, const void *));
int		   *inverse_permutation(int *a, int n);
QueryStat  *palloc_query_stat(void);
void		pfree_query_stat(QueryStat *stat);

/* Selectivity cache for parametrized baserels */
void cache_selectivity(int clause_hash,
				  int relid,
				  int global_relid,
				  double selectivity);
double	   *selectivity_cache_find_global_relid(int clause_hash, int global_relid);
void		selectivity_cache_clear(void);

/*storage function, moved by jim 2021.2.23*/
ArrayType *form_matrix(double **matrix, int nrows, int ncols);
void deform_matrix(Datum datum, double **matrix);

// static ArrayType *form_cubic(double ***cubic, int nrows, int ncols, int num_rf);
// static void deform_cubic(Datum datum, double ***cubic);

ArrayType *form_vector(double *vector, int nrows);
//static ArrayType *strlist_to_textarray_two(List *list);
void deform_vector(Datum datum, double *vector, int *nelems);

#endif
