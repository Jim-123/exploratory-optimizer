#include "aqo.h"

/*****************************************************************************
 *
 *	MACHINE LEARNING TECHNIQUES
 *
 * This module does not know anything about DBMS, cardinalities and all other
 * stuff. It learns matrices, predicts values and is quite happy.
 * The proposed method is designed for working with limited number of objects.
 * It is guaranteed that number of rows in the matrix will not exceed aqo_K
 * setting after learning procedure. This property also allows to adapt to
 * workloads which properties are slowly changed.
 *
 *****************************************************************************/

static double fs_distance(double *a, double *b, int len);
static double fs_similarity(double dist);
static void OkNNr_compute_weights(double *distances, int matrix_rows,
					  double **w, double *w_sum, int **idx, int aqo_k);


/*
 * Computes L2-distance between two given vectors.
 */
double
fs_distance(double *a, double *b, int len)
{
	double		res = 0;
	int			i;

	for (i = 0; i < len; ++i)
		res += (a[i] - b[i]) * (a[i] - b[i]);
	if (len != 0)
		res = sqrt(res / len);
	return res;
}

/*
 * Returns similarity between objects based on distance between them.
 */
double
fs_similarity(double dist)
{
	return 1.0 / (0.001 + dist);
}

/*
 * Compute weights necessary for both prediction and learning.
 * Creates and returns w, w_sum and idx based on given distances ad matrix_rows.
 *
 * Appeared as a separate function because of "don't repeat your code"
 * principle.
 */
void
OkNNr_compute_weights(double *distances, int matrix_rows,
					  double **w, double *w_sum, int **idx, int aqo_k)
{
	int			i,
				j;
	int			to_insert,
				tmp;

	*w_sum = 0;

	*idx = palloc0(sizeof(**idx) * aqo_k);
	for (i = 0; i < aqo_k; ++i)
		(*idx)[i] = -1;
	(*w) = palloc0(sizeof(**w) * aqo_k);

	for (i = 0; i < matrix_rows; ++i)
		for (j = 0; j < aqo_k; ++j)
			if ((*idx)[j] == -1 || distances[i] < distances[(*idx)[j]])
			{
				to_insert = i;
				for (; j < aqo_k; ++j)
				{
					tmp = (*idx)[j];
					(*idx)[j] = to_insert;
					to_insert = tmp;
				}
				break;
			}
	for (i = 0; i < aqo_k && (*idx)[i] != -1; ++i)
	{
		(*w)[i] = fs_similarity(distances[(*idx)[i]]);
		*w_sum += (*w)[i];
	}
}

/*
 * With given matrix, targets and features makes prediction for current object.
 *
 * Returns negative value in the case of refusal to make a prediction, because
 * positive targets are assumed.
 */
double
OkNNr_predict(int matrix_rows, int matrix_cols,
			  double **matrix, double *targets,
			  double *nw_features, int aqo_k)
{
	double	   *distances;
	int			i;
	int		   *idx;
	double	   *w;
	double		w_sum;
	double		result = 0;

	distances = palloc0(sizeof(*distances) * matrix_rows);

	for (i = 0; i < matrix_rows; ++i)
		distances[i] = fs_distance(matrix[i], nw_features, matrix_cols);

	OkNNr_compute_weights(distances, matrix_rows, &w, &w_sum, &idx, aqo_k);

	for (i = 0; i < aqo_k; ++i)
		if (idx[i] != -1)
			result += targets[idx[i]] * w[i] / w_sum;

	if (result < 0)
		result = 0;

	/* this should never happen */
	if (idx[0] == -1)
		result = -1;

	pfree(w);
	pfree(distances);
	pfree(idx);

	return result;
}

double
OkNNr_predict2(int matrix_rows, int matrix_cols,
			  double **matrix, double *targets,
			  double *nw_features, int aqo_k)
{
	double	   *distances;
	int			i;
	int		   *idx;
	double	   *w;
	double		w_sum;
	double		result = 0;
    double      threshold_distance = 0.1;
	double      min_distance = 9999;
	if(matrix_rows == 0){
		return 0;
	}
	distances = palloc0(sizeof(*distances) * matrix_rows);
    
	for (i = 0; i < matrix_rows; ++i){
		distances[i] = fs_distance(matrix[i], nw_features, matrix_cols);
		if(distances[i] < min_distance){
			min_distance = distances[i];
		}
	}
	//判断当min_distance小于threshold_distance时，默认为0
	if(min_distance > threshold_distance){
		return 0;
	}
	OkNNr_compute_weights(distances, matrix_rows, &w, &w_sum, &idx, aqo_k);

	for (i = 0; i < aqo_k; ++i)
		if (idx[i] != -1)
			result += targets[idx[i]] * w[i] / w_sum;

	if (result < 0)
		result = 0;

	/* this should never happen */
	if (idx[0] == -1)
		result = -1;

	pfree(w);
	pfree(distances);
	pfree(idx);

	return result;
}
/*
 * Modifies given matrix and targets using features and target value of new
 * object.
 * Returns indexes of changed lines: if index of line is less than matrix_rows
 * updates this line in database, otherwise adds new line with given index.
 * It is supposed that indexes of new lines are consequent numbers
 * starting from matrix_rows.
 */
