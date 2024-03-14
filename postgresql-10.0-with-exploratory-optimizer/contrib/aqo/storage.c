#include "aqo.h"

/*****************************************************************************
 *
 *	STORAGE INTERACTION
 *
 * This module is responsible for interaction with the storage of AQO data.
 * It does not provide information protection from concurrent updates.
 *
 *****************************************************************************/

HTAB *deactivated_queries = NULL;


#define FormVectorSz(v_name)			(form_vector((v_name), (v_name ## _size)))
#define DeformVectorSz(datum, v_name)	(deform_vector((datum), (v_name), &(v_name ## _size)))


static bool my_simple_heap_update(Relation relation,
								  ItemPointer otid,
								  HeapTuple tup);

static bool my_index_insert(Relation indexRelation,
							Datum *values,
							bool *isnull,
							ItemPointer heap_t_ctid,
							Relation heapRelation,
							IndexUniqueCheck checkUnique);


/*
 * Returns whether the query with given hash is in aqo_queries.
 * If yes, returns the content of the first line with given hash.
 */
bool
find_query(int query_hash,
		   Datum *search_values,
		   bool *search_nulls)
{
	RangeVar   *aqo_queries_table_rv;
	Relation	aqo_queries_heap;
	HeapTuple	tuple;

	LOCKMODE	lockmode = AccessShareLock;

	Relation	query_index_rel;
	Oid			query_index_rel_oid;
	IndexScanDesc query_index_scan;
	ScanKeyData key;

	bool		find_ok = false;

	query_index_rel_oid = RelnameGetRelid("aqo_queries_query_hash_idx");
	if (!OidIsValid(query_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_queries_table_rv = makeRangeVar("public", "aqo_queries", -1);
	aqo_queries_heap = heap_openrv(aqo_queries_table_rv, lockmode);

	query_index_rel = index_open(query_index_rel_oid, lockmode);
	query_index_scan = index_beginscan(aqo_queries_heap,
									   query_index_rel,
									   SnapshotSelf,
									   1,
									   0);

	ScanKeyInit(&key,
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_hash));

	index_rescan(query_index_scan, &key, 1, NULL, 0);
	tuple = index_getnext(query_index_scan, ForwardScanDirection);

	find_ok = (tuple != NULL);

	if (find_ok)
		heap_deform_tuple(tuple, aqo_queries_heap->rd_att,
						  search_values, search_nulls);

	index_endscan(query_index_scan);
	index_close(query_index_rel, lockmode);
	heap_close(aqo_queries_heap, lockmode);

	return find_ok;
}

/*
 * Creates entry for new query in aqo_queries table with given fields.
 * Returns false if the operation failed, true otherwise.
 */
bool
add_query(int query_hash, bool learn_aqo, bool use_aqo,
		  int fspace_hash, bool auto_tuning, double *query_history, int num_history, int total_num, double *current_query, int num_feature)
{
	RangeVar   *aqo_queries_table_rv;
	Relation	aqo_queries_heap;
	HeapTuple	tuple;

	LOCKMODE	lockmode = RowExclusiveLock;

	Datum		values[10];
	bool		nulls[10] = {false, false, false, false, false, false, false, false, false, false};

	Relation	query_index_rel;
	Oid			query_index_rel_oid;

	values[0] = Int32GetDatum(query_hash);
	values[1] = BoolGetDatum(learn_aqo);
	values[2] = BoolGetDatum(use_aqo);
	values[3] = Int32GetDatum(fspace_hash);
	values[4] = BoolGetDatum(auto_tuning);
	values[5] = PointerGetDatum(form_vector(query_history, num_history_data_compute_probability_fs));
    values[6] = Int32GetDatum(num_history);
	values[7] = Int32GetDatum(total_num);
	values[8] = PointerGetDatum(form_vector(current_query, 2));
    values[9] = Int32GetDatum(num_feature);
	query_index_rel_oid = RelnameGetRelid("aqo_queries_query_hash_idx");
	if (!OidIsValid(query_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}
	query_index_rel = index_open(query_index_rel_oid, lockmode);

	aqo_queries_table_rv = makeRangeVar("public", "aqo_queries", -1);
	aqo_queries_heap = heap_openrv(aqo_queries_table_rv, lockmode);

	tuple = heap_form_tuple(RelationGetDescr(aqo_queries_heap),
							values, nulls);
	PG_TRY();
	{
		simple_heap_insert(aqo_queries_heap, tuple);
		my_index_insert(query_index_rel,
						values, nulls,
						&(tuple->t_self),
						aqo_queries_heap,
						UNIQUE_CHECK_YES);
	}
	PG_CATCH();
	{
		/*
		 * Main goal is to catch deadlock errors during the index insertion.
		 */
		CommandCounterIncrement();
		simple_heap_delete(aqo_queries_heap, &(tuple->t_self));
		PG_RE_THROW();
	}
	PG_END_TRY();

	index_close(query_index_rel, lockmode);
	heap_close(aqo_queries_heap, lockmode);

	CommandCounterIncrement();

	return true;
}

/*
 * add collected data to table 
 */
// bool
// add_collect_data(int fss_hash, int nfeature,
// 		 double *features, double targets, double predicts)
// {
// 	RangeVar   *aqo_history_data_table_rv;
// 	Relation	aqo_queries_heap;
// 	HeapTuple	tuple;

// 	LOCKMODE	lockmode = RowExclusiveLock;

// 	Datum		values[7];
// 	bool		nulls[7] = {true, false, false, false, false, false, false};

// 	Relation	query_index_rel;
// 	Oid			query_index_rel_oid;

// 	double     *target;
// 	double     *predict;

	

// 	target = palloc0(sizeof(*target) * 1);
// 	predict = palloc0(sizeof(*predict) * 1);
// 	target[0] = targets;
// 	predict[0] = predicts;





// 	values[1] = Int32GetDatum(query_context.fspace_hash);
// 	values[2] = Int32GetDatum(fss_hash);
// 	values[3] = Int32GetDatum(nfeature);
// 	values[4] = PointerGetDatum(form_vector(features, nfeature));
// 	values[5] = PointerGetDatum(form_vector(target, 1));
// 	values[6] = PointerGetDatum(form_vector(predict, 1));

// 	query_index_rel_oid = RelnameGetRelid("aqo_fss_history_data_idx");
// 	if (!OidIsValid(query_index_rel_oid))
// 	{
// 		disable_aqo_for_query();
// 		return false;
// 	}
// 	query_index_rel = index_open(query_index_rel_oid, lockmode);

// 	aqo_history_data_table_rv = makeRangeVar("public", "aqo_history_data", -1);
// 	aqo_queries_heap = heap_openrv(aqo_history_data_table_rv, lockmode);

// 	tuple = heap_form_tuple(RelationGetDescr(aqo_queries_heap),
// 							values, nulls);
// 	PG_TRY();
// 	{
// 		simple_heap_insert(aqo_queries_heap, tuple);
// 		my_index_insert(query_index_rel,
// 						values, nulls,
// 						&(tuple->t_self),
// 						aqo_queries_heap,
// 						UNIQUE_CHECK_YES);
// 	}
// 	PG_CATCH();
// 	{
// 		/*
// 		 * Main goal is to catch deadlock errors during the index insertion.
// 		 */
// 		CommandCounterIncrement();
// 		simple_heap_delete(aqo_queries_heap, &(tuple->t_self));
// 		PG_RE_THROW();
// 	}
// 	PG_END_TRY();

// 	index_close(query_index_rel, lockmode);
// 	heap_close(aqo_queries_heap, lockmode);

// 	CommandCounterIncrement();

// 	return true;
// }

// /*
//  * add collected data to table (collect ffs_hash, causelist, selectivities, target_value)
//  */
// bool
// add_collect_data2(int fss_hash, List *relidslist, List *clauselist, List *selectivities, double targets)
// {
// 	RangeVar   *aqo_history_data_table_rv;
// 	Relation	aqo_queries_heap;
// 	HeapTuple	tuple;

// 	LOCKMODE	lockmode = RowExclusiveLock;
// 	Datum		values[7];
// 	bool		nulls[7] = {true, false, false, true, false, false, false};
	
// 	Relation	query_index_rel;
// 	Oid			query_index_rel_oid;
// 	//define some relate variables
// 	double     *target;
// 	int         selec_num;
// 	int         i;
// 	ListCell   *l;
// 	double	   *select;
// 	//List	   *test_relidslist = NIL;
// 	List	   *test_clauselist = NIL;
// 	char       *testchar;

// 	selec_num = list_length(selectivities);

//     //give memory
// 	target = palloc0(sizeof(*target) * 1);
// 	target[0] = targets;

// 	select = palloc0(sizeof(*select) * selec_num);
// 	testchar = palloc0(sizeof(*testchar) * 100);
// 	//testchar = "is ok";

// 	//将list转化为double
// 	i = 0;
// 	foreach(l, selectivities)
// 	{
// 		select[i] = log(*((double *) (lfirst(l))));

// 		i++;
// 	}
// 	//将list转化为字符串
// 	i = 0;
// 	foreach(l, clauselist)
// 	{
// 		testchar = nodeToString((Node *)(((RestrictInfo *) lfirst(l))->clause));
// 		test_clauselist = lappend(test_clauselist, testchar);
// 		i++;
// 	}
//     values[1] = Int32GetDatum(query_context.fspace_hash);
// 	values[2] = Int32GetDatum(fss_hash);
// 	//values[1] = PointerGetDatum(strlist_to_textarray_two(relidslist));
// 	values[4] = PointerGetDatum(strlist_to_textarray_two(test_clauselist));
// 	values[5] = PointerGetDatum(form_vector(select, selec_num));
// 	values[6] = PointerGetDatum(form_vector(target, 1));

// 	query_index_rel_oid = RelnameGetRelid("aqo_fss_history_data_two_idx");
// 	if (!OidIsValid(query_index_rel_oid))
// 	{
// 		disable_aqo_for_query();
// 		return false;
// 	}
// 	query_index_rel = index_open(query_index_rel_oid, lockmode);

// 	aqo_history_data_table_rv = makeRangeVar("public", "aqo_history_data_two", -1);
// 	aqo_queries_heap = heap_openrv(aqo_history_data_table_rv, lockmode);

// 	tuple = heap_form_tuple(RelationGetDescr(aqo_queries_heap),
// 							values, nulls);
// 	PG_TRY();
// 	{
// 		simple_heap_insert(aqo_queries_heap, tuple);
// 		my_index_insert(query_index_rel,
// 						values, nulls,
// 						&(tuple->t_self),
// 						aqo_queries_heap,
// 						UNIQUE_CHECK_YES);
// 	}
// 	PG_CATCH();
// 	{
// 		/*
// 		 * Main goal is to catch deadlock errors during the index insertion.
// 		 */
// 		CommandCounterIncrement();
// 		simple_heap_delete(aqo_queries_heap, &(tuple->t_self));
// 		PG_RE_THROW();
// 	}
// 	PG_END_TRY();

// 	index_close(query_index_rel, lockmode);
// 	heap_close(aqo_queries_heap, lockmode);

// 	CommandCounterIncrement();
// 	return true;
// }

bool
update_query(int query_hash, bool learn_aqo, bool use_aqo,
			 int fspace_hash, bool auto_tuning)
{
	RangeVar   *aqo_queries_table_rv;
	Relation	aqo_queries_heap;
	HeapTuple	tuple,
				nw_tuple;

	LOCKMODE	lockmode = RowExclusiveLock;

	Relation	query_index_rel;
	Oid			query_index_rel_oid;
	IndexScanDesc query_index_scan;
	ScanKeyData key;

	Datum		values[5];
	bool		isnull[5] = { false, false, false, false, false };
	bool		replace[5] = { false, true, true, true, true };

	query_index_rel_oid = RelnameGetRelid("aqo_queries_query_hash_idx");
	if (!OidIsValid(query_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_queries_table_rv = makeRangeVar("public", "aqo_queries", -1);
	aqo_queries_heap = heap_openrv(aqo_queries_table_rv, lockmode);

	query_index_rel = index_open(query_index_rel_oid, lockmode);
	query_index_scan = index_beginscan(aqo_queries_heap,
									   query_index_rel,
									   SnapshotSelf,
									   1,
									   0);

	ScanKeyInit(&key,
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_hash));

	index_rescan(query_index_scan, &key, 1, NULL, 0);
	tuple = index_getnext(query_index_scan, ForwardScanDirection);

	heap_deform_tuple(tuple, aqo_queries_heap->rd_att,
					  values, isnull);

	values[1] = BoolGetDatum(learn_aqo);
	values[2] = BoolGetDatum(use_aqo);
	values[3] = Int32GetDatum(fspace_hash);
	values[4] = BoolGetDatum(auto_tuning);

	nw_tuple = heap_modify_tuple(tuple, aqo_queries_heap->rd_att,
								 values, isnull, replace);
	if (my_simple_heap_update(aqo_queries_heap, &(nw_tuple->t_self), nw_tuple))
	{
		my_index_insert(query_index_rel, values, isnull, &(nw_tuple->t_self),
						aqo_queries_heap, UNIQUE_CHECK_YES);
	}
	else
	{
		/*
		 * Ooops, somebody concurrently updated the tuple. We have to merge
		 * our changes somehow, but now we just discard ours. We don't believe
		 * in high probability of simultaneously finishing of two long,
		 * complex, and important queries, so we don't loss important data.
		 */
	}

	index_endscan(query_index_scan);
	index_close(query_index_rel, lockmode);
	heap_close(aqo_queries_heap, lockmode);

	CommandCounterIncrement();

	return true;
}

bool
update_query2(int query_hash, bool learn_aqo, bool use_aqo,
			 int fspace_hash, bool auto_tuning, double *query_history, int num_history, int total_num)
{
	RangeVar   *aqo_queries_table_rv;
	Relation	aqo_queries_heap;
	HeapTuple	tuple,
				nw_tuple;

	LOCKMODE	lockmode = RowExclusiveLock;

	Relation	query_index_rel;
	Oid			query_index_rel_oid;
	IndexScanDesc query_index_scan;
	ScanKeyData key;

	Datum		values[10];
	bool		isnull[10] = { false, false, false, false, false,false,false,false,false,false};
	bool		replace[10] = { false, true, true, true, true, true, true, true, false, false};

	query_index_rel_oid = RelnameGetRelid("aqo_queries_query_hash_idx");
	if (!OidIsValid(query_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_queries_table_rv = makeRangeVar("public", "aqo_queries", -1);
	aqo_queries_heap = heap_openrv(aqo_queries_table_rv, lockmode);

	query_index_rel = index_open(query_index_rel_oid, lockmode);
	query_index_scan = index_beginscan(aqo_queries_heap,
									   query_index_rel,
									   SnapshotSelf,
									   1,
									   0);

	ScanKeyInit(&key,
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_hash));

	index_rescan(query_index_scan, &key, 1, NULL, 0);
	tuple = index_getnext(query_index_scan, ForwardScanDirection);

	heap_deform_tuple(tuple, aqo_queries_heap->rd_att,
					  values, isnull);

	values[1] = BoolGetDatum(learn_aqo);
	values[2] = BoolGetDatum(use_aqo);
	values[3] = Int32GetDatum(fspace_hash);
	values[4] = BoolGetDatum(auto_tuning);
	values[5] = PointerGetDatum(form_vector(query_history, num_history_data_compute_probability_fs));
    values[6] = Int32GetDatum(num_history);
	values[7] = Int32GetDatum(total_num);

	nw_tuple = heap_modify_tuple(tuple, aqo_queries_heap->rd_att,
								 values, isnull, replace);
	if (my_simple_heap_update(aqo_queries_heap, &(nw_tuple->t_self), nw_tuple))
	{
		my_index_insert(query_index_rel, values, isnull, &(nw_tuple->t_self),
						aqo_queries_heap, UNIQUE_CHECK_YES);
	}
	else
	{
		/*
		 * Ooops, somebody concurrently updated the tuple. We have to merge
		 * our changes somehow, but now we just discard ours. We don't believe
		 * in high probability of simultaneously finishing of two long,
		 * complex, and important queries, so we don't loss important data.
		 */
	}

	index_endscan(query_index_scan);
	index_close(query_index_rel, lockmode);
	heap_close(aqo_queries_heap, lockmode);

	CommandCounterIncrement();

	return true;
}
/*
 * Creates entry for new query in aqo_query_texts table with given fields.
 * Returns false if the operation failed, true otherwise.
 */
bool
add_query_text(int query_hash, const char *query_text)
{
	RangeVar   *aqo_query_texts_table_rv;
	Relation	aqo_query_texts_heap;
	HeapTuple	tuple;

	LOCKMODE	lockmode = RowExclusiveLock;

	Datum		values[2];
	bool		isnull[2] = {false, false};

	Relation	query_index_rel;
	Oid			query_index_rel_oid;

	values[0] = Int32GetDatum(query_hash);
	values[1] = CStringGetTextDatum(query_text);

	query_index_rel_oid = RelnameGetRelid("aqo_query_texts_query_hash_idx");
	if (!OidIsValid(query_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}
	query_index_rel = index_open(query_index_rel_oid, lockmode);

	aqo_query_texts_table_rv = makeRangeVar("public",
											"aqo_query_texts",
											-1);
	aqo_query_texts_heap = heap_openrv(aqo_query_texts_table_rv,
									   lockmode);

	tuple = heap_form_tuple(RelationGetDescr(aqo_query_texts_heap),
							values, isnull);

	PG_TRY();
	{
		simple_heap_insert(aqo_query_texts_heap, tuple);
		my_index_insert(query_index_rel,
						values, isnull,
						&(tuple->t_self),
						aqo_query_texts_heap,
						UNIQUE_CHECK_YES);
	}
	PG_CATCH();
	{
		CommandCounterIncrement();
		simple_heap_delete(aqo_query_texts_heap, &(tuple->t_self));
		PG_RE_THROW();
	}
	PG_END_TRY();

	index_close(query_index_rel, lockmode);
	heap_close(aqo_query_texts_heap, lockmode);

	CommandCounterIncrement();

	return true;
}

/*
 * Loads feature subspace (fss) from table aqo_data into memory.
 * The last column of the returned matrix is for target values of objects.
 * Returns false if the operation failed, true otherwise.
 *
 * 'fss_hash' is the hash of feature subspace which is supposed to be loaded
 * 'ncols' is the number of clauses in the feature subspace
 * 'matrix' is an allocated memory for matrix with the size of aqo_K rows
 *			and nhashes columns
 * 'targets' is an allocated memory with size aqo_K for target values
 *			of the objects
 * 'rows' is the pointer in which the function stores actual number of
 *			objects in the given feature space
 */
bool
load_fss(int fss_hash, int ncols,
		 double **matrix, double *targets, int *rows)
{
	RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	HeapTuple	tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[2];

	LOCKMODE	lockmode = AccessShareLock;

	Datum		values[5];
	bool		isnull[5];

	bool		success = true;

	data_index_rel_oid = RelnameGetRelid("aqo_fss_access_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_data", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  2,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_context.fspace_hash));

	ScanKeyInit(&key[1],
				2,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(fss_hash));

	index_rescan(data_index_scan, key, 2, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (tuple)
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);

		if (DatumGetInt32(values[2]) == ncols)
		{
			deform_matrix(values[3], matrix);
			deform_vector(values[4], targets, rows);
		}
		else
		{
			elog(WARNING, "unexpected number of features for hash (%d, %d):\
						   expected %d features, obtained %d",
						   query_context.fspace_hash,
						   fss_hash, ncols, DatumGetInt32(values[2]));
			success = false;
		}
	}
	else
		success = false;

	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	return success;
}

/* load Rf's datahouse */
bool
load_rf_datahouse(int fss_hash, int rf_hash, double **matrix, double *targets, int *rows)
{
	RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	HeapTuple	tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[3];

	LOCKMODE	lockmode = AccessShareLock;

	Datum		values[5];
	bool		isnull[5];

	bool		success = true;

	data_index_rel_oid = RelnameGetRelid("aqo_fss_lwpr_datahouse_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_data_house_lwpr", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  3,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_context.fspace_hash));

	ScanKeyInit(&key[1],
				2,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(fss_hash));
	ScanKeyInit(&key[2],
				3,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(rf_hash));

	index_rescan(data_index_scan, key, 3, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (tuple)
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);
		deform_matrix(values[3], matrix);
		deform_vector(values[4], targets, rows);	
	}else{
		success = false;
	}

	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	return success;
}

/* load Rf's datahouse */
bool
load_best_two_costs(int query_pattern, double **matrix, double *est_cost, double *true_cost, int *rows, int nfeatures)
{

	RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	HeapTuple	tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[1];

	LOCKMODE	lockmode = AccessShareLock;

	Datum		values[5];
	bool		isnull[5];

	bool		success = true;

	data_index_rel_oid = RelnameGetRelid("aqo_best_two_costs_table_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_best_two_costs_table", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  1,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_pattern));

	index_rescan(data_index_scan, key, 1, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (tuple)
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);
		if (DatumGetInt32(values[1]) == nfeatures)
		{
			deform_matrix(values[2], matrix);
			deform_vector(values[3], est_cost, rows);
			deform_vector(values[4], true_cost, rows);
		}
		else
		{
			elog(WARNING, "unexpected number of features for hash %d:\
						   expected %d features, obtained %d",
						   query_pattern,
						   nfeatures, DatumGetInt32(values[1]));
			success = false;
		}
			
	}else{
		success = false;
	}

	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	return success;
}

