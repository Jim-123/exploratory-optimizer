# -*- coding: utf-8 -*-
import psycopg2
import argparse
import datetime
import pandas as pd
import config_file as cf

def update_markov_table(history_num, markov_m, query_pattern, cursor, conn, num_queries, hist_queries):
    if num_queries < history_num+1:
        hist_queries.append(query_pattern)
    else:
        sql_aqo_disabled = 'set aqo.mode=''disabled'';'
        cursor.execute(sql_aqo_disabled)
        hist_queries_handled = 'array[' + ','.join(map(str, hist_queries)) + ']'
        sql_other = 'select imdb_get_queries_distribution_k(' + str(cf.num_query_template) + ',' + str(history_num) + ',' + str(markov_m) + ',' + hist_queries_handled + ');'
        cursor.execute(sql_other)
        conn.commit()
        for hist_iter in range(history_num - 1):
            hist_queries[hist_iter] = hist_queries[hist_iter + 1]
            hist_queries[history_num-1] = query_pattern
def update_aqo_query_table(current_query, num_feature, cursor, conn):
    current_query_list = []
    frist_value = current_query.split(',')[0].split('{')[-1]
    second_value = current_query.split(',')[-1].split('}')[0]
    current_query_list.append(frist_value)
    current_query_list.append(second_value)
    current_query_list = 'array[' + ','.join(map(str, current_query_list)) + ']'
    print(current_query)
    sql_other = 'update aqo_queries set current_query = ' + current_query_list + ' , num_feature = ' + str(num_feature) + ' where query_hash = 1;'
    print(sql_other)
    cursor.execute(sql_other)
    conn.commit()

def execute_current_query(query_id, query_mode, cursor, conn, query_table, history_num, markov_m, num_queries, hist_queries):
    sql_aqo_disabled = 'set aqo.mode=''disabled'';'
    cursor.execute(sql_aqo_disabled)
    if query_mode == 1:
        sql_aqo = 'set aqo.mode=''learn'';'
    else:
        sql_aqo = 'set aqo.mode=''disabled'';'
    sql_text_query = query_table['queries'][query_id-1]
    query_pattern = query_table['query_hash'][query_id-1]
    current_query = query_table['current_query'][query_id-1]
    num_feature = query_table['num_feature'][query_id-1]
    if query_mode > 0:
        update_markov_table(history_num, markov_m, query_pattern, cursor, conn, num_queries, hist_queries)
    update_aqo_query_table(current_query, num_feature, cursor, conn)
    stat_time = datetime.datetime.now()
    cursor.execute(sql_aqo)
    cursor.execute(sql_text_query)
    cursor.execute(sql_aqo_disabled)
    end_time = datetime.datetime.now()
    dure_time = (end_time-stat_time).total_seconds()*1000   # ms
    sql = 'insert into ' + cf.run_result_name + ' values( ' + str(query_id) + ',' \
          + "'" + str(stat_time) + "'" + ',' + "'" + str(end_time) + "'" + ',' + str(dure_time) + ');'
    cursor.execute(sql)
    conn.commit()







def run_workloads_runtime_experiments(delete_old_data, query_mode, begin_num, end_num, history_num, markov_m, hist_queries):
    print('db = ', cf.db_port)
    conn = psycopg2.connect(database=cf.db_database, user=cf.db_user_name, password=cf.db_passwd, host=cf.db_host, port=cf.db_port)
    cursor = conn.cursor()
    if query_mode > 0 and delete_old_data == 1:
        print("initialization of aqo")
        sql = "drop extension aqo; create extension aqo;"
        cursor.execute(sql)
        conn.commit()
    if delete_old_data == 1:
        print("delete table " + cf.run_result_name)
        sql = "delete from " + cf.run_result_name
        cursor.execute(sql)
        conn.commit()
    totalnum = end_num - begin_num + 1
    query_id = begin_num - 1
    num_queries = 0
    query_file = "./data/workloads/" + cf.query_file_name
    print('query_mode = ', query_mode)
    print('query_file = ', query_file)
    query_table = pd.read_csv(query_file, sep=',', escapechar='\\',
                              encoding='utf-8', low_memory=False, quotechar='"')
    while totalnum != 0:
        query_id = query_id + 1
        num_queries = num_queries + 1
        print("query_id = ", query_id)
        totalnum = totalnum - 1
        execute_current_query(query_id, query_mode, cursor, conn, query_table, history_num, markov_m, num_queries, hist_queries)
    print("close the connection of DB")
    conn.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--delete-old-data', type=int, default='1', help='delete old data')
    parser.add_argument('--query-mode', type=int, default='1', help='query mode')   # 0 postgresql optimizer
    parser.add_argument('--begin-num', type=int, default='1', help='begin num')
    parser.add_argument('--end-num', type=int, default='4000', help='end num')
    parser.add_argument('--history-num', type=int, default='7', help='historic data')
    parser.add_argument('--markov-m', type=int, default='3', help='the order of markov')

    args = parser.parse_args()
    delete_old_data = args.delete_old_data
    query_mode = args.query_mode
    begin_num = args.begin_num
    end_num = args.end_num
    history_num = args.history_num
    markov_m = args.markov_m
    hist_queries = []
    run_workloads_runtime_experiments(delete_old_data, query_mode, begin_num, end_num, history_num, markov_m, hist_queries)