List *
OkNNr_learn(int matrix_rows, int matrix_cols,
			double **matrix, double *targets,
			double *nw_features, double nw_target, int aqo_k)
{
	List	   *lst = NIL;
	double	   *distances;
	int			i,
				j;
	int			min_distance_id = 0;
	int		   *idx;
	double	   *w;
	double		w_sum;
	double	   *cur_row;
	double		coef1,
				coef2;
	double		result = 0;

	distances = palloc0(sizeof(*distances) * matrix_rows);

	for (i = 0; i < matrix_rows; ++i)
	{
		distances[i] = fs_distance(matrix[i], nw_features, matrix_cols);
		if (distances[i] < distances[min_distance_id])
			min_distance_id = i;
	}
	if (matrix_rows < aqo_k)
	{
		if (matrix_rows != 0 && distances[min_distance_id] <
			object_selection_object_threshold)
		{
			for (j = 0; j < matrix_cols; ++j)
				matrix[min_distance_id][j] += learning_rate *
					(nw_features[j] - matrix[min_distance_id][j]);
			targets[min_distance_id] += learning_rate *
				(nw_target - targets[min_distance_id]);
			lst = lappend_int(lst, min_distance_id);
		}
		else
		{
			for (j = 0; j < matrix_cols; ++j)
				matrix[matrix_rows][j] = nw_features[j];
			targets[matrix_rows] = nw_target;
			lst = lappend_int(lst, matrix_rows);
		}
	}
	else
	{
		OkNNr_compute_weights(distances, matrix_rows, &w, &w_sum, &idx, aqo_k);

		for (i = 0; i < aqo_k && idx[i] != -1; ++i)
			result += targets[idx[i]] * w[i] / w_sum;
		coef1 = learning_rate * (result - nw_target);

		for (i = 0; i < aqo_k && idx[i] != -1; ++i)
		{
			coef2 = coef1 * (targets[idx[i]] - result) * w[i] * w[i] /
				sqrt(matrix_cols) / w_sum;

			targets[idx[i]] -= coef1 * w[i] / w_sum;
			for (j = 0; j < matrix_cols; ++j)
			{
				cur_row = matrix[idx[i]];
				cur_row[j] -= coef2 * (nw_features[j] - cur_row[j]) /
					distances[idx[i]];
			}

			lst = lappend_int(lst, idx[i]);
		}

		pfree(w);
		pfree(idx);
	}

	pfree(distances);
	return lst;
}