/**
 * load query distribution
 * modified by jim 2021.2.13
 * */
bool load_query_distribution(int num_history_data, int query_history_hash, QueryContextData	*query_context2){
	if(num_history_data < num_history_data_compute_probability_fs){
		bool		success = true;
		//if history data is less than num_history_data_compute_probability_fs, the next query distribution is uniform
        query_context2->query_distribution = palloc0(sizeof(*query_context2->query_distribution) * num_query_pattern);
		for(int i=0; i<num_query_pattern; i++){
			query_context2->query_distribution[i] = 1./num_query_pattern;
		}
		return success;
	}else{
		//if history data is num_history_data_compute_probability_fs, we use markov table.
		RangeVar   *aqo_data_table_rv;
		Relation	aqo_data_heap;
		HeapTuple	tuple;

		Relation	data_index_rel;
		Oid			data_index_rel_oid;
		IndexScanDesc data_index_scan;
		ScanKeyData	key[1];

		LOCKMODE	lockmode = AccessShareLock;

		Datum		values[3];
		bool		isnull[3];

		bool		success = true;
		int         num_query_pattern_test = 0;

		data_index_rel_oid = RelnameGetRelid("aqo_markov_table_idx");
		if (!OidIsValid(data_index_rel_oid))
		{
			disable_aqo_for_query();
			return false;
		}

		aqo_data_table_rv = makeRangeVar("public", "aqo_markov_table", -1);
		aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

		data_index_rel = index_open(data_index_rel_oid, lockmode);
		data_index_scan = index_beginscan(aqo_data_heap,
										data_index_rel,
										SnapshotSelf,
										1,
										0);

		ScanKeyInit(&key[0],
					1,
					BTEqualStrategyNumber,
					F_INT4EQ,
					Int32GetDatum(query_context.fspace_hash));

		index_rescan(data_index_scan, key, 1, NULL, 0);

		tuple = index_getnext(data_index_scan, ForwardScanDirection);

		if (tuple)
		{
			query_context2->query_distribution = palloc0(sizeof(*query_context2->query_distribution) * num_query_pattern);
			heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);
			deform_vector(values[1], query_context2->query_distribution, &num_query_pattern_test);
		}else{
			success = false;
			query_context2->query_distribution = palloc0(sizeof(*query_context2->query_distribution) * num_query_pattern);
			for(int i=0; i<num_query_pattern; i++){
				query_context2->query_distribution[i] = 1./num_query_pattern;
			}
		}

		index_endscan(data_index_scan);

		index_close(data_index_rel, lockmode);
		heap_close(aqo_data_heap, lockmode);

		return success;
	}
}
// 单表查询，当前查询的发生概率为1，其它为0
bool load_query_distribution2(int query_hash, QueryContextData *query_context2){
	bool		success = true;
	//if history data is less than num_history_data_compute_probability_fs, the next query distribution is uniform
	query_context2->query_distribution = palloc0(sizeof(*query_context2->query_distribution) * num_query_pattern);
	query_context2->query_distribution[query_hash-1] = 1;
	return true;
}
/*
 * 加载rfwr。。
 */
