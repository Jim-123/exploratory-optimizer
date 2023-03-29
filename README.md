# exploratory optimizer


### Development
    This is the code for the exploratory optimizer: "Towards Exploratory Query Optimization for Template-based SQL" paper. We implement it in the form of a PostgreSQL plug-in. The code is developed on the basis of project “AQO” which can be found in “https://github.com/postgrespro/aqo”.

### Requirements
- Python 3.6
- Postgresql-10

### Install the PostgreSQL
```sh
cd postgresql-10.0-with-exploratory-optimizer
bash ./configure --prefix=/opt/PostgreSQL-10/ --without-readline --enable-debug CFLAGS='-O0 -g'
make clean && make && make install 
cd contrib/aqo                                               
make clean && make && make install 
```
Restart your database server
### Install the data and sql functions
## install data
Database like imdb and tpcds which can be found in www, omit here.
## execute sql and install sql functions
1. create extension aqo;
2. Run two files under the fold of sql functions.
### Running experiments 
Under the folder of run_experiments
1. configurate the config_file.py
2. modify the function aqo_planner to configurate the query templates of current workload, for example, 7 templates in imdb like that
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
 Note: you can use select pg_backend_pid and gdb tools to get the hash value (query_context.current_query_hash = get_query_hash(parse, query_text)) of each query template. If you not do that, database will use origin optimizer for that query. Now, we implement in this stupid way , in the future, we will update it. 
3. run
    ```sh
     python run_workloads_runtime_experiments.py --delete-old-data=1  --query-mode=2  --begin-num=1  --end-num=4000  ----history-num=7 --markov-m=3
    ```