List *
OkNNr_learn2(int matrix_rows, int matrix_cols,
			double **matrix, double *best_est_costs, double *best_true_costs,
			double *nw_features, double nw_est_cost, double nw_true_cost, int aqo_k)
{
	List	   *lst = NIL;
	double	   *distances;
	int			i,
				j;
	int			min_distance_id = 0;
	int		   *idx;
	double	   *w;
	double		w_sum;
	double	   *cur_row;
	double		coef1,
				coef2;
	double		result = 0;
	double      object_selection_object_threshold2 = 0.1;
	double      learning_rate1 = 0.1;  // used for updating the query feature
	double      learning_rate2 = 1;   //used for updating the estimated cost and true cost.
	double      compare_rate = 0.01;  //cost*compare_rate < history_cost
    double      hist_cost = 0;
	distances = palloc0(sizeof(*distances) * matrix_rows);
    // find the nearest point
	for (i = 0; i < matrix_rows; ++i)
	{
		distances[i] = fs_distance(matrix[i], nw_features, matrix_cols);
		if (distances[i] < distances[min_distance_id])
			min_distance_id = i;
	}
	if (matrix_rows < aqo_k)
	{
		if (matrix_rows != 0 && distances[min_distance_id] <
			object_selection_object_threshold2)
		{
			//judge if current nw_true_cost is less than the history point, if exist, update it, else do nothing. 2021.3.13
			//modified by jim 2021.3.15
			/*we need to consider more complex situation, nearly equal, larger, less than*/
			hist_cost = best_true_costs[min_distance_id];
			if ((nw_true_cost == hist_cost)||(nw_true_cost > hist_cost && nw_true_cost < hist_cost*(1+compare_rate))
			    ||(nw_true_cost < hist_cost && nw_true_cost*(1+compare_rate) > hist_cost))
			{
				//nearly equal, we should learn the query features and estimated cost.
				for (j = 0; j < matrix_cols; ++j)
					matrix[min_distance_id][j] += learning_rate1 *
						(nw_features[j] - matrix[min_distance_id][j]);
				//update estimated cost
				best_est_costs[min_distance_id] += learning_rate2 *
					(nw_est_cost - best_est_costs[min_distance_id]);
			
				lst = lappend_int(lst, min_distance_id);

			}else if (nw_true_cost*(1+compare_rate) < hist_cost){
				//less than, we need learn query features, estimated cost and true cost
				for (j = 0; j < matrix_cols; ++j)
					matrix[min_distance_id][j] += learning_rate1 *
						(nw_features[j] - matrix[min_distance_id][j]);
				//update estimated cost
				best_est_costs[min_distance_id] += learning_rate2 *
					(nw_est_cost - best_est_costs[min_distance_id]);
				//update true cost
				best_true_costs[min_distance_id] += learning_rate2 *
					(nw_true_cost - hist_cost);
			
				lst = lappend_int(lst, min_distance_id);
			}else if (hist_cost*(1+compare_rate) < nw_true_cost){
				//larger situation, if est cost is larger than best history cost, ignore it, otherwise, we should decrease the best history cost to est_cost
				//the relationity is because 1. longer runtime, longer est_cost; 2.cardinality is more accurate as time go 
				/* code */
				if(nw_est_cost < best_est_costs[min_distance_id]){
					for (j = 0; j < matrix_cols; ++j)
					matrix[min_distance_id][j] += learning_rate1 *
						(nw_features[j] - matrix[min_distance_id][j]);
					//update estimated cost
					best_est_costs[min_distance_id] += learning_rate2 *
						(nw_est_cost - best_est_costs[min_distance_id]);
				}
				lst = lappend_int(lst, min_distance_id);
			}
			
		}
		else
		{
			for (j = 0; j < matrix_cols; ++j)
				matrix[matrix_rows][j] = nw_features[j];
			best_est_costs[matrix_rows] = nw_est_cost;
			best_true_costs[matrix_rows] = nw_true_cost;
			lst = lappend_int(lst, matrix_rows);
		}
	}
	else
	{
        hist_cost = best_true_costs[min_distance_id];
		if ((nw_true_cost == hist_cost)||(nw_true_cost > hist_cost && nw_true_cost < hist_cost*(1+compare_rate))
			||(nw_true_cost < hist_cost && nw_true_cost*(1+compare_rate) > hist_cost))
		{
			//nearly equal, we should learn the query features and estimated cost.
			for (j = 0; j < matrix_cols; ++j)
				matrix[min_distance_id][j] += learning_rate1 *
					(nw_features[j] - matrix[min_distance_id][j]);
			//update estimated cost
			best_est_costs[min_distance_id] += learning_rate2 *
				(nw_est_cost - best_est_costs[min_distance_id]);
		
			lst = lappend_int(lst, min_distance_id);

		}else if (nw_true_cost*(1+compare_rate) < hist_cost){
			//less than, we need learn query features, estimated cost and true cost
			for (j = 0; j < matrix_cols; ++j)
				matrix[min_distance_id][j] += learning_rate1 *
					(nw_features[j] - matrix[min_distance_id][j]);
			//update estimated cost
			best_est_costs[min_distance_id] += learning_rate2 *
				(nw_est_cost - best_est_costs[min_distance_id]);
			//update true cost
			best_true_costs[min_distance_id] += learning_rate2 *
				(nw_true_cost - hist_cost);
		
			lst = lappend_int(lst, min_distance_id);
		}else if (hist_cost*(1+compare_rate) < nw_true_cost){
			//larger situation, if est cost is larger than best history cost, ignore it, otherwise, we should decrease the best history cost to est_cost
			//the relationity is because 1. longer runtime, longer est_cost; 2.cardinality is more accurate as time go 
			/* code */
			if(nw_est_cost < best_est_costs[min_distance_id]){
				for (j = 0; j < matrix_cols; ++j)
				matrix[min_distance_id][j] += learning_rate1 *
					(nw_features[j] - matrix[min_distance_id][j]);
				//update estimated cost
				best_est_costs[min_distance_id] += learning_rate2 *
					(nw_est_cost - best_est_costs[min_distance_id]);
			}
			lst = lappend_int(lst, min_distance_id);
		}
	}

	pfree(distances);
	return lst;
}