bool
load_fss_rfwr(int fss_hash, int ncols, LWPR_Model *model){
    RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	HeapTuple	tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[2];

	LOCKMODE	lockmode = AccessShareLock;

	Datum		values[35];
	bool		isnull[35];

	bool		success = true;
    //定义输出的RF变量
	double *nReg;
	double *trustworthy;
	double *slopeReady;
	double *sum_e2;
	double *beta0;
	double *ssp; /* used for calculating confidence bound */
	double **D;
	double **M;
    double **alpha;
	double **beta;
	double **c;
	double **SXresYres;
	double **SSs2;
	double **SSYres;
	double **SSXres;
	double **U;
	double **P;
	double **H;
	double **r;
	double **sum_w;
	double **sum_e_cv2;
	double **n_data;
	double **lambda;
	double **mean_x;
	double **var_x;
	double **s;
	double **slope;
	double **history_data_matrix;
	double *num_history_data;//保存的历史数据
	int num_query_pattern_test = 0;
	/*add by jim in 2021.1.22*/
	double **history_error_matrix;
	double *num_history_error;
	int num_rf;  //rf个数
	int i;
	int dim;   //rf的方向个数
	int nInS = model->nInStore; 
	LWPR_ReceptiveField *RF;
	data_index_rel_oid = RelnameGetRelid("aqo_fss_lwpr_access_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_data_lwpr", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  2,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_context.fspace_hash));

	ScanKeyInit(&key[1],
				2,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(fss_hash));

	index_rescan(data_index_scan, key, 2, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (tuple)
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);

		if (DatumGetInt32(values[2]) == ncols)
		{
			//分配内存
			num_rf = DatumGetInt32(values[3]);
			nReg = palloc0(sizeof(*nReg) * num_rf);
			trustworthy = palloc0(sizeof(*trustworthy) * num_rf);
			slopeReady = palloc0(sizeof(*slopeReady) * num_rf);
			sum_e2 = palloc0(sizeof(*sum_e2) * num_rf);
			beta0 = palloc0(sizeof(*beta0) * num_rf);
			ssp = palloc0(sizeof(*ssp) * num_rf);
            //cubic
			D = palloc(sizeof(*D) * num_rf);
			for (i = 0; i < num_rf; ++i){
				D[i] = palloc0(sizeof(**D) * nInS* ncols);
				// for(j = 0; j < ncols; ++j){
				// 	D[i][j] = palloc0(sizeof(***D) * ncols);
				// }
			}
			M = palloc(sizeof(*M) * num_rf);
			for (i = 0; i < num_rf; ++i){
				M[i] = palloc0(sizeof(**M) * nInS* ncols);
				// for(j = 0; j < ncols; ++j){
				// 	M[i][j] = palloc0(sizeof(***M) * ncols);
				// }
			}
			alpha = palloc(sizeof(*alpha) * num_rf);
			for (i = 0; i < num_rf; ++i){
				alpha[i] = palloc0(sizeof(**alpha) * nInS* ncols);
				// for(j = 0; j < ncols; ++j){
				// 	alpha[i][j] = palloc0(sizeof(***alpha) * ncols);
				// }
			}
			//matric
			beta = palloc(sizeof(*beta) * num_rf);
		    for (i = 0; i < num_rf; ++i)
				beta[i] = palloc0(sizeof(**beta) * ncols);
			c = palloc(sizeof(*c) * num_rf);
		    for (i = 0; i < num_rf; ++i)
				c[i] = palloc0(sizeof(**c) * ncols);
			SXresYres = palloc(sizeof(*SXresYres) * num_rf);
			for (i = 0; i < num_rf; ++i){
				SXresYres[i] = palloc0(sizeof(**SXresYres) * nInS* ncols);
				// for(j = 0; j < ncols; ++j){
				// 	SXresYres[i][j] = palloc0(sizeof(***SXresYres) * ncols);
				// }
			}
			SSs2 = palloc(sizeof(*SSs2) * num_rf);
			for (i = 0; i < num_rf; ++i)
				SSs2[i] = palloc0(sizeof(**SSs2) * ncols);
			SSYres = palloc(sizeof(*SSYres) * num_rf);
			for (i = 0; i < num_rf; ++i)
				SSYres[i] = palloc0(sizeof(**SSYres) * ncols);
			SSXres = palloc(sizeof(*SSXres) * num_rf);
			for (i = 0; i < num_rf; ++i){
				SSXres[i] = palloc0(sizeof(**SSXres) * nInS* ncols);
				// for(j = 0; j < ncols; ++j){
				// 	SSXres[i][j] = palloc0(sizeof(***SSXres) * ncols);
				// }
			}
			U = palloc(sizeof(*U) * num_rf);
			for (i = 0; i < num_rf; ++i){
				U[i] = palloc0(sizeof(**U) * nInS* ncols);
				// for(j = 0; j < ncols; ++j){
				// 	U[i][j] = palloc0(sizeof(***U) * ncols);
				// }
			}
			P = palloc(sizeof(*P) * num_rf);
			for (i = 0; i < num_rf; ++i){
				P[i] = palloc0(sizeof(**P) * nInS* ncols);
				// for(j = 0; j < ncols; ++j){
				// 	P[i][j] = palloc0(sizeof(***P) * ncols);
				// }
			}
			H = palloc(sizeof(*H) * num_rf);
			for (i = 0; i < num_rf; ++i)
				H[i] = palloc0(sizeof(**H) * ncols);
			r = palloc(sizeof(*r) * num_rf);
			for (i = 0; i < num_rf; ++i)
				r[i] = palloc0(sizeof(**r) * ncols);
			sum_w = palloc(sizeof(*sum_w) * num_rf);
			for (i = 0; i < num_rf; ++i)
				sum_w[i] = palloc0(sizeof(**sum_w) * ncols);
			sum_e_cv2 = palloc(sizeof(*sum_e_cv2) * num_rf);
			for (i = 0; i < num_rf; ++i)
				sum_e_cv2[i] = palloc0(sizeof(**sum_e_cv2) * ncols);
			n_data = palloc(sizeof(*n_data) * num_rf);
			for (i = 0; i < num_rf; ++i)
				n_data[i] = palloc0(sizeof(**n_data) * ncols);
			lambda = palloc(sizeof(*lambda) * num_rf);
			for (i = 0; i < num_rf; ++i)
				lambda[i] = palloc0(sizeof(**lambda) * ncols);
			mean_x = palloc(sizeof(*mean_x) * num_rf);
			for (i = 0; i < num_rf; ++i)
				mean_x[i] = palloc0(sizeof(**mean_x) * ncols);
			var_x = palloc(sizeof(*var_x) * num_rf);
			for (i = 0; i < num_rf; ++i)
				var_x[i] = palloc0(sizeof(**var_x) * ncols);
			s = palloc(sizeof(*s) * num_rf);
			for (i = 0; i < num_rf; ++i)
				s[i] = palloc0(sizeof(**s) * ncols);
			slope = palloc(sizeof(*slope) * num_rf);
			for (i = 0; i < num_rf; ++i)
				slope[i] = palloc0(sizeof(**slope) * ncols);
			/*初始化历史数据的矩阵和数量 */
			history_data_matrix = palloc(sizeof(*history_data_matrix) * num_query_pattern);
			for (i = 0; i < num_query_pattern; ++i)
				history_data_matrix[i] = palloc0(sizeof(**history_data_matrix) * (1 + ncols*num_history_data_compute_probability_rf));
			num_history_data = palloc0(sizeof(*num_history_data) * num_query_pattern);
			//初始化误差矩阵
			history_error_matrix = palloc(sizeof(*history_error_matrix) * num_rf);
			for (i = 0; i < num_rf; ++i)
				history_error_matrix[i] = palloc0(sizeof(**history_error_matrix) * num_pred_error_history);
			num_history_error = palloc0(sizeof(*num_history_error) * num_rf);
			//获取表中信息
			deform_vector(values[4], nReg, &num_rf); 
			deform_vector(values[5], trustworthy, &num_rf);
			deform_vector(values[6], slopeReady, &num_rf);
			deform_vector(values[7], sum_e2, &num_rf);
			deform_vector(values[8], beta0, &num_rf);
			deform_matrix(values[9], D);
			deform_matrix(values[10], M);
			deform_matrix(values[11], alpha);
			deform_matrix(values[12], beta);
			deform_matrix(values[13], c);
			deform_matrix(values[14], SXresYres);
			deform_matrix(values[15], SSs2);
			deform_matrix(values[16], SSYres);
			deform_matrix(values[17], SSXres);
			deform_matrix(values[18], U);
			deform_matrix(values[19], P);
			deform_matrix(values[20], H);
			deform_matrix(values[21], r);
			deform_matrix(values[22], sum_w);
			deform_matrix(values[23], sum_e_cv2);
			deform_matrix(values[24], n_data);
			deform_matrix(values[25], lambda);
			deform_matrix(values[26], mean_x);
			deform_matrix(values[27], var_x);
			deform_matrix(values[28], s);
			deform_matrix(values[29], slope);
			deform_matrix(values[30], history_data_matrix);
			deform_vector(values[31], num_history_data, &num_query_pattern_test);
			deform_vector(values[32], ssp, &num_rf);
			deform_matrix(values[33], history_error_matrix);
			deform_vector(values[34], num_history_error, &num_rf);
			//读取各个rf的信息
			//model->numRFS = num_rf;
			for(i=0;i<num_rf;i++){
				dim = (int)nReg[i];
				RF = lwpr_aux_add_rf(model,dim);
                if (RF==NULL) return 0;
				//将信息写入RF中
				RF->nReg = dim;
				RF->trustworthy = (int)trustworthy[i];
				RF->slopeReady = (int)slopeReady[i];
				RF->sum_e2 = sum_e2[i];
				RF->beta0 = beta0[i];
				RF->SSp = ssp[i];
				memcpy(RF->D, D[i], nInS*ncols*sizeof(double));
				memcpy(RF->M, M[i], nInS*ncols*sizeof(double));
				memcpy(RF->alpha, alpha[i], nInS*ncols*sizeof(double));
				memcpy(RF->beta, beta[i], ncols*sizeof(double));
				memcpy(RF->c, c[i], ncols*sizeof(double));
				memcpy(RF->SXresYres, SXresYres[i], nInS*ncols*sizeof(double));
				memcpy(RF->SSs2, SSs2[i], ncols*sizeof(double));
				memcpy(RF->SSYres, SSYres[i], ncols*sizeof(double));
				memcpy(RF->SSXres, SSXres[i], nInS*ncols*sizeof(double));
				memcpy(RF->U, U[i], nInS*ncols*sizeof(double));
				memcpy(RF->P, P[i], nInS*ncols*sizeof(double));
				memcpy(RF->H, H[i], ncols*sizeof(double));
				memcpy(RF->r, r[i], ncols*sizeof(double));
				memcpy(RF->sum_w, sum_w[i], ncols*sizeof(double));
				memcpy(RF->sum_e_cv2, sum_e_cv2[i], ncols*sizeof(double));
				memcpy(RF->n_data, n_data[i], ncols*sizeof(double));
				memcpy(RF->lambda, lambda[i], ncols*sizeof(double));
				memcpy(RF->mean_x, mean_x[i], ncols*sizeof(double));
				memcpy(RF->var_x, var_x[i], ncols*sizeof(double));
				memcpy(RF->s, s[i], ncols*sizeof(double));
				memcpy(RF->slope, slope[i], ncols*sizeof(double));
				memcpy(RF->pred_error_history, history_error_matrix[i], num_pred_error_history*sizeof(double));
				RF->pred_error_num = num_history_error[i];
			}
			//把当前hash值保存到model中
			model->fss_hash = fss_hash;
			memcpy(model->num_history_data, num_history_data, num_query_pattern*sizeof(double));
			//also save the history data
			for (i=0; i< num_query_pattern; i++){
				memcpy(model->history_data_matrix[i], history_data_matrix[i], (1 + ncols*num_history_data_compute_probability_rf)*sizeof(double));
			}
		   //释放内存
			pfree(nReg);
			pfree(trustworthy);
			pfree(slopeReady);
			pfree(sum_e2);
			pfree(beta0);
			pfree(ssp);
            //cubic
			for (i = 0; i < num_rf; ++i){
				// for(j = 0; j < ncols; ++j){
				// 	pfree(D[i][j]);
				// }
				pfree(D[i]);
			}
			pfree(D);
			for (i = 0; i < num_rf; ++i){
				// for(j = 0; j < ncols; ++j){
				// 	pfree(M[i][j]);
				// }
				pfree(M[i]);
			}
			pfree(M);
			for (i = 0; i < num_rf; ++i){
				// for(j = 0; j < ncols; ++j){
				// 	pfree(alpha[i][j]);
				// }
				pfree(alpha[i]);
			}
			pfree(alpha);
			for (i = 0; i < num_rf; ++i)
				pfree(beta[i]);
		    pfree(beta);
			//matric
			for (i = 0; i < num_rf; ++i)
				pfree(c[i]);
		    pfree(c);
			for (i = 0; i < num_rf; ++i){
				// for(j = 0; j < ncols; ++j){
				// 	pfree(SXresYres[i][j]);
				// }
				pfree(SXresYres[i]);
			}
			pfree(SXresYres);
			for (i = 0; i < num_rf; ++i)
				pfree(SSs2[i]);
		    pfree(SSs2);
			for (i = 0; i < num_rf; ++i)
				pfree(SSYres[i]);
		    pfree(SSYres);
	        for (i = 0; i < num_rf; ++i){
				// for(j = 0; j < ncols; ++j){
				// 	pfree(SSXres[i][j]);
				// }
				pfree(SSXres[i]);
			}
			pfree(SSXres);
			for (i = 0; i < num_rf; ++i){
				// for(j = 0; j < ncols; ++j){
				// 	pfree(U[i][j]);
				// }
				pfree(U[i]);
			}
			pfree(U);
			for (i = 0; i < num_rf; ++i){
				// for(j = 0; j < ncols; ++j){
				// 	pfree(P[i][j]);
				// }
				pfree(P[i]);
			}
			pfree(P);
			for (i = 0; i < num_rf; ++i)
				pfree(H[i]);
		    pfree(H);
			for (i = 0; i < num_rf; ++i)
				pfree(r[i]);
		    pfree(r);
			for (i = 0; i < num_rf; ++i)
				pfree(sum_w[i]);
		    pfree(sum_w);
			for (i = 0; i < num_rf; ++i)
				pfree(sum_e_cv2[i]);
		    pfree(sum_e_cv2);
			for (i = 0; i < num_rf; ++i)
				pfree(n_data[i]);
		    pfree(n_data);
			for (i = 0; i < num_rf; ++i)
				pfree(lambda[i]);
		    pfree(lambda);
			for (i = 0; i < num_rf; ++i)
				pfree(mean_x[i]);
		    pfree(mean_x);
			for (i = 0; i < num_rf; ++i)
				pfree(var_x[i]);
		    pfree(var_x);
			for (i = 0; i < num_rf; ++i)
				pfree(s[i]);
		    pfree(s);
			for (i = 0; i < num_rf; ++i)
				pfree(slope[i]);
		    pfree(slope);
			for (i = 0; i < num_query_pattern; ++i)
				pfree(history_data_matrix[i]);
		    pfree(history_data_matrix);
			pfree(num_history_data);
			for (i = 0; i < num_rf; ++i)
				pfree(history_error_matrix[i]);
		    pfree(history_error_matrix);
			pfree(num_history_error);
		}
		else
		{
			elog(WARNING, "unexpected number of features for hash (%d, %d):\
						   expected %d features, obtained %d",
						   query_context.fspace_hash,
						   fss_hash, ncols, DatumGetInt32(values[2]));
			success = false;
		}
	}else{
        //把当前hash值保存到model中
	    model->fss_hash = fss_hash;
		success = false;
	}
	
	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	return success;
}
/*
 * Updates the specified line in the specified feature subspace.
 * Returns false if the operation failed, true otherwise.
 *
 * 'fss_hash' specifies the feature subspace
 * 'nrows' x 'ncols' is the shape of 'matrix'
 * 'targets' is vector of size 'nrows'
 * 'old_nrows' is previous number of rows in matrix
 * 'changed_rows' is an integer list of changed lines
 */
