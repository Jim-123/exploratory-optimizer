CREATE OR REPLACE FUNCTION "public"."imdb_get_queries_distribution_k"("pattern_num" int4, "hist_num" int4, "markov_m" int4, "hist_queries" _float8)
  RETURNS "pg_catalog"."int4" AS $BODY$
	declare
	   sql_insert_query   text;
	   sql_update_query   text;
	   sql_other          text;
	   hash_num           int;
	   query_pattern      int;
	   distribution_array double precision[];
	   frequency_array    double precision[];
	   current_queries    double precision[];
	   ii                 int;
	   ii2                int;
	   ii3                int;
	   current_hash       int;
	   current_sum_freq   int;
	begin
	   --initial 
	   sql_insert_query:='';
	   sql_update_query:='';
	   sql_other:='';
	   hash_num=0;
	   current_hash=0;
	   current_sum_freq=0;
	   query_pattern=0;
	   --初始化数组
	   distribution_array :=array_fill(0::double precision, array[pattern_num]);
	   frequency_array :=array_fill(0::double precision, array[pattern_num]);
	   current_queries := array_fill(0::double precision, array[markov_m]);
	   --get current markov state
	   FOR ii IN 1..markov_m LOOP 
		  current_queries[ii] = hist_queries[ii];
	   end loop;
	   --get hash of state
	  sql_other='select imdb_get_array_hash(array['
	  ||array_to_string(current_queries,',')
	  ||'],'
	  ||markov_m
	  ||')';
	  execute 	sql_other into current_hash;
	  sql_other ='select count(*) from aqo_markov_table where hist_query_hash= '||current_hash;
	  execute 	sql_other into hash_num;
	  if hash_num > 0 then
	      sql_other ='select frequent_query from aqo_markov_table where hist_query_hash= '||current_hash;
	      execute 	sql_other into frequency_array;
	  end if;
	  for ii2 in (markov_m+1)..hist_num loop
		  --get next query_pattern
		  query_pattern = hist_queries[ii2];
		  frequency_array[query_pattern] = frequency_array[query_pattern]+1;
	  end loop;
	  --calculate the distribution_query according to frequent_query
	  FOR ii3 IN 1..pattern_num LOOP
		 current_sum_freq =  current_sum_freq + frequency_array[ii3];
	  end loop;
	  --calculate probability
	  IF current_sum_freq !=0 then
		  FOR ii3 IN 1..pattern_num LOOP
			 distribution_array[ii3] = frequency_array[ii3]/current_sum_freq;
		  end loop;
	  end if;
	  --insert or update value（aqo_markov_table）
	  if hash_num = 0 then
		  sql_insert_query='insert into aqo_markov_table values( '
			|| current_hash
			||','
			||'array['
			|| array_to_string(distribution_array,',') 
			|| ']'
			||','
			||'array['
			|| array_to_string(frequency_array,',') 
			|| ']);';
		  execute sql_insert_query;
	  else
	      sql_update_query='update aqo_markov_table set '
			||' distribution_query = '
			||'array['
			|| array_to_string(distribution_array,',') 
			|| ']'
			||','
			||' frequent_query= '
			||'array['
			|| array_to_string(frequency_array,',') 
			|| '] '
			||'where hist_query_hash = '
			||current_hash;
		  execute sql_update_query;
	  end if;
	  
	   return 0;
	end;
	$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100