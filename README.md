# exploratory optimizer


### Development
    This is the code for the exploratory optimizer: "Towards an Exploratory Query Optimizer for Template-based SQL" paper. We implement it in the form of a PostgreSQL plug-in. The code is developed on the basis of project “AQO” which can be found in “https://github.com/postgrespro/aqo”.

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
Database like imdb and tpch which can be found in www, omit here.
## install sql functions and execute sql
   Run two files under the fold of sql functions.
   create extension aqo;
### Running experiments 
Under the folder of run_experiments
1. configurate the config_file.py
2. run
    ```sh
     python run_workloads_runtime_experiments.py --delete-old-data=1  --query-mode=2  --begin-num=1  --end-num=4000  ----history-num=7 --markov-m=3
    ```