bool
update_fss(int fss_hash, int nrows, int ncols,
		   double **matrix, double *targets,
		   int old_nrows, List *changed_rows)
{
	RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	TupleDesc	tuple_desc;
	HeapTuple	tuple,
				nw_tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[2];

	LOCKMODE	lockmode = RowExclusiveLock;

	Datum		values[5];
	bool		isnull[5] = { false, false, false, false, false };
	bool		replace[5] = { false, false, false, true, true };

	data_index_rel_oid = RelnameGetRelid("aqo_fss_access_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_data", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	tuple_desc = RelationGetDescr(aqo_data_heap);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  2,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_context.fspace_hash));

	ScanKeyInit(&key[1],
				2,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(fss_hash));

	index_rescan(data_index_scan, key, 2, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (!tuple)
	{
		values[0] = Int32GetDatum(query_context.fspace_hash);
		values[1] = Int32GetDatum(fss_hash);
		values[2] = Int32GetDatum(ncols);
		values[3] = PointerGetDatum(form_matrix(matrix, nrows, ncols));
		values[4] = PointerGetDatum(form_vector(targets, nrows));
		tuple = heap_form_tuple(tuple_desc, values, isnull);
		PG_TRY();
		{
			simple_heap_insert(aqo_data_heap, tuple);
			my_index_insert(data_index_rel, values, isnull, &(tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_YES);
		}
		PG_CATCH();
		{
			CommandCounterIncrement();
			simple_heap_delete(aqo_data_heap, &(tuple->t_self));
			PG_RE_THROW();
		}
		PG_END_TRY();
	}
	else
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);
		values[3] = PointerGetDatum(form_matrix(matrix, nrows, ncols));
		values[4] = PointerGetDatum(form_vector(targets, nrows));
		nw_tuple = heap_modify_tuple(tuple, tuple_desc,
									 values, isnull, replace);
		if (my_simple_heap_update(aqo_data_heap, &(nw_tuple->t_self), nw_tuple))
		{
			my_index_insert(data_index_rel, values, isnull, &(nw_tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_YES);
		}
		else
		{
			/*
			 * Ooops, somebody concurrently updated the tuple. We have to
			 * merge our changes somehow, but now we just discard ours. We
			 * don't believe in high probability of simultaneously finishing
			 * of two long, complex, and important queries, so we don't loss
			 * important data.
			 */
		}
	}

	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	CommandCounterIncrement();

	return true;
}
/* updata rf's datahouse */
bool
update_rf_datahouse(int fss_hash, int rf_hash, int ncols, int nrows, double **matrix, double *targets)
{
	RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	TupleDesc	tuple_desc;
	HeapTuple	tuple,
				nw_tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[3];

	LOCKMODE	lockmode = RowExclusiveLock;

	Datum		values[5];
	bool		isnull[5] = { false, false, false, false, false };
	bool		replace[5] = { false, false, false, true, true };

	data_index_rel_oid = RelnameGetRelid("aqo_fss_lwpr_datahouse_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_data_house_lwpr", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	tuple_desc = RelationGetDescr(aqo_data_heap);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  3,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_context.fspace_hash));

	ScanKeyInit(&key[1],
				2,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(fss_hash));

	ScanKeyInit(&key[2],
				3,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(rf_hash));

	index_rescan(data_index_scan, key, 3, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (!tuple)
	{
		values[0] = Int32GetDatum(query_context.fspace_hash);
		values[1] = Int32GetDatum(fss_hash);
		values[2] = Int32GetDatum(rf_hash);
		values[3] = PointerGetDatum(form_matrix(matrix, nrows, ncols));
		values[4] = PointerGetDatum(form_vector(targets, nrows));
		tuple = heap_form_tuple(tuple_desc, values, isnull);
		PG_TRY();
		{
			simple_heap_insert(aqo_data_heap, tuple);
			my_index_insert(data_index_rel, values, isnull, &(tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_YES);
		}
		PG_CATCH();
		{
			CommandCounterIncrement();
			simple_heap_delete(aqo_data_heap, &(tuple->t_self));
			PG_RE_THROW();
		}
		PG_END_TRY();
	}
	else
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);
		values[3] = PointerGetDatum(form_matrix(matrix, nrows, ncols));
		values[4] = PointerGetDatum(form_vector(targets, nrows));
		nw_tuple = heap_modify_tuple(tuple, tuple_desc,
									 values, isnull, replace);
		if (my_simple_heap_update(aqo_data_heap, &(nw_tuple->t_self), nw_tuple))
		{
			my_index_insert(data_index_rel, values, isnull, &(nw_tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_YES);
		}
		else
		{
			/*
			 * Ooops, somebody concurrently updated the tuple. We have to
			 * merge our changes somehow, but now we just discard ours. We
			 * don't believe in high probability of simultaneously finishing
			 * of two long, complex, and important queries, so we don't loss
			 * important data.
			 */
		}
	}

	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	CommandCounterIncrement();

	return true;
}

