import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

def query_finish_time3(query_num, interval):
    ## imdb 7
    file = "./data/imdb-7-uniform.csv"
    file = "./data/imdb-7-possion.csv"
    file = "./data/imdb-7-randwalk.csv"
    file = "./data/imdb-7-bao.csv"
    result_file = pd.read_csv(file, sep=',', escapechar='\\',
                encoding='utf-8', low_memory=False, quotechar='"')
    queries_time0 = []
    queries_time1 = []
    queries_time2 = []
    queries_time3 = []
    num_queries = []
    for i in range(0, query_num):
        # time = time + int(new_df[i].split(',')[-1])
        # time1 = time1 + int(new_df1[i].split(',')[-1])
        time0 = np.sum(result_file['origin'][0:interval*(i)])
        time1 = np.sum(result_file['aqo'][0:interval*(i)])
        time2 = np.sum(result_file['learn'][0:interval * (i)])
        time3 = np.sum(result_file['cardlearner3'][0:interval*(i)])
        # reduce to s
        time0 = np.divide(time0, 1000*3600)
        time1 = np.divide(time1, 1000*3600)
        time2 = np.divide(time2, 1000*3600)
        time3 = np.divide(time3, 1000*3600)
        queries_time0.append(time0)
        queries_time1.append(time1)
        queries_time2.append(time2)
        queries_time3.append(time3)
        num_queries.append([(i)*interval])
    # 查询完成的时间
    # print(num_queries)
    plt.figure()
    plt.xlabel('The number of queries executed')
    plt.ylabel('Running time (h)')
    num_initial = 0
    plt.plot(num_queries[num_initial:], queries_time0[num_initial:],
             label='PDO')
    plt.plot(num_queries[num_initial:], queries_time1[num_initial:],
             label='CLO')
    plt.plot(num_queries[num_initial:], queries_time3[num_initial:],
             label='CO')
    plt.plot(num_queries[num_initial:], queries_time2[num_initial:],
             label='EQO')
    plt.ylim([0, 8])
    plt.legend(loc=4)
    plt.show()

if __name__ == '__main__':
    query_finish_time3(4000, 1)
