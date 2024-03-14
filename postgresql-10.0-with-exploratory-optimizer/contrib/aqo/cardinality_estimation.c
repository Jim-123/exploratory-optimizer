#include "aqo.h"

/*****************************************************************************
 *
 *	CARDINALITY ESTIMATION
 *
 * This is the module in which cardinality estimation problem obtained from
 * cardinality_hooks turns into machine learning problem.
 *
 *****************************************************************************/

/*
 * General method for prediction the cardinality of given relation using online knn
 */
double
predict_for_relation(List *restrict_clauses, List *selectivities, List *relids)
{
	int			nfeatures;
	int			fss_hash;
	double	  **matrix;
	double	   *target;
	double	   *features;
	double		result;
	int			rows;
	int			i;

	get_fss_for_object(restrict_clauses, selectivities, relids,
					   &nfeatures, &fss_hash, &features);

	matrix = palloc(sizeof(*matrix) * aqo_K);
	for (i = 0; i < aqo_K; ++i)
		matrix[i] = palloc0(sizeof(**matrix) * nfeatures);
	target = palloc0(sizeof(*target) * aqo_K);

	if (load_fss(fss_hash, nfeatures, matrix, target, &rows))
		result = OkNNr_predict(rows, nfeatures, matrix, target, features,2);
	else
		result = -1;

	pfree(features);
	for (i = 0; i < aqo_K; ++i)
		pfree(matrix[i]);
	pfree(matrix);
	pfree(target);
	list_free_deep(selectivities);
	list_free(restrict_clauses);
	list_free(relids);

	if (result < 0)
		return -1;
	else
		return exp(result);
}

/*
 * General method for prediction the cardinality of given relation using LWPR
 */
double
predict_for_relation_lwpr(List *restrict_clauses, List *selectivities, List *relids)
{
    //获取当前子查询的特征和hash值
	int			nfeatures;
	int			fss_hash;
	double	   *features;
	double	    result;
    double      cutoff = 0.001;

	//获取当前的LWPR模型
	LWPR_Model  model;
	//获取相关参数
	get_fss_for_object(restrict_clauses, selectivities, relids,
					   &nfeatures, &fss_hash, &features);
	//初始化并分配空间给model
    lwpr_init_model(&model, nfeatures, 1);
    //加载model
	if (load_fss_rfwr(fss_hash, nfeatures, &model))
		result = lwpr_predict(&model, features, cutoff);
	else
		result = -1;

	pfree(features);
	//free model
    lwpr_free_model(&model);
	list_free_deep(selectivities);
	list_free(restrict_clauses);
	list_free(relids);

	if (result < 0)
		return -1;
	else
		return exp(result);
}
/**
 * General method for prediction the cardinality and PEV of given relation using LWPR
 */
void predict_for_relation_lwpr_explore(List *restrict_clauses, List *selectivities, List *relids,  Explore_Value *result)
{
    //获取当前子查询的特征和hash值
	int			nfeatures;
	int			fss_hash;
	double	   *features;
    double      cutoff = 0.001;
	//获取当前的LWPR模型
	LWPR_Model  model;
	//获取相关参数
	get_fss_for_object(restrict_clauses, selectivities, relids,
					   &nfeatures, &fss_hash, &features);
	//初始化并分配空间给model
    lwpr_init_model(&model, nfeatures, 1);
    //加载model
	if (load_fss_rfwr(fss_hash, nfeatures, &model))
	{
		lwpr_predict_explore(&model, features, cutoff, result);
		if (result->rows == -9999){
			//当为-9999时，则说明使用原基数估计方法
			result->rows = -1;
		}else
		{
			/* code */
			result->rows= exp(result->rows);
		}
		//更新模型
		update_fss_rfwr2(fss_hash, nfeatures, &model);
	}
	else
	{
		result->rows = -1;
	}

	pfree(features);
	//free model
    lwpr_free_model(&model);
	list_free_deep(selectivities);
	list_free(restrict_clauses);
	list_free(relids);
}