/* updata rf's datahouse */
bool
update_best_two_costs(int query_pattern, int ncols, int nrows, double **matrix, double *est_cost, double *true_cost)
{
	RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	TupleDesc	tuple_desc;
	HeapTuple	tuple,
				nw_tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[1];

	LOCKMODE	lockmode = RowExclusiveLock;

	Datum		values[5];
	bool		isnull[5] = { false, false, false, false, false };
	bool		replace[5] = { false, false, true, true, true };

	data_index_rel_oid = RelnameGetRelid("aqo_best_two_costs_table_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_best_two_costs_table", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	tuple_desc = RelationGetDescr(aqo_data_heap);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  1,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_pattern));

	index_rescan(data_index_scan, key, 1, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (!tuple)
	{
		values[0] = Int32GetDatum(query_pattern);
		values[1] = Int32GetDatum(ncols);
		values[2] = PointerGetDatum(form_matrix(matrix, nrows, ncols));
		values[3] = PointerGetDatum(form_vector(est_cost, nrows));
		values[4] = PointerGetDatum(form_vector(true_cost, nrows));

		tuple = heap_form_tuple(tuple_desc, values, isnull);
		PG_TRY();
		{
			simple_heap_insert(aqo_data_heap, tuple);
			my_index_insert(data_index_rel, values, isnull, &(tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_YES);
		}
		PG_CATCH();
		{
			CommandCounterIncrement();
			simple_heap_delete(aqo_data_heap, &(tuple->t_self));
			PG_RE_THROW();
		}
		PG_END_TRY();
	}
	else
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);
		values[2] = PointerGetDatum(form_matrix(matrix, nrows, ncols));
		values[3] = PointerGetDatum(form_vector(est_cost, nrows));
		values[4] = PointerGetDatum(form_vector(true_cost, nrows));
		
		nw_tuple = heap_modify_tuple(tuple, tuple_desc,
									 values, isnull, replace);
		if (my_simple_heap_update(aqo_data_heap, &(nw_tuple->t_self), nw_tuple))
		{
			my_index_insert(data_index_rel, values, isnull, &(nw_tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_YES);
		}
		else
		{
			/*
			 * Ooops, somebody concurrently updated the tuple. We have to
			 * merge our changes somehow, but now we just discard ours. We
			 * don't believe in high probability of simultaneously finishing
			 * of two long, complex, and important queries, so we don't loss
			 * important data.
			 */
		}
	}

	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	CommandCounterIncrement();

	return true;
}
/*
 * 更新rfwr
 */
bool
update_fss_rfwr(int fss_hash, int ncols, LWPR_Model *model)
{
	RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	TupleDesc	tuple_desc;
	HeapTuple	tuple,
				nw_tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[2];
	LOCKMODE	lockmode = RowExclusiveLock;
	Datum		values[35];
	bool		isnull[35] = {false, false, false, false, false, 
							  false, false, false, false, false,
							  false, false, false, false, false,
							  false, false, false, false, false,
							  false, false, false, false, false, 
							  false, false, false, false, false,
							  false, false, false, false, false};
	bool		replace[35] = {false, false, false, true, true, 
							  true, true, true, true, true,
							  true, true, true, true, true,
							  true, true, true, true, true,
							  true, true, true, true, true, 
							  true, true, true, true, true,
							  true, true, true, true, true};
	//定义输入的RF变量
	double *nReg;
	double *trustworthy;
	double *slopeReady;
	double *sum_e2;
	double *beta0;
	double *ssp;
	double **D;
	double **M;
    double **alpha;
	double **beta;
	double **c;
	double **SXresYres;
	double **SSs2;
	double **SSYres;
	double **SSXres;
	double **U;
	double **P;
	double **H;
	double **r;
	double **sum_w;
	double **sum_e_cv2;
	double **n_data;
	double **lambda;
	double **mean_x;
	double **var_x;
	double **s;
	double **slope;
	double **history_data_matrix;
	double *num_history_data;
	/*add by jim in 2021.1.22*/
	double **history_error_matrix;
	double *num_history_error = 0;
	int num_rf = model->numRFS;  //rf个数
	int i;
	int nInS = model->nInStore;
	//int dim;   //个rf的方向个数
	LWPR_ReceptiveField *RF;
    
	//分配内存
	nReg = palloc0(sizeof(*nReg) * num_rf);
	trustworthy = palloc0(sizeof(*trustworthy) * num_rf);
	slopeReady = palloc0(sizeof(*slopeReady) * num_rf);
	sum_e2 = palloc0(sizeof(*sum_e2) * num_rf);
	beta0 = palloc0(sizeof(*beta0) * num_rf);
	ssp = palloc0(sizeof(*ssp) * num_rf);
	//cubic
	D = palloc(sizeof(*D) * num_rf);
	for (i = 0; i < num_rf; ++i){
		D[i] = palloc0(sizeof(**D) * nInS * ncols);
		// for(j = 0; j < ncols; ++j){
		// 	D[i][j] = palloc0(sizeof(***D) * ncols);
		// }
	}
	M = palloc(sizeof(*M) * num_rf);
	for (i = 0; i < num_rf; ++i){
		M[i] = palloc0(sizeof(**M) * nInS * ncols);
		// for(j = 0; j < ncols; ++j){
		// 	M[i][j] = palloc0(sizeof(***M) * ncols);
		// }
	}
	alpha = palloc(sizeof(*alpha) * num_rf);
	for (i = 0; i < num_rf; ++i){
		alpha[i] = palloc0(sizeof(**alpha) * nInS * ncols);
		// for(j = 0; j < ncols; ++j){
		// 	alpha[i][j] = palloc0(sizeof(***alpha) * ncols);
		// }
	}
	//matric
	beta = palloc(sizeof(*beta) * num_rf);
	for (i = 0; i < num_rf; ++i)
		beta[i] = palloc0(sizeof(**beta) * ncols);
	c = palloc(sizeof(*c) * num_rf);
	for (i = 0; i < num_rf; ++i)
		c[i] = palloc0(sizeof(**c) * ncols);
	SXresYres = palloc(sizeof(*SXresYres) * num_rf);
	for (i = 0; i < num_rf; ++i){
		SXresYres[i] = palloc0(sizeof(**SXresYres) * nInS * ncols);
		// for(j = 0; j < ncols; ++j){
		// 	SXresYres[i][j] = palloc0(sizeof(***SXresYres) * ncols);
		// }
	}
	SSs2 = palloc(sizeof(*SSs2) * num_rf);
	for (i = 0; i < num_rf; ++i)
		SSs2[i] = palloc0(sizeof(**SSs2) * ncols);
	SSYres = palloc(sizeof(*SSYres) * num_rf);
	for (i = 0; i < num_rf; ++i)
		SSYres[i] = palloc0(sizeof(**SSYres) * ncols);
	SSXres = palloc(sizeof(*SSXres) * num_rf);
	for (i = 0; i < num_rf; ++i){
		SSXres[i] = palloc0(sizeof(**SSXres) * nInS * ncols);
		// for(j = 0; j < ncols; ++j){
		// 	SSXres[i][j] = palloc0(sizeof(***SSXres) * ncols);
		// }
	}
	U = palloc(sizeof(*U) * num_rf);
	for (i = 0; i < num_rf; ++i){
		U[i] = palloc0(sizeof(**U) * nInS * ncols);
		// for(j = 0; j < ncols; ++j){
		// 	U[i][j] = palloc0(sizeof(***U) * ncols);
		// }
	}
	P = palloc(sizeof(*P) * num_rf);
	for (i = 0; i < num_rf; ++i){
		P[i] = palloc0(sizeof(**P) * nInS * ncols);
		// for(j = 0; j < ncols; ++j){
		// 	P[i][j] = palloc0(sizeof(***P) * ncols);
		// }
	}
	H = palloc(sizeof(*H) * num_rf);
	for (i = 0; i < num_rf; ++i)
		H[i] = palloc0(sizeof(**H) * ncols);
	r = palloc(sizeof(*r) * num_rf);
	for (i = 0; i < num_rf; ++i)
		r[i] = palloc0(sizeof(**r) * ncols);
	sum_w = palloc(sizeof(*sum_w) * num_rf);
	for (i = 0; i < num_rf; ++i)
		sum_w[i] = palloc0(sizeof(**sum_w) * ncols);
	sum_e_cv2 = palloc(sizeof(*sum_e_cv2) * num_rf);
	for (i = 0; i < num_rf; ++i)
		sum_e_cv2[i] = palloc0(sizeof(**sum_e_cv2) * ncols);
	n_data = palloc(sizeof(*n_data) * num_rf);
	for (i = 0; i < num_rf; ++i)
		n_data[i] = palloc0(sizeof(**n_data) * ncols);
	lambda = palloc(sizeof(*lambda) * num_rf);
	for (i = 0; i < num_rf; ++i)
		lambda[i] = palloc0(sizeof(**lambda) * ncols);
	mean_x = palloc(sizeof(*mean_x) * num_rf);
	for (i = 0; i < num_rf; ++i)
		mean_x[i] = palloc0(sizeof(**mean_x) * ncols);
	var_x = palloc(sizeof(*var_x) * num_rf);
	for (i = 0; i < num_rf; ++i)
		var_x[i] = palloc0(sizeof(**var_x) * ncols);
	s = palloc(sizeof(*s) * num_rf);
	for (i = 0; i < num_rf; ++i)
		s[i] = palloc0(sizeof(**s) * ncols);
	slope = palloc(sizeof(*slope) * num_rf);
	for (i = 0; i < num_rf; ++i)
		slope[i] = palloc0(sizeof(**slope) * ncols);
	//初始化history_data
	history_data_matrix = palloc(sizeof(*history_data_matrix) * num_query_pattern);
	for (i = 0; i < num_query_pattern; ++i)
		history_data_matrix[i] = palloc0(sizeof(**history_data_matrix) * (1 + ncols*num_history_data_compute_probability_rf));
	num_history_data = palloc0(sizeof(*num_history_data) * num_query_pattern);
	//初始化误差矩阵
	history_error_matrix = palloc(sizeof(*history_error_matrix) * num_rf);
	for (i = 0; i < num_rf; ++i)
		history_error_matrix[i] = palloc0(sizeof(**history_error_matrix) * num_pred_error_history);
	num_history_error = palloc0(sizeof(*num_history_error) * num_rf);
	//下面对rfs信息写入到pointer中
	for(i=0;i<num_rf;i++){
		RF = model->rf[i];
		if (RF==NULL) return 0;
		//将信息写入RF中
		nReg[i] = RF->nReg;
		trustworthy[i] = RF->trustworthy;
		slopeReady[i] = RF->slopeReady;
		sum_e2[i] = RF->sum_e2;
		beta0[i] = RF->beta0;
		ssp[i] = RF->SSp;
		memcpy(D[i], RF->D, nInS*ncols*sizeof(double));
		memcpy(M[i], RF->M, nInS*ncols*sizeof(double));
		memcpy(alpha[i], RF->alpha, nInS*ncols*sizeof(double));
		memcpy(beta[i], RF->beta, ncols*sizeof(double));
		memcpy(c[i], RF->c, ncols*sizeof(double));
		memcpy(SXresYres[i], RF->SXresYres, nInS*ncols*sizeof(double));
		memcpy(SSs2[i], RF->SSs2, ncols*sizeof(double));
		memcpy(SSYres[i], RF->SSYres, ncols*sizeof(double));
		memcpy(SSXres[i], RF->SSXres, nInS*ncols*sizeof(double));
		memcpy(U[i], RF->U, nInS*ncols*sizeof(double));
		memcpy(P[i], RF->P, nInS*ncols*sizeof(double));
		memcpy(H[i], RF->H, ncols*sizeof(double));
		memcpy(r[i], RF->r, ncols*sizeof(double));
		memcpy(sum_w[i], RF->sum_w, ncols*sizeof(double));
		memcpy(sum_e_cv2[i], RF->sum_e_cv2, ncols*sizeof(double));
		memcpy(n_data[i], RF->n_data, ncols*sizeof(double));
		memcpy(lambda[i], RF->lambda, ncols*sizeof(double));
		memcpy(mean_x[i], RF->mean_x, ncols*sizeof(double));
		memcpy(var_x[i], RF->var_x, ncols*sizeof(double));
		memcpy(s[i], RF->s, ncols*sizeof(double));
		memcpy(slope[i], RF->slope, ncols*sizeof(double));
		memcpy(history_error_matrix[i], RF->pred_error_history, num_pred_error_history*sizeof(double));
        num_history_error[i] = RF->pred_error_num;
	}
	memcpy(num_history_data, model->num_history_data, num_query_pattern*sizeof(double));
	//将历史数据也写入到数据库
	for (i=0; i< num_query_pattern; i++){
		memcpy(history_data_matrix[i], model->history_data_matrix[i], (1 + ncols*num_history_data_compute_probability_rf)*sizeof(double));
	}
	data_index_rel_oid = RelnameGetRelid("aqo_fss_lwpr_access_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_data_lwpr", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	tuple_desc = RelationGetDescr(aqo_data_heap);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  2,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_context.fspace_hash));

	ScanKeyInit(&key[1],
				2,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(fss_hash));

	index_rescan(data_index_scan, key, 2, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (!tuple)
	{
		//三个不能修改的
		values[0] = Int32GetDatum(query_context.fspace_hash);
		values[1] = Int32GetDatum(fss_hash);
		values[2] = Int32GetDatum(ncols);
		//其它进行修改
		values[3] = Int32GetDatum(num_rf);
		values[4] = PointerGetDatum(form_vector(nReg, num_rf));
		values[5] = PointerGetDatum(form_vector(trustworthy, num_rf));
		values[6] = PointerGetDatum(form_vector(slopeReady, num_rf));
		values[7] = PointerGetDatum(form_vector(sum_e2, num_rf));
		values[8] = PointerGetDatum(form_vector(beta0, num_rf));
		// values[9] = PointerGetDatum(form_cubic(D, ncols, ncols, num_rf));
		// values[10] = PointerGetDatum(form_cubic(M, ncols, ncols, num_rf));
		// values[11] = PointerGetDatum(form_cubic(alpha, ncols, ncols, num_rf));
		values[9] =  PointerGetDatum(form_matrix(D, num_rf, nInS * ncols));
		values[10] = PointerGetDatum(form_matrix(M, num_rf, nInS * ncols));
		values[11] = PointerGetDatum(form_matrix(alpha, num_rf, nInS * ncols));
		values[12] = PointerGetDatum(form_matrix(beta, num_rf, ncols));
		values[13] = PointerGetDatum(form_matrix(c, num_rf, ncols));
		//values[14] = PointerGetDatum(form_cubic(SXresYres, ncols, ncols, num_rf));
		values[14] = PointerGetDatum(form_matrix(SXresYres, num_rf, nInS * ncols));
		values[15] = PointerGetDatum(form_matrix(SSs2, num_rf, ncols));
		values[16] = PointerGetDatum(form_matrix(SSYres, num_rf, ncols));
		// values[17] = PointerGetDatum(form_cubic(SSXres, ncols, ncols, num_rf));
		// values[18] = PointerGetDatum(form_cubic(U, ncols, ncols, num_rf));
		// values[19] = PointerGetDatum(form_cubic(P, ncols, ncols, num_rf));
		values[17] = PointerGetDatum(form_matrix(SSXres, num_rf, nInS * ncols));
		values[18] = PointerGetDatum(form_matrix(U, num_rf, nInS * ncols));
		values[19] = PointerGetDatum(form_matrix(P, num_rf, nInS * ncols));
		values[20] = PointerGetDatum(form_matrix(H, num_rf, ncols));
		values[21] = PointerGetDatum(form_matrix(r, num_rf, ncols));
		values[22] = PointerGetDatum(form_matrix(sum_w, num_rf, ncols));
		values[23] = PointerGetDatum(form_matrix(sum_e_cv2, num_rf, ncols));
		values[24] = PointerGetDatum(form_matrix(n_data, num_rf, ncols));
		values[25] = PointerGetDatum(form_matrix(lambda, num_rf, ncols));
		values[26] = PointerGetDatum(form_matrix(mean_x, num_rf, ncols));
		values[27] = PointerGetDatum(form_matrix(var_x, num_rf, ncols));
		values[28] = PointerGetDatum(form_matrix(s, num_rf, ncols));
		values[29] = PointerGetDatum(form_matrix(slope, num_rf, ncols));
		values[30] = PointerGetDatum(form_matrix(history_data_matrix, num_query_pattern, 1 + ncols*num_history_data_compute_probability_rf));
		values[31] = PointerGetDatum(form_vector(num_history_data, num_query_pattern));
		values[32] = PointerGetDatum(form_vector(ssp, num_rf)); /*new add for calculating confidence bound */
		values[33] = PointerGetDatum(form_matrix(history_error_matrix, num_rf, num_pred_error_history));
		values[34] = PointerGetDatum(form_vector(num_history_error, num_rf));
		tuple = heap_form_tuple(tuple_desc, values, isnull);
		PG_TRY();
		{
			simple_heap_insert(aqo_data_heap, tuple);
			my_index_insert(data_index_rel, values, isnull, &(tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_YES);
		}
		PG_CATCH();
		{
			CommandCounterIncrement();
			simple_heap_delete(aqo_data_heap, &(tuple->t_self));
			PG_RE_THROW();
		}
		PG_END_TRY();
	}
	else
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);
		values[3] = Int32GetDatum(num_rf);
		values[4] = PointerGetDatum(form_vector(nReg, num_rf));
		values[5] = PointerGetDatum(form_vector(trustworthy, num_rf));
		values[6] = PointerGetDatum(form_vector(slopeReady, num_rf));
		values[7] = PointerGetDatum(form_vector(sum_e2, num_rf));
		values[8] = PointerGetDatum(form_vector(beta0, num_rf));
		// values[9] = PointerGetDatum(form_cubic(D, ncols, ncols, num_rf));
		// values[10] = PointerGetDatum(form_cubic(M, ncols, ncols, num_rf));
		// values[11] = PointerGetDatum(form_cubic(alpha, ncols, ncols, num_rf));
		values[9] =  PointerGetDatum(form_matrix(D, num_rf, nInS * ncols));
		values[10] = PointerGetDatum(form_matrix(M, num_rf, nInS * ncols));
		values[11] = PointerGetDatum(form_matrix(alpha, num_rf, nInS * ncols));
		values[12] = PointerGetDatum(form_matrix(beta, num_rf, ncols));
		values[13] = PointerGetDatum(form_matrix(c, num_rf, ncols));
		//values[14] = PointerGetDatum(form_cubic(SXresYres, ncols, ncols, num_rf));
		values[14] = PointerGetDatum(form_matrix(SXresYres, num_rf, nInS * ncols));
		values[15] = PointerGetDatum(form_matrix(SSs2, num_rf, ncols));
		values[16] = PointerGetDatum(form_matrix(SSYres, num_rf, ncols));
		// values[17] = PointerGetDatum(form_cubic(SSXres, ncols, ncols, num_rf));
		// values[18] = PointerGetDatum(form_cubic(U, ncols, ncols, num_rf));
		// values[19] = PointerGetDatum(form_cubic(P, ncols, ncols, num_rf));
		values[17] = PointerGetDatum(form_matrix(SSXres, num_rf, nInS * ncols));
		values[18] = PointerGetDatum(form_matrix(U, num_rf, nInS * ncols));
		values[19] = PointerGetDatum(form_matrix(P, num_rf, nInS * ncols));
		values[20] = PointerGetDatum(form_matrix(H, num_rf, ncols));
		values[21] = PointerGetDatum(form_matrix(r, num_rf, ncols));
		values[22] = PointerGetDatum(form_matrix(sum_w, num_rf, ncols));
		values[23] = PointerGetDatum(form_matrix(sum_e_cv2, num_rf, ncols));
		values[24] = PointerGetDatum(form_matrix(n_data, num_rf, ncols));
		values[25] = PointerGetDatum(form_matrix(lambda, num_rf, ncols));
		values[26] = PointerGetDatum(form_matrix(mean_x, num_rf, ncols));
		values[27] = PointerGetDatum(form_matrix(var_x, num_rf, ncols));
		values[28] = PointerGetDatum(form_matrix(s, num_rf, ncols));
		values[29] = PointerGetDatum(form_matrix(slope, num_rf, ncols));
		values[30] = PointerGetDatum(form_matrix(history_data_matrix, num_query_pattern, 1 + ncols*num_history_data_compute_probability_rf));
		values[31] = PointerGetDatum(form_vector(num_history_data, num_query_pattern));
		values[32] = PointerGetDatum(form_vector(ssp, num_rf)); /*new add for calculating confidence bound */
		values[33] = PointerGetDatum(form_matrix(history_error_matrix, num_rf, num_pred_error_history));
		values[34] = PointerGetDatum(form_vector(num_history_error, num_rf));
		nw_tuple = heap_modify_tuple(tuple, tuple_desc,
									 values, isnull, replace);
		if (my_simple_heap_update(aqo_data_heap, &(nw_tuple->t_self), nw_tuple))
		{
			my_index_insert(data_index_rel, values, isnull, &(nw_tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_NO);
		}
		else
		{
			/*
			 * Ooops, somebody concurrently updated the tuple. We have to
			 * merge our changes somehow, but now we just discard ours. We
			 * don't believe in high probability of simultaneously finishing
			 * of two long, complex, and important queries, so we don't loss
			 * important data.
			 */
		}
	}
    //释放内存
	pfree(nReg);
	pfree(trustworthy);
	pfree(slopeReady);
	pfree(sum_e2);
	pfree(beta0);
	pfree(ssp);
	//cubic
	for (i = 0; i < num_rf; ++i){
		// for(j = 0; j < ncols; ++j){
		// 	pfree(D[i][j]);
		// }
		pfree(D[i]);
	}
	pfree(D);
	for (i = 0; i < num_rf; ++i){
		// for(j = 0; j < ncols; ++j){
		// 	pfree(M[i][j]);
		// }
		pfree(M[i]);
	}
	pfree(M);
	for (i = 0; i < num_rf; ++i){
		// for(j = 0; j < ncols; ++j){
		// 	pfree(alpha[i][j]);
		// }
		pfree(alpha[i]);
	}
	pfree(alpha);
	for (i = 0; i < num_rf; ++i)
		pfree(beta[i]);
	pfree(beta);
	//matric
	for (i = 0; i < num_rf; ++i)
		pfree(c[i]);
	pfree(c);
	for (i = 0; i < num_rf; ++i){
		// for(j = 0; j < ncols; ++j){
		// 	pfree(SXresYres[i][j]);
		// }
		pfree(SXresYres[i]);
	}
	pfree(SXresYres);
	for (i = 0; i < num_rf; ++i)
		pfree(SSs2[i]);
	pfree(SSs2);
	for (i = 0; i < num_rf; ++i)
		pfree(SSYres[i]);
	pfree(SSYres);
	for (i = 0; i < num_rf; ++i){
		// for(j = 0; j < ncols; ++j){
		// 	pfree(SSXres[i][j]);
		// }
		pfree(SSXres[i]);
	}
	pfree(SSXres);
	for (i = 0; i < num_rf; ++i){
		// for(j = 0; j < ncols; ++j){
		// 	pfree(U[i][j]);
		// }
		pfree(U[i]);
	}
	pfree(U);
	for (i = 0; i < num_rf; ++i){
		// for(j = 0; j < ncols; ++j){
		// 	pfree(P[i][j]);
		// }
		pfree(P[i]);
	}
	pfree(P);
	for (i = 0; i < num_rf; ++i)
		pfree(H[i]);
	pfree(H);
	for (i = 0; i < num_rf; ++i)
		pfree(r[i]);
	pfree(r);
	for (i = 0; i < num_rf; ++i)
		pfree(sum_w[i]);
	pfree(sum_w);
	for (i = 0; i < num_rf; ++i)
		pfree(sum_e_cv2[i]);
	pfree(sum_e_cv2);
	for (i = 0; i < num_rf; ++i)
		pfree(n_data[i]);
	pfree(n_data);
	for (i = 0; i < num_rf; ++i)
		pfree(lambda[i]);
	pfree(lambda);
	for (i = 0; i < num_rf; ++i)
		pfree(mean_x[i]);
	pfree(mean_x);
	for (i = 0; i < num_rf; ++i)
		pfree(var_x[i]);
	pfree(var_x);
	for (i = 0; i < num_rf; ++i)
		pfree(s[i]);
	pfree(s);
	for (i = 0; i < num_rf; ++i)
		pfree(slope[i]);
	pfree(slope);

	for (i = 0; i < num_query_pattern; ++i)
		pfree(history_data_matrix[i]);
	pfree(history_data_matrix);
	pfree(num_history_data);

	for (i = 0; i < num_rf; ++i)
		pfree(history_error_matrix[i]);
	pfree(history_error_matrix);
	pfree(num_history_error);

	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	CommandCounterIncrement();

	return true;
}
bool
update_fss_rfwr2(int fss_hash, int ncols, LWPR_Model *model)
{
	RangeVar   *aqo_data_table_rv;
	Relation	aqo_data_heap;
	TupleDesc	tuple_desc;
	HeapTuple	tuple,
				nw_tuple;

	Relation	data_index_rel;
	Oid			data_index_rel_oid;
	IndexScanDesc data_index_scan;
	ScanKeyData	key[2];
	LOCKMODE	lockmode = RowExclusiveLock;
	Datum		values[35];
	bool		isnull[35] = {false, false, false, false, false, 
							  false, false, false, false, false,
							  false, false, false, false, false,
							  false, false, false, false, false,
							  false, false, false, false, false, 
							  false, false, false, false, false,
							  false, false, false, false, false};
	bool		replace[35] = {false, false, false, true, true, 
							  true, true, true, true, true,
							  true, true, true, true, true,
							  true, true, true, true, true,
							  true, true, true, true, true, 
							  true, true, true, true, true,
							  true, true, true, true, true};
	
	/*add by jim in 2021.2.26*/
	double **history_data_matrix;
	double *num_history_data;
    int i;
	//初始化history_data
	history_data_matrix = palloc(sizeof(*history_data_matrix) * num_query_pattern);
	for (i = 0; i < num_query_pattern; ++i)
		history_data_matrix[i] = palloc0(sizeof(**history_data_matrix) * (1 + ncols*num_history_data_compute_probability_rf));
	num_history_data = palloc0(sizeof(*num_history_data) * num_query_pattern);
	//复制
	memcpy(num_history_data, model->num_history_data, num_query_pattern*sizeof(double));
	//将历史数据也写入到数据库
	for (i=0; i< num_query_pattern; i++){
		memcpy(history_data_matrix[i], model->history_data_matrix[i], (1 + ncols*num_history_data_compute_probability_rf)*sizeof(double));
	}
	//写入
	data_index_rel_oid = RelnameGetRelid("aqo_fss_lwpr_access_idx");
	if (!OidIsValid(data_index_rel_oid))
	{
		disable_aqo_for_query();
		return false;
	}

	aqo_data_table_rv = makeRangeVar("public", "aqo_data_lwpr", -1);
	aqo_data_heap = heap_openrv(aqo_data_table_rv, lockmode);

	tuple_desc = RelationGetDescr(aqo_data_heap);

	data_index_rel = index_open(data_index_rel_oid, lockmode);
	data_index_scan = index_beginscan(aqo_data_heap,
									  data_index_rel,
									  SnapshotSelf,
									  2,
									  0);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_context.fspace_hash));

	ScanKeyInit(&key[1],
				2,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(fss_hash));

	index_rescan(data_index_scan, key, 2, NULL, 0);

	tuple = index_getnext(data_index_scan, ForwardScanDirection);

	if (!tuple)
	{
		//三个不能修改的
		return false;
	}
	else
	{
		heap_deform_tuple(tuple, aqo_data_heap->rd_att, values, isnull);
		values[30] = PointerGetDatum(form_matrix(history_data_matrix, num_query_pattern, 1 + ncols*num_history_data_compute_probability_rf));
		values[31] = PointerGetDatum(form_vector(num_history_data, num_query_pattern));
		nw_tuple = heap_modify_tuple(tuple, tuple_desc,
									 values, isnull, replace);
		if (my_simple_heap_update(aqo_data_heap, &(nw_tuple->t_self), nw_tuple))
		{
			my_index_insert(data_index_rel, values, isnull, &(nw_tuple->t_self),
							aqo_data_heap, UNIQUE_CHECK_NO);
		}
		else
		{
			/*
			 * Ooops, somebody concurrently updated the tuple. We have to
			 * merge our changes somehow, but now we just discard ours. We
			 * don't believe in high probability of simultaneously finishing
			 * of two long, complex, and important queries, so we don't loss
			 * important data.
			 */
		}
	}
    //释放内存

	for (i = 0; i < num_query_pattern; ++i)
		pfree(history_data_matrix[i]);
	pfree(history_data_matrix);
	pfree(num_history_data);

	index_endscan(data_index_scan);

	index_close(data_index_rel, lockmode);
	heap_close(aqo_data_heap, lockmode);

	CommandCounterIncrement();

	return true;
}
/*
 * Returns QueryStat for the given query_hash. Returns empty QueryStat if
 * no statistics is stored for the given query_hash in table aqo_query_stat.
 * Returns NULL and executes disable_aqo_for_query if aqo_query_stat
 * is not found.
 */
QueryStat *
get_aqo_stat(int query_hash)
{
	RangeVar   *aqo_stat_table_rv;
	Relation	aqo_stat_heap;
	HeapTuple	tuple;
	LOCKMODE	heap_lock = AccessShareLock;

	Relation	stat_index_rel;
	Oid			stat_index_rel_oid;
	IndexScanDesc stat_index_scan;
	ScanKeyData key;
	LOCKMODE	index_lock = AccessShareLock;

	Datum		values[9];
	bool		nulls[9];

	QueryStat  *stat = palloc_query_stat();

	stat_index_rel_oid = RelnameGetRelid("aqo_query_stat_idx");
	if (!OidIsValid(stat_index_rel_oid))
	{
		disable_aqo_for_query();
		return NULL;
	}

	aqo_stat_table_rv = makeRangeVar("public", "aqo_query_stat", -1);
	aqo_stat_heap = heap_openrv(aqo_stat_table_rv, heap_lock);

	stat_index_rel = index_open(stat_index_rel_oid, index_lock);
	stat_index_scan = index_beginscan(aqo_stat_heap,
									  stat_index_rel,
									  SnapshotSelf,
									  1,
									  0);

	ScanKeyInit(&key,
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_hash));

	index_rescan(stat_index_scan, &key, 1, NULL, 0);

	tuple = index_getnext(stat_index_scan, ForwardScanDirection);

	if (tuple)
	{
		heap_deform_tuple(tuple, aqo_stat_heap->rd_att, values, nulls);

		DeformVectorSz(values[1], stat->execution_time_with_aqo);
		DeformVectorSz(values[2], stat->execution_time_without_aqo);
		DeformVectorSz(values[3], stat->planning_time_with_aqo);
		DeformVectorSz(values[4], stat->planning_time_without_aqo);
		DeformVectorSz(values[5], stat->cardinality_error_with_aqo);
		DeformVectorSz(values[6], stat->cardinality_error_without_aqo);

		stat->executions_with_aqo = DatumGetInt64(values[7]);
		stat->executions_without_aqo = DatumGetInt64(values[8]);
	}

	index_endscan(stat_index_scan);

	index_close(stat_index_rel, index_lock);
	heap_close(aqo_stat_heap, heap_lock);

	return stat;
}

/*
 * Saves given QueryStat for the given query_hash.
 * Executes disable_aqo_for_query if aqo_query_stat is not found.
 */
void
update_aqo_stat(int query_hash, QueryStat *stat)
{
	RangeVar   *aqo_stat_table_rv;
	Relation	aqo_stat_heap;
	HeapTuple	tuple,
				nw_tuple;
	TupleDesc	tuple_desc;

	Relation	stat_index_rel;
	Oid			stat_index_rel_oid;
	IndexScanDesc stat_index_scan;
	ScanKeyData	key;

	LOCKMODE	lockmode = RowExclusiveLock;

	Datum		values[9];
	bool		isnull[9] = { false, false, false,
							  false, false, false,
							  false, false, false };
	bool		replace[9] = { false, true, true,
							    true, true, true,
								true, true, true };

	stat_index_rel_oid = RelnameGetRelid("aqo_query_stat_idx");
	if (!OidIsValid(stat_index_rel_oid))
	{
		disable_aqo_for_query();
		return;
	}

	aqo_stat_table_rv = makeRangeVar("public", "aqo_query_stat", -1);
	aqo_stat_heap = heap_openrv(aqo_stat_table_rv, lockmode);

	tuple_desc = RelationGetDescr(aqo_stat_heap);

	stat_index_rel = index_open(stat_index_rel_oid, lockmode);
	stat_index_scan = index_beginscan(aqo_stat_heap,
									  stat_index_rel,
									  SnapshotSelf,
									  1,
									  0);

	ScanKeyInit(&key,
				1,
				BTEqualStrategyNumber,
				F_INT4EQ,
				Int32GetDatum(query_hash));

	index_rescan(stat_index_scan, &key, 1, NULL, 0);

	tuple = index_getnext(stat_index_scan, ForwardScanDirection);

	values[0] = tuple == NULL ?
					Int32GetDatum(query_hash) :
					heap_getattr(tuple, 1, RelationGetDescr(aqo_stat_heap), &isnull[0]);

	values[1] = PointerGetDatum(FormVectorSz(stat->execution_time_with_aqo));
	values[2] = PointerGetDatum(FormVectorSz(stat->execution_time_without_aqo));
	values[3] = PointerGetDatum(FormVectorSz(stat->planning_time_with_aqo));
	values[4] = PointerGetDatum(FormVectorSz(stat->planning_time_without_aqo));
	values[5] = PointerGetDatum(FormVectorSz(stat->cardinality_error_with_aqo));
	values[6] = PointerGetDatum(FormVectorSz(stat->cardinality_error_without_aqo));

	values[7] = Int64GetDatum(stat->executions_with_aqo);
	values[8] = Int64GetDatum(stat->executions_without_aqo);

	if (!tuple)
	{
		tuple = heap_form_tuple(tuple_desc, values, isnull);
		PG_TRY();
		{
			simple_heap_insert(aqo_stat_heap, tuple);
			my_index_insert(stat_index_rel, values, isnull, &(tuple->t_self),
							aqo_stat_heap, UNIQUE_CHECK_YES);
		}
		PG_CATCH();
		{
			CommandCounterIncrement();
			simple_heap_delete(aqo_stat_heap, &(tuple->t_self));
			PG_RE_THROW();
		}
		PG_END_TRY();
	}
	else
	{
		nw_tuple = heap_modify_tuple(tuple, tuple_desc,
									 values, isnull, replace);
		if (my_simple_heap_update(aqo_stat_heap, &(nw_tuple->t_self), nw_tuple))
		{
			/* NOTE: insert index tuple iff heap update succeeded! */
			my_index_insert(stat_index_rel, values, isnull, &(nw_tuple->t_self),
							aqo_stat_heap, UNIQUE_CHECK_YES);
		}
		else
		{
			/*
			 * Ooops, somebody concurrently updated the tuple. We have to
			 * merge our changes somehow, but now we just discard ours. We
			 * don't believe in high probability of simultaneously finishing
			 * of two long, complex, and important queries, so we don't loss
			 * important data.
			 */
		}
	}

	index_endscan(stat_index_scan);

	index_close(stat_index_rel, lockmode);
	heap_close(aqo_stat_heap, lockmode);

	CommandCounterIncrement();
}

/*
 * Expands matrix from storage into simple C-array.
 */
/* void 
deform_cubic(Datum datum, double ***cubic)
{
	ArrayType  *array = DatumGetArrayTypePCopy(PG_DETOAST_DATUM(datum));
	int			nelems;
	Datum	   *values;
	int			rows;
	int			cols;
	int         num_rf;  //有效域的个数
	int			i,
				j,
				k;

	deconstruct_array(array,
					  FLOAT8OID, 8, FLOAT8PASSBYVAL, 'd',
					  &values, NULL, &nelems);
	if (nelems != 0)
	{
		num_rf = ARR_DIMS(array)[0];
		rows = ARR_DIMS(array)[1];
		cols = ARR_DIMS(array)[2];
		for (k = 0; k < num_rf; ++k)
			for (i = 0; i < rows; ++i)
				for (j = 0; j < cols; ++j)
					cubic[k][i][j] = DatumGetFloat8(values[k * rows * cols + i * cols + j]);
	}
	pfree(values);
	pfree(array);
}*/
void
deform_matrix(Datum datum, double **matrix)
{
	ArrayType  *array = DatumGetArrayTypePCopy(PG_DETOAST_DATUM(datum));
	int			nelems;
	Datum	   *values;
	int			rows;
	int			cols;
	int			i,
				j;

	deconstruct_array(array,
					  FLOAT8OID, 8, FLOAT8PASSBYVAL, 'd',
					  &values, NULL, &nelems);
	if (nelems != 0)
	{
		rows = ARR_DIMS(array)[0];
		cols = ARR_DIMS(array)[1];
		for (i = 0; i < rows; ++i)
			for (j = 0; j < cols; ++j)
				matrix[i][j] = DatumGetFloat8(values[i * cols + j]);
	}
	pfree(values);
	pfree(array);
}

/*
 * Expands vector from storage into simple C-array.
 * Also returns its number of elements.
 */
void
deform_vector(Datum datum, double *vector, int *nelems)
{
	ArrayType  *array = DatumGetArrayTypePCopy(PG_DETOAST_DATUM(datum));
	Datum	   *values;
	int			i;

	deconstruct_array(array,
					  FLOAT8OID, 8, FLOAT8PASSBYVAL, 'd',
					  &values, NULL, nelems);
	for (i = 0; i < *nelems; ++i)
		vector[i] = DatumGetFloat8(values[i]);
	pfree(values);
	pfree(array);
}

/*
 * Forms ArrayType object for storage from simple C-array matrix.
 */
/*ArrayType *
form_cubic(double ***cubic, int nrows, int ncols, int num_rf)
{
    Datum	   *elems;
	ArrayType  *array;
	int			dims[3];
	int			lbs[3];
	int			i,
				j,
				k;
    dims[0] = num_rf;
	dims[1] = nrows;
	dims[2] = ncols;
	lbs[0] = lbs[1] = lbs[2] = 1;
	elems = palloc(sizeof(*elems) * nrows * ncols * num_rf);
	for(k = 0; k<num_rf; k++)
		for (i = 0; i < nrows; ++i)
			for (j = 0; j < ncols; ++j)
				elems[k * nrows * ncols +i * ncols + j] = Float8GetDatum(cubic[k][i][j]);

	array = construct_md_array(elems, NULL, 3, dims, lbs,
							   FLOAT8OID, 8, FLOAT8PASSBYVAL, 'd');
	pfree(elems);
	return array;
}
 */
ArrayType *
form_matrix(double **matrix, int nrows, int ncols)
{
	Datum	   *elems;
	ArrayType  *array;
	int			dims[2];
	int			lbs[2];
	int			i,
				j;

	dims[0] = nrows;
	dims[1] = ncols;
	lbs[0] = lbs[1] = 1;
	elems = palloc(sizeof(*elems) * nrows * ncols);
	for (i = 0; i < nrows; ++i)
		for (j = 0; j < ncols; ++j)
			elems[i * ncols + j] = Float8GetDatum(matrix[i][j]);

	array = construct_md_array(elems, NULL, 2, dims, lbs,
							   FLOAT8OID, 8, FLOAT8PASSBYVAL, 'd');
	pfree(elems);
	return array;
}

/*
 * Forms ArrayType object for storage from simple C-array vector.
 */
ArrayType *
form_vector(double *vector, int nrows)
{
	Datum	   *elems;
	ArrayType  *array;
	int			dims[1];
	int			lbs[1];
	int			i;

	dims[0] = nrows;
	lbs[0] = 1;
	elems = palloc(sizeof(*elems) * nrows);
	for (i = 0; i < nrows; ++i)
		elems[i] = Float8GetDatum(vector[i]);
	array = construct_md_array(elems, NULL, 1, dims, lbs,
							   FLOAT8OID, 8, FLOAT8PASSBYVAL, 'd');
	pfree(elems);
	return array;
}

/*
 * Form ArrayType object for storage from simple C-array vector.
 */
// ArrayType *
// strlist_to_textarray_two(List *list)
// {
// 	ArrayType  *arr;
// 	Datum	   *datums;
// 	int			j = 0;
// 	ListCell   *cell;
// 	MemoryContext memcxt;
// 	MemoryContext oldcxt;

// 	memcxt = AllocSetContextCreate(CurrentMemoryContext,
// 								   "strlist to array",
// 								   ALLOCSET_DEFAULT_SIZES);
// 	oldcxt = MemoryContextSwitchTo(memcxt);

// 	datums = palloc(sizeof(text *) * list_length(list));
// 	foreach(cell, list)
// 	{
// 		char	   *name = lfirst(cell);

// 		datums[j++] = CStringGetTextDatum(name);
// 	}

// 	MemoryContextSwitchTo(oldcxt);

// 	arr = construct_array(datums, list_length(list),
// 						  TEXTOID, -1, false, 'i');
// 	MemoryContextDelete(memcxt);

// 	return arr;
// }

/*
 * Returns true if updated successfully, false if updated concurrently by
 * another session, error otherwise.
 */
static bool
my_simple_heap_update(Relation relation, ItemPointer otid, HeapTuple tup)
{
	HTSU_Result result;
	HeapUpdateFailureData hufd;
	LockTupleMode lockmode;

	result = heap_update(relation, otid, tup,
						 GetCurrentCommandId(true), InvalidSnapshot,
						 true /* wait for commit */ ,
						 &hufd, &lockmode);
	switch (result)
	{
		case HeapTupleSelfUpdated:
			/* Tuple was already updated in current command? */
			elog(ERROR, "tuple already updated by self");
			break;

		case HeapTupleMayBeUpdated:
			/* done successfully */
			return true;
			break;

		case HeapTupleUpdated:
			return false;
			break;

		case HeapTupleBeingUpdated:
			return false;
			break;

		default:
			elog(ERROR, "unrecognized heap_update status: %u", result);
			break;
	}
	return false;
}


/* Provides correct insert in both PostgreQL 9.6.X and 10.X.X */
static bool
my_index_insert(Relation indexRelation,
				Datum *values, bool *isnull,
				ItemPointer heap_t_ctid,
				Relation heapRelation,
				IndexUniqueCheck checkUnique)
{
	/* Index must be UNIQUE to support uniqueness checks */
	Assert(checkUnique == UNIQUE_CHECK_NO ||
		   indexRelation->rd_index->indisunique);

#if PG_VERSION_NUM < 100000
	return index_insert(indexRelation, values, isnull, heap_t_ctid,
						heapRelation, checkUnique);
#else
	return index_insert(indexRelation, values, isnull, heap_t_ctid,
						heapRelation, checkUnique,
						BuildIndexInfo(indexRelation));
#endif
}

/* Creates a storage for hashes of deactivated queries */
void
init_deactivated_queries_storage(void)
{
	HASHCTL		hash_ctl;

	/* Create the hashtable proper */
	MemSet(&hash_ctl, 0, sizeof(hash_ctl));
	hash_ctl.keysize = sizeof(int);
	hash_ctl.entrysize = sizeof(int);
	deactivated_queries = hash_create("aqo_deactivated_queries",
									  128,		/* start small and extend */
									  &hash_ctl,
									  HASH_ELEM | HASH_BLOBS);
}

/* Destroys the storage for hash of deactivated queries */
void
fini_deactivated_queries_storage(void)
{
	hash_destroy(deactivated_queries);
	deactivated_queries = NULL;
}

/* Checks whether the query with given hash is deactivated */
bool
query_is_deactivated(int query_hash)
{
	bool		found;

	hash_search(deactivated_queries, &query_hash, HASH_FIND, &found);
	return found;
}

/* Adds given query hash into the set of hashes of deactivated queries*/
void
add_deactivated_query(int query_hash)
{
	hash_search(deactivated_queries, &query_hash, HASH_ENTER, NULL);
}
