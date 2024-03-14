#include "aqo.h"

/*****************************************************************************
 *
 *	MACHINE LEARNING TECHNIQUES(LWPR)
 *  Written by jim 2020.4.2 
 *
 *****************************************************************************/
/*初始化mode */
int lwpr_init_model(LWPR_Model *model, int nIn, int nOut) {

   int i,nInS;
   //给model分配空间 question a）: lwpr_mem_alloc_model实现
   if (0==lwpr_mem_alloc_model(model,nIn,64)) {
      model->nIn = 0;
      return 0;
   }
   //初始化相关变量
   nInS = model->nInStore;
   //正则化处理
   for (i=0;i<nIn;i++) model->norm_in[i] = 1.0;
   model->norm_out = 1.0;
   model->n_data = 0;
   //超参数初始化
   model->diag_only = 1;
   model->penalty = 1e-6;
 
   for (i=0;i<nIn;i++) {
      //有效区域
      model->init_D[i+i*nInS] = 25.0;
      model->init_M[i+i*nInS] = 5.0;
      //学习率
      model->init_alpha[i+i*nInS] = 0.05;
   }
   model->w_gen = 0.1;
   model->w_prune = 0.9;
   model->init_lambda = 1;
   model->final_lambda = 1;
   model->tau_lambda = 1;
   model->init_S2 = 1e-10;
   model->add_threshold = 0.5;
   model->kernel = LWPR_GAUSSIAN_KERNEL;
   model->update_D = 1;
   model->fss_hash = 0;
   return 1;
}

/*
    模型预测
 */
double lwpr_predict(LWPR_Model *model, const double *x, double cutoff) {
   int i;
   //out
   double y;
   LWPR_ThreadData TD; 

   //正则化输入
   for (i=0;i<model->nIn;i++) model->xn[i]=x[i]/model->norm_in[i];
   //初始化TD
   TD.model = model;
   TD.xn = model->xn;
   // question?
   TD.ws = model->ws;
   TD.cutoff = cutoff; 
   //calculate the result
   lwpr_aux_predict_one_T(&TD);
	y = TD.yn;
   // 正则化输出
   y = y*model->norm_out;
   return y;
}

/*
    模型预测+explore value
 */
void lwpr_predict_explore(LWPR_Model *model, const double *x, double cutoff, Explore_Value *explore_values) {
   int i;
   //out
   LWPR_ThreadData TD;
   double y; 
   //正则化输入
   for (i=0;i<model->nIn;i++) model->xn[i]=x[i]/model->norm_in[i];
   //初始化TD
   TD.model = model;
   TD.xn = model->xn;
   // question?
   TD.ws = model->ws;
   TD.cutoff = cutoff; 
   //calculate the result
   lwpr_aux_predict_conf_one_T(&TD);
   //get explore value
   explore_values->est_future = model->explore_values->est_future;
   explore_values->est_uncof = model->explore_values->est_uncof;
   explore_values->rate = model->explore_values->rate;
   // 正则化输出
   y = TD.yn;
   y = y*model->norm_out;
   explore_values->rows=y;
}

/*
 * 训练模型
 */
int lwpr_update(LWPR_Model *model, const double *x, const double y) {
   //printf("%s\n", "I am in lwpr_update now!");
   int i,code=0;
   model->n_data += 1;
   // 对输入输出进行正则化处理
   for (i=0;i<model->nIn;i++) model->xn[i]=x[i]/model->norm_in[i];
   model->yn = y/ model->norm_out;   
   //对模型进行更新
   code = lwpr_aux_update_one(model, model->xn, model->yn);   
   return code;
}

/*
*  some useful functions
*/
void lwpr_aux_predict_one_T(void *ptr) {
   //得到当前的线程和工作空间  
   LWPR_ThreadData *TD = (LWPR_ThreadData *) ptr;
   LWPR_Workspace *WS = TD->ws;

   int i,j,n;
   int nIn=TD->model->nIn;
   int nInS=TD->model->nInStore;
   
   double *xc = WS->xc;
   double *s = WS->s;
   
   double w = 0.0;
   
   double yp = 0.0;
   
   double sum_w = 0.0;
   int idx_max = 0;
   TD->w_max = 0.0;
   
   

   for (n=0;n<TD->model->numRFS;n++) {
      
      double dist = 0.0;
      LWPR_ReceptiveField *RF = TD->model->rf[n];
      //读取rf数据相关变量
      double      *subspace_pattern = RF->c;
      double	  **matrix;
      double	   *targets;
      double	   *features = TD->xn;
      int         fss_hash = TD->model->fss_hash;
      int         rf_hash;
      int			rows;
      int			k;
      for (i=0;i<nIn;i++) {
         xc[i] = TD->xn[i] - RF->c[i];
      }

      for (j=0;j<nIn;j++) {
         dist += xc[j] * lwpr_math_dot_product(RF->D + j*nInS, xc, nIn);
      }

      switch(TD->model->kernel) {
         case LWPR_GAUSSIAN_KERNEL:
            w = exp(-0.5*dist);
            break;
         case LWPR_BISQUARE_KERNEL:
            w = 1-0.25*dist;
            w = (w<0) ? 0 : w*w;
            break;
      }

      if (w > TD->w_max) {
         TD->w_max = w;
         idx_max = n;
      }

      if (w > TD->cutoff) {
         double yp_n = RF->beta0;
         //中心化数据
         for (i=0;i<nIn;i++) {
            xc[i] = TD->xn[i] - RF->mean_x[i];
         }      
         
         if (RF->slopeReady) {   
            yp_n += lwpr_math_dot_product(xc, RF->slope, nIn);
         } else {
            // 当rf可信时，使用PLS进行计算
            if(RF->trustworthy){
               int nR = RF->nReg;
               if (RF->n_data[nR-1] <= 2*nIn) nR--;         
               lwpr_aux_compute_projection(nIn, nInS, nR, s, xc, RF->U, RF->P, WS);
               for (i=0;i<nR;i++) {yp_n+=s[i]*RF->beta[i];}
            }else{
               //否则使用knn进行计算
               //1.初始化矩阵
               matrix = palloc(sizeof(*matrix) *(2.0*nIn+1));
               for (k = 0; k < (2.0*nIn+1); ++k){
                  matrix[k] = palloc0(sizeof(**matrix) * nIn);
               }
               targets = palloc0(sizeof(*targets) * (2.0*nIn+1));
               //计算当前rf_hash
               rf_hash = get_int_array_hash2(subspace_pattern, nIn);
               //读取RF数据
               if(load_rf_datahouse(fss_hash, rf_hash, matrix, targets, &rows)) {
                  yp_n = OkNNr_predict(rows,  nIn, matrix, targets, features, 2);
               }
               //释放内存
               for (k = 0; k < (2.0*nIn+1); ++k)
                  pfree(matrix[k]);
               pfree(matrix);
               pfree(targets);
            } 
         }

         yp += w*yp_n;
         sum_w += w;
      }
   }
   // 求最终的值
   if (sum_w > 0.0) {
      yp/=sum_w;
   }else{
      /* 从rf数据中提取 */
      LWPR_ReceptiveField *RF = TD->model->rf[idx_max];
      //读取rf数据相关变量
      double      *subspace_pattern = RF->c;
      double	  **matrix;
      double	   *targets;
      double	   *features = TD->xn;
      int         fss_hash = TD->model->fss_hash;
      int         rf_hash;
      int			rows;
      int			k;
      // 当rf可信时，使用PLS进行计算
      if(RF->trustworthy){
         yp = RF->beta0;
         int nR = RF->nReg;
         if (RF->n_data[nR-1] <= 2*nIn) nR--;         
         lwpr_aux_compute_projection(nIn, nInS, nR, s, xc, RF->U, RF->P, WS);
         for (i=0;i<nR;i++) {yp+=s[i]*RF->beta[i];}
      }else{
         //否则使用knn进行计算
         //1.初始化矩阵
         matrix = palloc(sizeof(*matrix) *(2.0*nIn+1));
         for (k = 0; k < (2.0*nIn+1); ++k){
            matrix[k] = palloc0(sizeof(**matrix) * nIn);
         }
         targets = palloc0(sizeof(*targets) * (2.0*nIn+1));
         //计算当前rf_hash
         rf_hash = get_int_array_hash2(subspace_pattern, nIn);
         //读取RF数据
         if(load_rf_datahouse(fss_hash, rf_hash, matrix, targets, &rows)) {
            yp = OkNNr_predict(rows,  nIn, matrix, targets, features, 2);
         }
         //释放内存
         for (k = 0; k < (2.0*nIn+1); ++k)
            pfree(matrix[k]);
         pfree(matrix);
         pfree(targets);
      }
   }
   
   TD->yn = yp;
}
/**
 * calculate explore value(est_uncof, est_future)
 */
void  lwpr_aux_predict_conf_one_T(void *ptr) {
   LWPR_ThreadData *TD = (LWPR_ThreadData *) ptr;
   LWPR_Workspace *WS = TD->ws;
   /*get the lwpr model */
   LWPR_Model *model = TD->model;

   /* define explore value and initialization */
   Explore_Value *explore_value = TD->model->explore_values;
   lwpr_men_alloc_ev(explore_value);

   //get total happen weight(frequent) of all receptive field
   double *total_happen_freq;
   total_happen_freq = palloc0(sizeof(*total_happen_freq) * num_query_pattern);
   //get history data
   double **history_data_matrix = model->history_data_matrix;
   //get the num of history data, modified by jim
   double *num_history_data = model->num_history_data;
   int current_data_matric_inx = 1;
   int current_data_num = 1;
   //define the future_value for every query pattern
   double *future_value_pattern;
   future_value_pattern = palloc0(sizeof(*future_value_pattern) * num_query_pattern);
   int i,j,n,m,k,k2,l;
   int nIn=TD->model->nIn;
   int nInS=TD->model->nInStore;
   
   double *xc = WS->xc;
   double *s = WS->s;
   // 用来做预测(new2020829)
   double yp = 0.0;
   double sum_w2 = 0.0;
   int idx_max = 0;
   // 用来处理一个查询距离某个RF特别近的情况(2020919)
   int exact_flag = 0;
   double exact_y = 0;

   double w;
   double sum_w = 0.0;
   double sum_wyy = 0.0;
   double sum_conf = 0.0;
   double sum_all = 0.0;
   TD->w_max = 0.0;
   TD->yn = 0.0;

   //try est_future=1 when it is a outiler,20201229,modified by jim
   int count_rfs = 0;

   //min_error used for deciding whether to use origin estimation, modified by jim in 2021.1.23
   double min_error = 9999;
   
   //current query_distribution
   double current_query_distribution = 0;
   // calculate the happen frequency(probability) by using the history data. Maybe someday we use more suitable method;
   // modified by jim 2021.2.16, calculate the RF frequency for every query pattern, used by calculating the explore_value for every query pattern
   for(n=0;n<TD->model->numRFS;n++){
      //对每一个感受野进行处理
      LWPR_ReceptiveField *RF = TD->model->rf[n];
      for(m=0;m<num_query_pattern;m++){
         current_query_distribution = query_context.query_distribution[m];
         if(current_query_distribution == 0){
            RF->prob_rf[m] = 0;
            total_happen_freq[m] = 0;
         }else
         {
            /* code */
            RF->prob_rf[m] = 0;
            for(k=0;k<num_history_data[m];k++){
               double dist = 0.0;
               double *current_xn = palloc0(sizeof(*current_xn) * nIn);
               for(k2=0;k2<nIn;k2++){
                  current_xn[k2] = history_data_matrix[m][k*nIn+1+k2];
               }
               for (i=0;i<nIn;i++) {
                  xc[i] = current_xn[i] - RF->c[i];
               }
               for (j=0;j<nIn;j++) {
                  dist += xc[j] * lwpr_math_dot_product(RF->D + j*nInS, xc, nIn);
               }

               switch(TD->model->kernel) {
                  case LWPR_GAUSSIAN_KERNEL:
                     w = exp(-0.5*dist);
                     break;
                  case LWPR_BISQUARE_KERNEL:
                     w = 1-0.25*dist;
                     w = (w<0) ? 0 : w*w;
                     break;
               }
               /*判断w是否超过TD->cutoff，当超过时，我们应该考虑该RF的影响，反之，可认为该RF的影响几乎为0*/
               //modified by jim 2021.2.26
               if(w > TD->cutoff){
                  RF->prob_rf[m] += w;
               }else{
                  RF->prob_rf[m] += 0;
               }
            }
            total_happen_freq[m] += RF->prob_rf[m];
         }      
      }//num_pattern
   //rf
   }
   //modified by jim 2021.2.26，将其从update的部分移入到predict部分，这里还需要保存
   //更新历史数据矩阵，没有必要使用for语句
   /*判断是否跟当前查询相关，如果相关，则进行处理，否则跳过,只需处理我们规定的查询模板*/
   if (query_context.current_query_hash!=0){
      current_data_matric_inx = query_context.current_query_hash -1;
      current_data_num = (int) num_history_data[current_data_matric_inx];
      if(current_data_num<num_history_data_compute_probability_rf){
         /* 只需添加该数据 */
         for(i=current_data_num*nIn+1; i<(current_data_num+1)*nIn+1; i++){
            history_data_matrix[current_data_matric_inx][i] = TD->xn[i-current_data_num*nIn-1];
         }
         current_data_num += 1;
         num_history_data[current_data_matric_inx] = (double) current_data_num;
      }else
      {
         /* 将最之前的一个数据进行删除*/
         for(i=1; i<num_history_data_compute_probability_rf; i++){
            for(j=0; j<nIn; j++){
               history_data_matrix[current_data_matric_inx][(i-1)*nIn+1+j] = history_data_matrix[current_data_matric_inx][i*nIn+1+j];
            }
         }
         /*将当前数据加入到矩阵最末尾 */
         for(i=0; i<nIn; i++){
            history_data_matrix[current_data_matric_inx][(num_history_data_compute_probability_rf-1)*nIn+1+i] = TD->xn[i];
         }
         //num_history_data保持不变
      }
   }

   /* Prediction and confidence bounds in one go */
   for (n=0;n<TD->model->numRFS;n++) {
      double dist = 0.0;
      LWPR_ReceptiveField *RF = TD->model->rf[n];

      //读取rf数据相关变量
      double      *subspace_pattern = RF->c;
      double	   *features = TD->xn;
      int         fss_hash = TD->model->fss_hash;
      int         rf_hash;
      int			rows;
      int			k;

      for (i=0;i<nIn;i++) {
         xc[i] = TD->xn[i] - RF->c[i];
      }

      for (j=0;j<nIn;j++) {
         dist += xc[j] * lwpr_math_dot_product(RF->D + j*nInS, xc, nIn);
      }

      switch(TD->model->kernel) {
         case LWPR_GAUSSIAN_KERNEL:
            w = exp(-0.5*dist);
            break;
         case LWPR_BISQUARE_KERNEL:
            w = 1-0.25*dist;
            w = (w<0) ? 0 : w*w;
            break;
      }

      if (w > TD->w_max) {
         TD->w_max = w;
         idx_max = n;
      }
      sum_all += w;
      /*预测时，只使用可靠的（核距离大于cuttoff,且trustworthy）,但是在计算est_future时，只要核距离大于cutoff时都需要考虑 */
      if (w > TD->cutoff) {
         double yp_n = RF->beta0;
         double sigma2 = 0.0;
         int nR = RF->nReg;
         //get error history and num, modified by jim 2021.1.1.22
         double *histroy_error_array = RF->pred_error_history;
         int num_error_array = RF->pred_error_num;
         double avg_error = 0.0;
         // judge whether num_error_arry == 0, if 0 then skip it, modified by jim in 2022.6.20
         if (num_error_array > 0){
             // calculate the avg error
            avg_error = lwpr_math_avg_vector(histroy_error_array, num_error_array);
            //calculate min_error, modified 2021.1.23
            if (avg_error < min_error){
               min_error = avg_error;
            }
         } 
        
         // modifed add condition:  nIn>1
         if (RF->n_data[nR-1] <= 2*nIn && nIn>1) nR--;

         for (i=0;i<nIn;i++) {
            xc[i] = TD->xn[i] - RF->mean_x[i];
         }
         lwpr_aux_compute_projection(nIn, nInS, nR, s, xc, RF->U, RF->P, WS);
         //计算该RF的置信度
         for (i=0;i<nR;i++) {
            yp_n+=s[i]*RF->beta[i];
            sigma2 +=s[i]*s[i] / RF->SSs2[i];
         }
         // the variance of current receptive field
         sigma2 = RF->sum_e_cv2[nR-1]/(RF->sum_w[nR-1] - RF->SSp)*(1+w*sigma2);
         //save the value of improvement, use function N(u, a) to calculate the confidence given the confidence bound.
         //p(|y-y_n|<=y_n*confidence_bound_percentile) = p(y_n*confidence_bound_percentile - y_n =<y<=y_n*confidence_bound_percentile +y_n)>= alpha
         //confidence
         double alpha = 0;
         //modified by jim 2020.12.24,we just consider the case sigma2>0.000001
         //modified by jim 2022.6.20
         if(sigma2==0 && num_error_array==0){
            //its rf has not been update now, we set its confidence to w (although it is not accuracy, but can reflect its function)
            alpha = w;
         }else if(sigma2<0.000001){
            //when variance is 0, then alpha=1
            alpha = 1;
         }else{
            //otherwise, we need calculate it.
            alpha = integral(norm_ditribution_function, yp_n-yp_n*confidence_bound_percentile, yp_n*confidence_bound_percentile +yp_n, 1000, yp_n, sigma2);
         }
         //特殊处理2021.1.6 modified by jim
         if(alpha > 1){
            alpha = 1;
         }
         //2022.6.20, in the future, we also need consider the weight when calculate the RF->conf_rf 
         RF->conf_rf = 1-alpha;
         //RF->conf_rf = sqrt(sigma2)/yp_n;
         //modified by jim 2021.2.16, calculate the future value for each query pattern
         //note: 这里的future_value_pattern[l]只考虑和当前查询相关的RF，如果无关，提升为0，因此不用考虑
         for(l=0;l<num_query_pattern;l++){
            if(RF->prob_rf[l]!=0){
               RF->prob_rf[l] = RF->prob_rf[l]/total_happen_freq[l];
               future_value_pattern[l] += RF->prob_rf[l]*RF->conf_rf;
               //future_value_pattern[l] += RF->prob_rf[l]*RF->conf_rf*w;
            }
         }
         /*当一个查询距离这个RF特别近时，需要特殊处理*/
         /*特别注意一些特殊情况，如数据库内的特征不靠谱（不同的子句，相同的特征），parametered path 具有相同的特征，但是基数随着参数表而变动，因此加入avg_error < avg_error_threshold进一步进行验证*/
         if (w > 0.999 && avg_error < avg_error_threshold){
             exact_flag = 1;
             exact_y = yp_n;
             continue;
         }
         //计算预测值
         if(RF->trustworthy){
            sum_wyy += w*yp_n*yp_n;
            sum_conf += w*sigma2;
            TD->yn += w*yp_n;
            sum_w += w;
            //判断是否可用于预测,modified by jim
            if(avg_error > avg_error_threshold){
               yp_n = 0;
               w = 0;
            }
         }
         else{
            //add by jim in 2022.8.2, we also add this for calculate the ev->unconf
            sum_wyy += w*yp_n*yp_n;
            sum_conf += w*sigma2;
            TD->yn += w*yp_n;
            sum_w += w;
            //否则使用knn进行计算
            //1.初始化矩阵
            double	  **matrix;
            double	   *targets;
            matrix = palloc(sizeof(*matrix) *(2.0*nIn+1));
            for (k = 0; k < (2.0*nIn+1); ++k){
               matrix[k] = palloc0(sizeof(**matrix) * nIn);
            }
            targets = palloc0(sizeof(*targets) * (2.0*nIn+1));
            //计算当前rf_hash
            rf_hash = get_int_array_hash2(subspace_pattern, nIn);
            //读取RF数据
            if(load_rf_datahouse(fss_hash, rf_hash, matrix, targets, &rows)) {
               yp_n = OkNNr_predict(rows,  nIn, matrix, targets, features, 2);
            }
            //释放内存
            for (k = 0; k < (2.0*nIn+1); ++k)
               pfree(matrix[k]);
            pfree(matrix);
            pfree(targets);
         }
         if (w > 0){
            yp += w*yp_n;
            sum_w2 += w;
            count_rfs += 1;
         }  
      }//cutoff
   }
   //detect if x is a outiler, if yes then let est_future=1;
   if(count_rfs==0 && exact_flag==0){
      //当该查询为一个离群查询时，假设对模型的作用为outer_future_value，那么需要考虑下一个查询的分布是什么，是否该模型属于各个查询
      for(i=0;i<num_query_pattern;i++){
         if(num_history_data[i]>0){
            //当大于0时，可认为该查询具有该模型
            explore_value->est_future += query_context.query_distribution[i]*outer_future_value;//modified by jim 2021.2.24
         }
      }
   }else{
      //normal sub query space, we use the formula : est_future = SUM(query_pattern_distribution * future_value_pattern);
      for(l=0;l<num_query_pattern;l++){
         explore_value->est_future += query_context.query_distribution[l] * future_value_pattern[l];
         //explore_value->est_future += query_context.query_distribution[l] *( future_value_pattern[l]/sum_all);
      }
   }
   //计算est_uncof
   if (exact_flag == 1){
      /* we confidence it 100%*/
      explore_value->est_uncof = 0;
   }else if (sum_w2 > 0.0) {
      double sum_wy = TD->yn;
      ///////////////////////original calculation//////////////////
      TD->yn /= sum_w;
      //TD->w_sec = fabs(sum_conf + sum_wyy - sum_wy*TD->yn)/(sum_w*sum_w); /*  the estimated variance which serves as confidence bound */
      ///////////////////////original calculation//////////////////
      TD->w_sec = (sum_conf + fabs(sum_wyy + sum_wy*sum_wy*(sum_w-2)))/(sum_w*sum_w);
      //confidence
      double alpha = 0;
      if(TD->w_sec < 0.000001){//modified 2020.12.24
         alpha = 1;
      }else{
         alpha = integral(norm_ditribution_function, TD->yn-TD->yn*confidence_bound_percentile, TD->yn*confidence_bound_percentile +TD->yn, 1000, TD->yn, TD->w_sec);
      }
      //特殊处理2021.1.6 modified by jim
      if(alpha > 1){
         alpha = 1;
      }
      explore_value->est_uncof = 1-alpha;
      //explore_value->est_uncof = sqrt(TD->w_sec)/TD->yn;
   } else {
      TD->w_sec = 1e20; /* DBL_INFTY; */
      // default, the confidence = 0;
      explore_value->est_uncof = 1;
   }
   // 计算预测值
   // 求最终的值
   if (exact_flag == 1){
      yp = exact_y;
   }else if (sum_w2 > 0.0) {
      yp/=sum_w2;
   }else{
      //没有相应的rf时，我们需要进一步判定，是否当前查询离最近的rf也很远，如果是，我们选用原基数估计方法，jim by2020.12.29
       if((TD->w_max < use_aqo_threshold && count_rfs==0)||(TD->w_max > use_aqo_threshold && min_error > avg_error_threshold)){
      //if(TD->w_max < use_aqo_threshold && count_rfs==0){
         yp = -9999;
      }else{
         /* 从rf数据中提取 */
         LWPR_ReceptiveField *RF = TD->model->rf[idx_max];
         //读取rf数据相关变量
         double      *subspace_pattern = RF->c;
         double	  **matrix;
         double	   *targets;
         double	   *features = TD->xn;
         int         fss_hash = TD->model->fss_hash;
         int         rf_hash;
         int			rows;
         int			k;
         // 当rf可信时，使用PLS进行计算
         if(RF->trustworthy){
            yp = RF->beta0;
            int nR = RF->nReg;
            if (RF->n_data[nR-1] <= 2*nIn) nR--;         
            lwpr_aux_compute_projection(nIn, nInS, nR, s, xc, RF->U, RF->P, WS);
            for (i=0;i<nR;i++) {yp+=s[i]*RF->beta[i];}
         }else{
            //否则使用knn进行计算
            //1.初始化矩阵
            matrix = palloc(sizeof(*matrix) *(2.0*nIn+1));
            for (k = 0; k < (2.0*nIn+1); ++k){
               matrix[k] = palloc0(sizeof(**matrix) * nIn);
            }
            targets = palloc0(sizeof(*targets) * (2.0*nIn+1));
            //计算当前rf_hash
            rf_hash = get_int_array_hash2(subspace_pattern, nIn);
            //读取RF数据
            if(load_rf_datahouse(fss_hash, rf_hash, matrix, targets, &rows)) {
               yp = OkNNr_predict(rows,  nIn, matrix, targets, features, 2);
            }
            //释放内存
            for (k = 0; k < (2.0*nIn+1); ++k)
               pfree(matrix[k]);
            pfree(matrix);
            pfree(targets);
         }
      }  
   }
   TD->yn = yp;
}

//更新
int lwpr_aux_update_one(LWPR_Model *model, const double *xn, double yn) {

   LWPR_ThreadData TD;
   TD.model = model;
	TD.xn = xn;
	TD.yn = yn;
	TD.ws = model->ws;
    //更新
   lwpr_aux_update_one_T(&TD);      
   //是否需要增加或剪枝rf
   return lwpr_aux_update_one_add_prune(model, &TD, xn, yn);
}

// 计算projection   
void lwpr_aux_compute_projection(int nIn, int nInS, int nReg, 
      double *s, const double *x, const double *U, const double *P, LWPR_Workspace *WS) {
      
   int i,j;
   double sj;
   double *xu = WS->xu; 
   
   for (i=0;i<nIn;i++) xu[i] = x[i];
   
   for (j=0;j<nReg-1;j++) {
      s[j] = sj = lwpr_math_dot_product(U+j*nInS, xu, nIn);
      /* for (i=0;i<nIn;i++) xu[i] -= P[i+j*nInS]*sj; */
      lwpr_math_add_scalar_vector(xu,-sj,P+j*nInS, nIn);
   }
   s[nReg-1]=lwpr_math_dot_product(U+(nReg-1)*nInS, xu, nIn);           
}
//具体更新实现
void lwpr_aux_update_one_T(void *ptr) {
   LWPR_ThreadData *TD = (LWPR_ThreadData *) ptr;
   LWPR_Workspace *WS = TD->ws;
   const LWPR_Model *model = TD->model;
      
   int i,j,n,nIn,nInS;
   double *xc; 
   double e,e_cv; 
  
   double ymz;
   
   double w, w_sec = 0.0, w_max = 0.0;
   int ind = -1;
   int ind_sec = -1, ind_max = -1;
   double yp = 0.0, yp_n;
   
   double sum_w = 0.0;

   double dwdq;


   nIn = TD->model->nIn;
   nInS = TD->model->nInStore;   
   xc = WS->xc;
   /* 这里需要判断是否为新建的模型，如果为新建的模型，则需要在model上更新history_query and num_history, modified by jim 2021.3.5* */
   if(model->numRFS == 0){
       //得到当前的历史数据矩阵
      double **history_data_matrix = TD->model->history_data_matrix;
      double *num_history_data = TD->model->num_history_data;
      // //历史数据矩阵的索引和个数
      int current_data_matric_inx = 1;
      int current_data_num = 1;
      if (query_context.current_query_hash!=0){
         current_data_matric_inx = query_context.current_query_hash -1;
         current_data_num = (int) num_history_data[current_data_matric_inx];
         if(current_data_num<num_history_data_compute_probability_rf){
            /* 只需添加该数据 */
            for(i=current_data_num*nIn+1; i<(current_data_num+1)*nIn+1; i++){
               history_data_matrix[current_data_matric_inx][i] = TD->xn[i-current_data_num*nIn-1];
            }
            current_data_num += 1;
            num_history_data[current_data_matric_inx] = (double) current_data_num;
         }else
         {
            /* 将最之前的一个数据进行删除*/
            for(i=1; i<num_history_data_compute_probability_rf; i++){
               for(j=0; j<nIn; j++){
                  history_data_matrix[current_data_matric_inx][(i-1)*nIn+1+j] = history_data_matrix[current_data_matric_inx][i*nIn+1+j];
               }
            }
            /*将当前数据加入到矩阵最末尾 */
            for(i=0; i<nIn; i++){
               history_data_matrix[current_data_matric_inx][(num_history_data_compute_probability_rf-1)*nIn+1+i] = TD->xn[i];
            }
            //num_history_data保持不变
         }
      }
   }else{
      // 对每个接受域进行更新
      for (n=0;n<model->numRFS;n++) {
      
         double dist = 0.0;
         // 获取当前的更新域
         LWPR_ReceptiveField *RF = TD->model->rf[n];
         //计算输入向量和中心点的差值
         for (i=0;i<nIn;i++) {
            xc[i] = TD->xn[i] - RF->c[i];
         }
         //计算核距离
         for (j=0;j<nIn;j++) {
            dist += xc[j] * lwpr_math_dot_product(RF->D + j*nInS, xc, nIn);
         }
         switch(TD->model->kernel) {
            case LWPR_GAUSSIAN_KERNEL:
               w = exp(-0.5*dist);
               dwdq = -0.5 * w;
               break;
            case LWPR_BISQUARE_KERNEL:
               dwdq = 1-0.25*dist;
               if (dwdq<0) {
                  w = dwdq = 0.0;
               } else {
                  w = dwdq*dwdq;
                  dwdq = -0.5*dwdq;
               }
               break;
            default:
               w = dwdq = 0;
         }
         
      //计算最大和次大距离及其索引
         if (w>w_sec) {
            ind = ind_sec;
            if (w>w_max) {
               ind_sec = ind_max;
               w_sec = w_max;            
               ind_max = n;
               w_max = w;
            } else {
               ind_sec = n;
               w_sec = w;
            }
         } else {
            ind = n;
         }
         //当距离大于0.001时进行更新
         if (w>0.001) {
            double transmul;
            
            RF->w = w;

            ymz = lwpr_aux_update_means(RF,TD->xn,TD->yn,w,WS->xmz,model);
            lwpr_aux_update_regression(RF, &yp_n, &e_cv, &e, WS->xmz, ymz, w, WS, model);
            ////////////////////////////////////////////////////////
            //更新error的历史记录 e,modified by jim 2021.1.21
            double *history_error_matric = RF->pred_error_history;
            int num_history_error = RF->pred_error_num;
            //更新历史数据矩阵
            if(num_history_error<num_pred_error_history){
               /* 只需添加该数据 */
               history_error_matric[num_history_error] = fabs(e/(TD->yn+0.001));
               num_history_error += 1;
            }else
            {
               /* 将最之前的一个数据进行删除*/
               for(j=1;j<num_pred_error_history;j++){
                  history_error_matric[j-1] = history_error_matric[j];
               }
               /*将当前数据加入到矩阵最末尾 */
               history_error_matric[num_pred_error_history-1] = fabs(e/(TD->yn+0.001));
            }
            //保存num_history_data and history_data_matric
            RF->pred_error_num = num_history_error;
            ////////////////////////////////////////////////////////
            if (RF->trustworthy) {
               yp += w*yp_n;
               sum_w += w;
            }
            
            if (model->update_D) {
               transmul = lwpr_aux_update_distance_metric(RF, w, dwdq, e_cv, e, TD->xn, WS, model);
            }
            
            lwpr_aux_check_add_projection(RF, model);
            
            for (i=0;i<RF->nReg;i++) {
               RF->n_data[i] = RF->n_data[i] * RF->lambda[i] + 1;
               RF->lambda[i] = model->tau_lambda * RF->lambda[i] + model->final_lambda*(1.0-model->tau_lambda);
            }
         } else {
            RF->w = 0.0;
         }
      }
   }
   TD->w_max = w_max;
   TD->ind_max = ind_max;
   TD->w_sec = w_sec;
   TD->ind_sec = ind_sec;
   TD->yp = yp;
   TD->sum_w = sum_w;
}

/* returns ymz. xmz is also output value */
double lwpr_aux_update_means(LWPR_ReceptiveField *RF, const double *x, double y, double w, double *xmz, const LWPR_Model *model) {
   int i;
   int nIn = model->nIn;
   double swl = RF->sum_w[0] * RF->lambda[0];
   double invFac = 1.0/(swl + w);
   
   for (i=0;i<nIn;i++) {
      double mx = RF->mean_x[i];
      mx = RF->mean_x[i] = (swl * mx + w*x[i])*invFac;
      mx = xmz[i] = x[i] - mx;
      RF->var_x[i] = (swl * RF->var_x[i] + w*mx*mx)*invFac;
   }
   RF->beta0 = (swl * RF->beta0 + w*y)*invFac;
   return (y-RF->beta0);
}
// 更新回归方程
void lwpr_aux_update_regression(LWPR_ReceptiveField *RF, double *yp, double *e_cv_R, double *e,
   const double *x, double y, double w, LWPR_Workspace *WS, const LWPR_Model *model) {
   
   int nIn = model->nIn;
   int nInS = model->nInStore;
   int nReg = RF->nReg;
   
   double *yres = WS->yres; 
   double *ytarget = WS->ytarget; 
   double *e_cv = WS->e_cv;
   double *xres = WS->xres; 
   double ypred = 0.0;
   double ws2_SSs2 = 0.0;
   int i,j;
   //读取rf数据相关变量
   double      *subspace_pattern = RF->c;
	double	  **matrix;
	double	   *targets;
	double	   *features = model->xn;
	double		result = model->yn;
   int         fss_hash = model->fss_hash;
   int         rf_hash;
	int			rows;
	int			k;
   List	     *changed_lines = NIL;
   ListCell   *l;
   int         new_matrix_rows = 0;
   
   
   lwpr_aux_compute_projection_r(nIn,nInS,nReg,RF->s,xres,x,RF->U,RF->P);
   // 计算yres
   yres[0] = RF->beta[0] * RF->s[0];
   for (i=1;i<nReg;i++) {
      yres[i] = RF->beta[i] * RF->s[i] + yres[i-1];
   }
   // 计算e_cv
   for (i=0;i<nReg;i++) {
      RF->sum_w[i] = RF->sum_w[i] * RF->lambda[i] + w;   
      e_cv[i] = y - yres[i];
   }
   // 计算ytarget
   ytarget[0] = y;
   for (i=0;i<nReg-1;i++) {
      ytarget[i+1] = e_cv[i];
   }
   // 对每个维度进行更新
   for (j=0;j<nReg;j++) {
      int jN = j*nInS;
      double lambda_slow = 0.9 + 0.1*RF->lambda[j];
      double wytar = w * ytarget[j];
      double Unorm = 0.0;
      double wsj = w*RF->s[j];
      double inv_SSs2j;
     
      for (i=0;i<nIn;i++) {
         double help = RF->SXresYres[i+jN] = RF->SXresYres[i+jN] * lambda_slow
               + wytar * xres[i+jN];
         Unorm += help*help;
      }
      
      /* Numerical safety measure */
      if (Unorm > 1e-24) {
         Unorm = 1.0/sqrt(Unorm);

         /* for (i=0;i<nIn;i++) RF->U[i+j*nInS] = Unorm*RF->SXresYres[i+j*nInS]; */
         lwpr_math_scalar_vector(RF->U + jN, Unorm, RF->SXresYres + jN,nIn); 
      }
      
      RF->SSs2[j] = RF->lambda[j]*RF->SSs2[j] + RF->s[j]*wsj;
      RF->SSYres[j] = RF->lambda[j]*RF->SSYres[j] + ytarget[j]*wsj;
      
      /* for (i=0;i<nIn;i++) RF->SSXres[i+j*nInS] = RF->lambda[j]*RF->SSXres[i+j*nInS] + wsj*xres[i+j*nInS]; */
      lwpr_math_scale_add_scalar_vector(RF->lambda[j], RF->SSXres + jN, wsj, xres + jN, nIn); 

      inv_SSs2j = 1.0/RF->SSs2[j];
      RF->beta[j] = RF->SSYres[j] * inv_SSs2j;
      
      /* for (i=0;i<nIn;i++) RF->P[i+j*nInS] = RF->SSXres[i+j*nInS] * inv_SSs2j; */
      lwpr_math_scalar_vector(RF->P + jN, inv_SSs2j, RF->SSXres + jN,nIn);       
      
      ws2_SSs2 += wsj * wsj * inv_SSs2j;
   }
   // this value for calculating the confidence
   RF->SSp = RF->lambda[nReg-1]*RF->SSp + ws2_SSs2;
   
   lwpr_aux_compute_projection(nIn,nInS,nReg,RF->s,x,RF->U,RF->P,WS);
   
   /* new addition: do not include last PLS dimension if not trustworthy yet */
   /* TODO: check stuff below, in particular e_cv_R */

   //* 对 nREg=1 需要特殊处理 modified by jim 2020.11.12
   if (nReg ==1 ){
      *e_cv_R = e_cv[0];
      ypred+=RF->beta[0] * RF->s[0];
   }else{
      if (RF->n_data[nReg-1] > 2.0*nIn) {
            for (j=0;j<nReg;j++) ypred+=RF->beta[j] * RF->s[j];
            *e_cv_R = e_cv[nReg-1];
      } else {
            for (j=0;j<nReg-1;j++) ypred+=RF->beta[j] * RF->s[j];
            *e_cv_R = e_cv[nReg-2];
     }
   }
   *e = y-ypred;
   //modified by jim 2020.11.12, remove the condition: if (RF->n_data[0]*(1.0-RF->lambda[0]) > 0.1) 
   RF->sum_e2 = RF->sum_e2 * RF->lambda[nReg-1] + w*(*e)*(*e);
   for (i=0;i<nReg;i++) {
      RF->sum_e_cv2[i] = RF->sum_e_cv2[i]*RF->lambda[i] + w*e_cv[i]*e_cv[i];   
   }

   *yp = ypred + RF->beta0;
    RF->slopeReady = 0;
    //判断当前rf是否已经准备好  
   if (RF->n_data[0] > 2.0*nIn) {
      RF->trustworthy = 1;
   }else{
      /* 当RF不可信时，保存数据到相应的RF数据仓库中 */
      //1.初始化矩阵
      matrix = palloc(sizeof(*matrix) *(2.0*nIn+1));
      for (k = 0; k < (2.0*nIn+1); ++k){
         matrix[k] = palloc0(sizeof(**matrix) * nIn);
      }
      targets = palloc0(sizeof(*targets) * (2.0*nIn+1));
      //计算当前rf_hash
      rf_hash = get_int_array_hash2(subspace_pattern, nIn);
      if (!load_rf_datahouse(fss_hash, rf_hash, matrix, targets, &rows))
		   rows = 0;
      //学习
      changed_lines = OkNNr_learn(rows, nIn,
                           matrix, targets,
                           features, result, (2.0*nIn+1));

      new_matrix_rows = rows;
      foreach(l, changed_lines)
      {
         if (lfirst_int(l) >= new_matrix_rows)
            new_matrix_rows = lfirst_int(l) + 1;
      }
	   //将数据写入表中
      update_rf_datahouse(fss_hash, rf_hash, nIn,  new_matrix_rows, matrix, targets);
      //释放内存
      for (k = 0; k < (2.0*nIn+1); ++k)
			pfree(matrix[k]);
		pfree(matrix);
		pfree(targets);
   }
}
/* 更新距离矩阵 */
double lwpr_aux_update_distance_metric(LWPR_ReceptiveField *RF, 
      double w, double dwdq, double e_cv, double e, const double *xn, LWPR_Workspace *WS, const LWPR_Model *model) {
      
   double transMul;
   double penalty;
   
   int nInS = model->nInStore;
   int nIn = model->nIn;
   int nR = RF->nReg;
   
   int *derivOk = WS->derivOk;

   double *Ps = WS->Ps;
   double *Pse = WS->Pse;
   
   double *dwdM = WS->dwdM;
   double *dJ2dM = WS->dJ2dM;

   double *dx = WS->dx;
   
   double h=0.0;
   double e2;
   double e_cv2;
   
   double W,E;
   double dJ1dw;
   double maxM;
   double wW;
   
   int reduced = 0;
      
   int i,j;
   
   penalty = model->penalty / model->nIn;
   
   for (i=0;i<nR;i++) {
      //derivOk[i] = (RF->n_data[i]*(1.0 - RF->lambda[0]) > 0.1) ? 1:0;
      derivOk[i] = 1;
   }
   
   if (!derivOk[0]) return 0.0;
    
   e2 = e*e;
   e_cv2 = e_cv*e_cv;
   
   for (i=0;i<nR;i++) {
      if (derivOk[i]) h+=RF->s[i]*RF->s[i]/RF->SSs2[i];
   }
   h*=w;
   W = RF->sum_w[0];   /* has already been updated in update_regression */
   if (nR==1) {
      E = RF->sum_e_cv2[0];
   } else {
      if (RF->n_data[nR-1]>2*nIn) {
	      E = RF->sum_e_cv2[nR-1];
      } else {
         E = RF->sum_e_cv2[nR-2];
      }
   }
   transMul = RF->sum_e2/(E+1E-10); /* to the 4th power ... */
   transMul*=transMul;   transMul*=transMul; 
   
   if (transMul>1.0) transMul = 1.0;
   
   dJ1dw = -E/W + e_cv2; /* another division by W comes later */
   for (i=0;i<nR;i++) {
      if (derivOk[i]) {
         Ps[i] = RF->s[i]/RF->SSs2[i];
         Pse[i] = e*Ps[i];
         dJ1dw -= 2.0*Pse[i]*RF->H[i] + 2.0*Ps[i]*Ps[i]*RF->r[i];
      } else {
         Ps[i] = Pse[i] = 0.0;
      }
   }
   dJ1dw/=W;
   
   wW = w/W;
   
   for (i=0;i<nIn;i++) dx[i]=xn[i]-RF->c[i];
   lwpr_aux_dist_derivatives(nIn, nInS, dwdM, dJ2dM, w, dwdq, RF->D, RF->M, dx, model->diag_only, penalty);  
   
   if (model->diag_only) {
   
      maxM = 0.0;
      for (j=0;j<nIn;j++) {   
         double m = fabs(RF->M[j+j*nInS]);
         if (m>maxM) maxM=m;
      }
      
      for (j=0;j<nIn;j++) {
         int off = j + j*nInS;         
         dJ2dM[off] = wW * dJ2dM[off] + dwdM[off]*dJ1dw;
      }
      

      for (j=0;j<nIn;j++) {   
         int off = j + j*nInS;                     
         double delta_M_jj = RF->alpha[off] * transMul * dJ2dM[off];
         if (delta_M_jj > 0.1*maxM) {
            RF->alpha[off]*=0.5;
            reduced = 1;
         } else {
            RF->M[off] -= delta_M_jj;
         }
      }

      for (j=0;j<nIn;j++) {   
         RF->D[j+j*nInS] = RF->M[j+j*nInS] * RF->M[j+j*nInS];
      }
   
   } else {
      /* Full distance matrix (non-diagonal) case */
      maxM = 0.0;
      for (j=0;j<nIn;j++) {   
         for (i=0;i<=j;i++) {
            double m = fabs(RF->M[i+j*nInS]);
            if (m>maxM) maxM=m;
         }
      }
   
      /* Reuse dJ2dM as dJdM */
      for (j=0;j<nIn;j++) {   
         /* for (i=0;i<=j;i++) dJ2dM[i+j*nInS] = wW * dJ2dM[i+j*nInS] + dwdM[i+j*nInS]*dJ1dw; */  
         lwpr_math_scale_add_scalar_vector(wW, dJ2dM + j*nInS, dJ1dw, dwdM + j*nInS, j+1);
      }

      for (j=0;j<nIn;j++) {   
         for (i=0;i<=j;i++) {
            double delta_M_ij = RF->alpha[i+j*nInS] * transMul * dJ2dM[i+j*nInS];
            if (delta_M_ij > 0.1*maxM) {
               reduced = 1;
               RF->alpha[i+j*nInS]*=0.5;
            } else {
               RF->M[i+j*nInS] -= delta_M_ij;
            }
         }
      }

      for (j=0;j<nIn;j++) {   
         /* Calculate in lower triangle, fill upper */
         for (i=0;i<j;i++) {   
            RF->D[i+j*nInS] = RF->D[j+i*nInS];
         }
         for (i=j;i<nIn;i++) {
            RF->D[i+j*nInS] = lwpr_math_dot_product(RF->M + i*nInS, RF->M + j*nInS,j+1);
         }
      }
   }
   
   for (i=0;i<nR;i++) {
      if (derivOk[i]) {
         RF->H[i] = RF->lambda[i] * RF->H[i] + (w/(1-h))*RF->s[i]*e_cv*transMul;
         RF->r[i] = RF->lambda[i] * RF->r[i] + (w*w*e_cv2/(1-h))*RF->s[i]*RF->s[i]*transMul;
      }
   }
   return transMul; 
}

int lwpr_aux_check_add_projection(LWPR_ReceptiveField *RF, const LWPR_Model *model) {
   int nReg = RF->nReg;
   int nIn = model->nIn;
   int nInS = model->nInStore;
   double mse_n_reg, mse_n_reg_1;

   if (nReg >= nIn) return 0; /* already at full complexity */
   
   mse_n_reg = RF->sum_e_cv2[nReg-1] / RF->sum_w[nReg-1] + 1e-10;
   mse_n_reg_1 = RF->sum_e_cv2[nReg-2] / RF->sum_w[nReg-2] + 1e-10;   
   
   if ((mse_n_reg < model->add_threshold*mse_n_reg_1)
      && (RF->n_data[nReg-1] > 0.99*RF->n_data[0] )){
      //&& (RF->n_data[nReg-1]*(1.0-RF->lambda[nReg-1]) > 0.5)) {
      
        
      if (nReg == RF->nRegStore) {
         /* try to enlarge the RF, if this fails, return */
         if (!lwpr_mem_realloc_rf(RF,nReg + 2, model->nInStore)) return -1;
      }
      /* lwpr_mem_realloc_rf will have initialised the new elements to 0 
      ** So we just fill in non-zero elements */
      
      RF->SSs2[nReg] = model->init_S2;
      RF->U[nReg + nReg*nInS] = 1.0;
      RF->P[nReg + nReg*nInS] = 1.0;
      RF->sum_w[nReg]=1e-10;
      RF->lambda[nReg]=model->init_lambda;
      RF->nReg = nReg+1;
      
      RF->SSp = 0.0;
      return 1;
   } else {
      return 0;
   }
}
void lwpr_aux_compute_projection_r(int nIn, int nInS, int nReg, 
      double *s, double *xres, const double *x, const double *U, const double *P) {
      
   int i,j;
   double sj;
   
   for (i=0;i<nIn;i++) xres[i] = x[i];
   for (j=0;j<nReg-1;j++) {
      s[j] = sj = lwpr_math_dot_product(U+j*nInS, xres + j*nInS, nIn);
      for (i=0;i<nIn;i++) {
         xres[i + (j+1)*nInS] = xres[i + j*nInS] - P[i+j*nInS]*sj;
      }
   }
   s[nReg-1] = lwpr_math_dot_product(U+(nReg-1)*nInS, xres + (nReg-1)*nInS, nIn);          
}

void lwpr_aux_dist_derivatives(int nIn,int nInS,double *dwdM, double *dJ2dM, double w, double dwdq, 
        const double *RF_D, const double *RF_M, const double *dx, int diag_only, double penalty) {
         
   int m,n;
   /* Fill elements (n,m) */ 
   
   /* penalty only occurs with a factor 2, so we take it out */
   penalty+=penalty;
   
   if (diag_only) {
		/* diagonal case WITHOUT meta learning */               
		for (n=0;n<nIn;n++) {
		int n_n = n + n*nInS;
		/* take the derivative of q=dx'*D*dx with respect to nn_th element of M */            

		double aux = 2.0 * RF_M[n_n];

		dwdM[n_n] = dx[n] * dx[n] * aux * dwdq;
		dJ2dM[n_n] = penalty * RF_D[n_n] * aux;
		}
	    return;
   }
	/* non-diagonal (= upper-triangular) case WITHOUT meta learning*/   
   for (n=0;n<nIn;n++) {
		for (m=n;m<nIn;m++) {
		double sum_aux = 0.0;
		double dqdM_nm = 0.0;
		int i;

		/* take the derivative of q = dx'*D*dx with respect to nm_th element of M */
		for (i=n;i<nIn;i++) {
			/* aux corresponds to the i,n_th (= n,i_th) element of dDdm_nm  
				this is directly processed for dwdM and dJ2dM   */
			double M_ni = RF_M[n+i*nInS];
			dqdM_nm += dx[i] * M_ni;       /* additional factor 2.0*dx[m] comes after the loop */        
			sum_aux += RF_D[i + m*nInS] * M_ni;                         
		}
		dwdM[n+m*nInS] = 2.0 * dx[m] * dqdM_nm * dwdq;
		dJ2dM[n+m*nInS] = 2.0 * penalty * sum_aux;
		}
	}
}
int lwpr_aux_update_one_add_prune(LWPR_Model *model, LWPR_ThreadData *TD, const double *xn, double yn) {
   // 判断最大的w是否大于w_gen   
   if (TD->w_max <= model->w_gen) {
      LWPR_ReceptiveField *RF = lwpr_aux_add_rf(model,0);

      /* Receptive field could not be allocated. The LWPR model is still
         valid, but return "0" to indicate this */      
      if (RF == NULL) return 0;

      if ((TD->w_max > 0.1*model->w_gen) && (model->rf[TD->ind_max]->trustworthy)) {
         return lwpr_aux_init_rf(RF,model,model->rf[TD->ind_max], xn, yn);
      }
      return lwpr_aux_init_rf(RF,model,NULL, xn, yn);
   }
   
   /* Prune ReceptiveFields */
   if (TD->w_sec > model->w_prune) {
      double tr_max = 0.0, tr_sec = 0.0;
      int i,prune;
      for (i=0;i<model->nIn;i++) {
         /*
         tr_max += lwpr_math_norm2(sub->rf[TD->ind_max]->M + i*model->nInStore, model->nIn);
         tr_sec += lwpr_math_norm2(sub->rf[TD->ind_sec]->M + i*model->nInStore, model->nIn);
         */
         /* code for just comparing the traces of D */
         tr_max += model->rf[TD->ind_max]->D[i+i*model->nInStore];
         tr_sec += model->rf[TD->ind_sec]->D[i+i*model->nInStore];
      }
      /* TODO: ORIGINAL LOGIC WAS REVERSED -- CHECK */
      prune = (tr_max < tr_sec) ? TD->ind_max : TD->ind_sec;
      
      lwpr_mem_free_rf(model->rf[prune]);
      pfree(model->rf[prune]);
      
      if (prune < model->numRFS-1) {
         /* Fill the gap with last RF (we just move around the pointer) */      
         model->rf[prune] = model->rf[model->numRFS-1];
      }
      model->numRFS--;
      model->n_pruned++;
      
      /* printf("Output %d, pruned RF %d\n",dim+1,prune+1); */
      //相应删除datahouse数据
      //。。。。。后续进行补充
   }
   
   return 1;   
}

LWPR_ReceptiveField *lwpr_aux_add_rf(LWPR_Model *model, int nReg) {
   LWPR_ReceptiveField *RF;
   int nIn = model->nIn;
   //判断内存是否够用，不够用时进行重新分配
   while (model->numRFS >= model->numPointers) {
      LWPR_ReceptiveField **newStore = (LWPR_ReceptiveField **) repalloc(model->rf, (model->numPointers+16)*sizeof(LWPR_ReceptiveField *));
      if (newStore == NULL) return NULL;      
      //重新设定
      model->rf = newStore;
      model->numPointers+=16;  
   }
   //对新添加的receptive field 进行初始化
   RF = (LWPR_ReceptiveField *) palloc(sizeof(LWPR_ReceptiveField));
   if (RF == NULL) return NULL;   
   
   if (nReg > 0) {
      //int nRegStore = (nReg > 2) ? nReg : 2;
      //设定negstore =nInS
      int nRegStore = nIn;
      lwpr_mem_alloc_rf(RF, model, nReg, nRegStore);
   } else {
      memset(RF, 0, sizeof(LWPR_ReceptiveField));
   }
   
   model->rf[model->numRFS++]=RF;
   
   return RF;  
}
int lwpr_aux_init_rf(LWPR_ReceptiveField *RF, const LWPR_Model *model, const LWPR_ReceptiveField *RFT, const double *xc, double y) {
   int i,j,nReg, nRegStore;
   int nIn = model->nIn;
   int nInS = model->nInStore;
   
   if (RFT==NULL) {
      nReg = (nIn>1)? 2:1;
      //nRegStore = (nReg > 2) ? nReg : 2;
      nRegStore = nIn;
      if (!lwpr_mem_alloc_rf(RF, model, nReg, nRegStore)) return 0;
      
      memcpy(RF->D, model->init_D, nInS*nIn*sizeof(double));
      memcpy(RF->M, model->init_M, nInS*nIn*sizeof(double));
      memcpy(RF->alpha, model->init_alpha, nInS*nIn*sizeof(double));      
      RF->beta0 = y;
   } else {
      nReg = RFT->nReg;
      nRegStore = RFT->nRegStore;

      if (!lwpr_mem_alloc_rf(RF, model, nReg, nRegStore)) return 0;
      
      memcpy(RF->D, RFT->D, nInS*nIn*sizeof(double));
      memcpy(RF->M, RFT->M, nInS*nIn*sizeof(double));
      memcpy(RF->alpha, RFT->alpha, nInS*nIn*sizeof(double));      
      RF->beta0 = RFT->beta0;
   }
   /* lwpr_mem_alloc_rf has initialised all elements to zero */
   memcpy(RF->c, xc, nIn*sizeof(double));
   /* 初始化history_error_matrix*/
   for (int k =0; k < num_pred_error_history; k++){
      RF->pred_error_history[k] = 0;
   }
   // 初始化prob_rf
   for(int k = 0; k < num_query_pattern; k++){
      RF->prob_rf[k] = 0;
   }

   RF->trustworthy = 0;
   RF->w = 0.0;
   RF->sum_e2 = 0.0;
   RF->SSp = 0.0;
   
   for (i=0;i<nReg;i++) {
      RF->SSs2[i] = model->init_S2;
      RF->U[i+i*nInS] = 1.0;
      RF->P[i+i*nInS] = 1.0;
      RF->sum_w[i] = 1e-10;
      RF->n_data[i] = 1e-10;
      RF->lambda[i] = model->init_lambda;
   }
   for (j=0;j<nIn;j++) {
      for (i=0;i<=j;i++) {
         RF->b[i+j*nInS] = log(RF->alpha[i+j*nInS] + 1e-10);
      }
   }
   //将该数据写如到datahouse中
   //读取rf数据相关变量
   double      *subspace_pattern = xc;
	double	  **matrix;
	double	   *targets;
	double	   *features = xc;
	double		result = y;
   int         fss_hash = model->fss_hash;
   int         rf_hash;
	int			rows;
	int			k;
   List	     *changed_lines = NIL;
   ListCell   *l;
   int         new_matrix_rows = 0;
   /* 当RF不可信时，保存数据到相应的RF数据仓库中 */
   //1.初始化矩阵
   matrix = palloc(sizeof(*matrix) *(2.0*nIn+1));
   for (k = 0; k < (2.0*nIn+1); ++k){
      matrix[k] = palloc0(sizeof(**matrix) * nIn);
   }
   targets = palloc0(sizeof(*targets) * (2.0*nIn+1));
   //计算当前rf_hash
   rf_hash = get_int_array_hash2(subspace_pattern, nIn);
   if (!load_rf_datahouse(fss_hash, rf_hash, matrix, targets, &rows))
      rows = 0;
   //学习
   changed_lines = OkNNr_learn(rows, nIn,
                        matrix, targets,
                        features, result, (2.0*nIn+1));

   new_matrix_rows = rows;
   foreach(l, changed_lines)
   {
      if (lfirst_int(l) >= new_matrix_rows)
         new_matrix_rows = lfirst_int(l) + 1;
   }
   //将数据写入表中
   update_rf_datahouse(fss_hash, rf_hash, nIn,  new_matrix_rows, matrix, targets);
   //释放内存
   for (k = 0; k < (2.0*nIn+1); ++k)
      pfree(matrix[k]);
   pfree(matrix);
   pfree(targets);
   return 1;   
}
/*
* math 
*/
double lwpr_math_dot_product(const double *x,const double *y,int n) {
   double dp=0;
   while (n>=4) {
      dp += y[0] * x[0];
      dp += y[1] * x[1];
      dp += y[2] * x[2];
      dp += y[3] * x[3];
      n-=4;
      y+=4;
      x+=4;
   }
   switch(n) {
      case 3: dp += y[2] * x[2];
      case 2: dp += y[1] * x[1];
      case 1: dp += y[0] * x[0];
   }         
   return dp;
}
void lwpr_math_add_scalar_vector(double *y, double a,const double *x,int n) {
   /*
   DAXPY_SSE2(X,n,a,x,y);
   */
   /* for (i=0;i<n;i++) y[i] += a*x[i]; */
   while (n>=8) {
      y[0] += a*x[0];
      y[1] += a*x[1];
      y[2] += a*x[2];
      y[3] += a*x[3];
      y[4] += a*x[4];
      y[5] += a*x[5];
      y[6] += a*x[6];
      y[7] += a*x[7];
      n-=8;
      y+=8;
      x+=8;
   }
   switch(n) {
      case 7: y[6] += a*x[6];
      case 6: y[5] += a*x[5];
      case 5: y[4] += a*x[4];
      case 4: y[3] += a*x[3];
      case 3: y[2] += a*x[2];
      case 2: y[1] += a*x[1];
      case 1: y[0] += a*x[0];
   }      
}

void lwpr_math_scalar_vector(double *y, double a,const double *x,int n) {
   /* for (i=0;i<n;i++) y[i] = a*x[i]; */
   while (n>=8) {
      y[0] = a*x[0];
      y[1] = a*x[1];
      y[2] = a*x[2];
      y[3] = a*x[3];
      y[4] = a*x[4];
      y[5] = a*x[5];
      y[6] = a*x[6];
      y[7] = a*x[7];
      n-=8;
      y+=8;
      x+=8;
   }
   switch(n) {
      case 7: y[6] = a*x[6];
      case 6: y[5] = a*x[5];
      case 5: y[4] = a*x[4];
      case 4: y[3] = a*x[3];
      case 3: y[2] = a*x[2];
      case 2: y[1] = a*x[1];
      case 1: y[0] = a*x[0];
   }         
}
void lwpr_math_scale_add_scalar_vector(double b, double *y, double a,const double *x,int n) {
   /* for (i=0;i<n;i++) y[i] = b*y[i] + a*x[i]; */
   while (n>=8) {
      y[0] = b*y[0] + a*x[0];
      y[1] = b*y[1] + a*x[1];
      y[2] = b*y[2] + a*x[2];
      y[3] = b*y[3] + a*x[3];
      y[4] = b*y[4] + a*x[4];
      y[5] = b*y[5] + a*x[5];
      y[6] = b*y[6] + a*x[6];
      y[7] = b*y[7] + a*x[7];
      n-=8;
      y+=8;
      x+=8;
   }
   switch(n) {
      case 7: y[6] = b*y[6] + a*x[6];
      case 6: y[5] = b*y[5] + a*x[5];
      case 5: y[4] = b*y[4] + a*x[4];
      case 4: y[3] = b*y[3] + a*x[3];
      case 3: y[2] = b*y[2] + a*x[2];
      case 2: y[1] = b*y[1] + a*x[1];
      case 1: y[0] = b*y[0] + a*x[0];
   }      
}

/* 计算一个数组值的平均数 */
double lwpr_math_avg_vector(const double *x,int n)
{
   double avg_value = 0.0;
   for (int i=0; i<n; i++){
      avg_value += x[i];
   }
   avg_value = avg_value/n;
   return avg_value;
   
}
/* 计算积分 */

double integral(double(*p)(double,double,double),double a,double b,int n,double a1,double b1)
{
	int i;
	double x,h,s;
	h=(b-a)/n;
	x=a;//积分下限
	s=0;
	for(i=0;i<=n;i++)
	{
		x=x+h;//对函数的自变量x进行迭代
		s=s+(*p)(a1,b1,x)*h;
	}
	return s;
}
/**
 *  定义正态分布函数 
 *  a: estimation
 *  b: variance
 */
double norm_ditribution_function(double a,double b,double x)
{
   double result = 0;
   double r1 = 0;
   double r2 = 0;
   r1 = 1/sqrt(2 * 3.141592 * b);
   r2 = exp(-1*(x-a)*(x-a)/(2*b));
   result = r1*r2;
	return result;
}

//新建内存和释放内存
void lwpr_free_model(LWPR_Model *model) {
    int j;
    if (model->nIn == 0) return;
    for (j=0; j < model->numRFS; j++) {
        lwpr_mem_free_rf(model->rf[j]);
        pfree(model->rf[j]);
    }
    pfree(model->rf);
    lwpr_mem_free_ws(model->ws);
    pfree(model->ws);
    //释放explore_value
    pfree(model->explore_values);
    //释放history_data_matrix
    for(j=0;j<num_query_pattern;j++){
       pfree(model->history_data_matrix[j]);
    }
    pfree(model->history_data_matrix);
    //free其它
    pfree(model->storage);
}

int lwpr_mem_alloc_model(LWPR_Model *model, int nIn, int storeRFS) {
   int nInS;
   double *storage;
   
   nInS = (nIn&1)?(nIn+1):nIn;
   
   //分配ws 
   model->ws = (LWPR_Workspace *) palloc(sizeof(LWPR_Workspace)*1);
   if (model->ws == NULL) {
      return 0;
   }
   if (!lwpr_mem_alloc_ws(model->ws,nIn)) {
      lwpr_mem_free_ws(model->ws);
      pfree(model->ws);
      return 0;
   }
   //分配model
   storage = (double *) palloc0(sizeof(double)*(1 + nInS*(3*nIn + 2) + num_query_pattern));
   if (storage==NULL) {
      lwpr_mem_free_ws(model->ws);      
      pfree(model->ws);
      return 0;
   } 
   model->storage = storage;
   if (((intptr_t)((void *) storage)) & 8) storage++;   

   model->init_D = storage;     storage+=nInS*nIn;
   model->init_M = storage;     storage+=nInS*nIn;
   model->init_alpha = storage; storage+=nInS*nIn;
   model->norm_in = storage;    storage+=nInS;
   model->xn = storage;         storage+=nInS;
   model->num_history_data = storage;
   model->norm_out = 1;
   model->yn = 1;
   model->n_pruned = 0;   
   model->numRFS = 0;
   model->numPointers = storeRFS;
   if (storeRFS>0) {
      model->rf = (LWPR_ReceptiveField **) palloc(sizeof(LWPR_ReceptiveField *)*storeRFS);
      if (model->rf == NULL) {
         model->numPointers = 0;
         pfree(model->rf);
         lwpr_mem_free_ws(model->ws);                  
         pfree(model->ws);
         pfree(model->storage);
         return 0;
      }
   }

   model->nIn = nIn;
   model->nInStore = nInS;
   //初始化探索价值结构体
	model->explore_values = (Explore_Value *) palloc(sizeof(Explore_Value));
   /* initialial */
   lwpr_men_alloc_ev(model->explore_values);
   
   
   //初始化历史数据矩阵
   model->history_data_matrix = palloc(sizeof(*model->history_data_matrix) * num_query_pattern);
   for (int i = 0; i < num_query_pattern; i++){
      model->history_data_matrix[i] = palloc0(sizeof(**model->history_data_matrix) * (num_history_data_compute_probability_rf*nIn+1));
      //save the query pattern, modified by jim 2021.2.22
      model->history_data_matrix[i][0] = i+1;
   }
   return 1;
}

int lwpr_mem_realloc_rf(LWPR_ReceptiveField *RF, int nRegStore, int nInStore) {
   double *newStorage, *storage;
   int nInS,nReg;
   nInS = nInStore;
   nReg = RF->nReg;
   
   storage = newStorage = (double *) palloc0(sizeof(double)*(1 + nRegStore*(4*nInS + 11)));
   if (newStorage==NULL) return 0;
      
   if (((intptr_t)((void *) storage)) & 8) storage++;   
   
   memcpy(storage, RF->SXresYres, nInS*nReg*sizeof(double)); RF->SXresYres = storage; storage+=nInS*nRegStore;
   memcpy(storage, RF->SSXres,    nInS*nReg*sizeof(double)); RF->SSXres    = storage; storage+=nInS*nRegStore;
   memcpy(storage, RF->U,         nInS*nReg*sizeof(double)); RF->U         = storage; storage+=nInS*nRegStore;
   memcpy(storage, RF->P,         nInS*nReg*sizeof(double)); RF->P         = storage; storage+=nInS*nRegStore;

   memcpy(storage, RF->beta,      nReg*sizeof(double)); RF->beta      = storage; storage+=nRegStore;
   memcpy(storage, RF->SSs2,      nReg*sizeof(double)); RF->SSs2      = storage; storage+=nRegStore;
   memcpy(storage, RF->SSYres,    nReg*sizeof(double)); RF->SSYres    = storage; storage+=nRegStore;
   memcpy(storage, RF->H,         nReg*sizeof(double)); RF->H         = storage; storage+=nRegStore;   
   memcpy(storage, RF->r,         nReg*sizeof(double)); RF->r         = storage; storage+=nRegStore;
   memcpy(storage, RF->sum_w,     nReg*sizeof(double)); RF->sum_w     = storage; storage+=nRegStore;
   memcpy(storage, RF->sum_e_cv2, nReg*sizeof(double)); RF->sum_e_cv2 = storage; storage+=nRegStore;
   memcpy(storage, RF->n_data,    nReg*sizeof(double)); RF->n_data    = storage; storage+=nRegStore;
   memcpy(storage, RF->lambda,    nReg*sizeof(double)); RF->lambda    = storage; storage+=nRegStore;
   memcpy(storage, RF->s,         nReg*sizeof(double)); RF->s         = storage;
   //memcpy(storage, RF->pred_error_history,    num_pred_error_history*sizeof(double)); RF->pred_error_history       = storage;
   //这里还应该去复制我们定义的，暂时使用不到该函数，故先舍去，未来进行修改 2021.2.24
   pfree(RF->varStorage);
   RF->varStorage = newStorage;
   RF->nRegStore = nRegStore;     
   return 1;
}

int lwpr_mem_alloc_rf(LWPR_ReceptiveField *RF, const LWPR_Model *model, int nReg, int nRegStore) {
   double *storage;
   int nIn = model->nIn;
   int nInS = model->nInStore;
   
   if (nRegStore < nReg) nRegStore = nReg;
   
   RF->nReg = nReg;
   RF->nRegStore = nRegStore;   
   
   /* First allocate stuff independent of nReg:
   **    D,M,alpha,h,b are nIn x nIn
   **    mean_x, var_x are nIn x 1
   **           slope  is  nIn x 1
   **      ==>  nIn * (5*nIn + 4)
   */
   
   storage = RF->fixStorage = (double *) palloc0(sizeof(double)*(1 + nInS*(5*nIn + 4)));
   if (storage==NULL) return 0;
   
   if (((intptr_t)((void *) storage)) & 8) storage++;
   RF->alpha  = storage; storage+=nInS*nIn;
   RF->D      = storage; storage+=nInS*nIn;
   RF->M      = storage; storage+=nInS*nIn;
   RF->h      = storage; storage+=nInS*nIn;
   RF->b      = storage; storage+=nInS*nIn;
   RF->c      = storage; storage+=nInS;   
   RF->mean_x = storage; storage+=nInS;
   RF->slope  = storage; storage+=nInS;
   RF->var_x  = storage; 
   
   /* Now, allocate stuff dependent of nReg (nRegStore):
   ** Align the matrices SXresYres, SSXres, W, and U on 16-Byte boundaries
   ** Alignment of the rest can be assured if nRegStore is always chosen even (2,4,...)
   */
   //we add num_pred_error_history to access memory
   storage = RF->varStorage = (double *) palloc0(sizeof(double)*(1 + nRegStore*(4*nInS + 10) + num_pred_error_history + num_query_pattern));
   
   if (storage==NULL) {
      /* free already alloced storage */
      pfree(RF->fixStorage);
      RF->fixStorage=NULL;
      return 0;
   }   
     
   if (((intptr_t)((void *) storage)) & 8) storage++;

   RF->SXresYres = storage; storage+=nInS*nRegStore;
   RF->SSXres    = storage; storage+=nInS*nRegStore;
   RF->U         = storage; storage+=nInS*nRegStore;
   RF->P         = storage; storage+=nInS*nRegStore;

   RF->beta      = storage; storage+=nRegStore;
   RF->SSs2      = storage; storage+=nRegStore;
   RF->SSYres    = storage; storage+=nRegStore;
   RF->H         = storage; storage+=nRegStore;
   RF->r         = storage; storage+=nRegStore;
   RF->sum_w     = storage; storage+=nRegStore;
   RF->sum_e_cv2 = storage; storage+=nRegStore;
   RF->n_data    = storage; storage+=nRegStore;
   RF->lambda    = storage; storage+=nRegStore;
   RF->s         = storage; storage+=nRegStore;
   RF->prob_rf   = storage; storage+=num_query_pattern;
   RF->pred_error_history = storage;
   RF->pred_error_num = 0;
   RF->w = RF->beta0 = RF->sum_e2 = 0.0;
   RF->trustworthy = 0;
   RF->slopeReady = 0;
   /* initialize the probability and conf_rf */
   RF->conf_rf = 0;
   return 1;
}

/*Allocate men for Explore value struct*/
int lwpr_men_alloc_ev(Explore_Value *ev){
   ev->rows=0;
   ev->est_future=0;
   ev->est_uncof=0;
   ev->rate=rate_between_explore_value;
   return 1;
}

void lwpr_mem_free_rf(LWPR_ReceptiveField *RF) {
   RF->nRegStore = 0;
   pfree(RF->fixStorage);
   pfree(RF->varStorage);
}

void lwpr_mem_free_ws(LWPR_Workspace *ws) {
   pfree(ws->derivOk);
   pfree(ws->storage);
}

int lwpr_mem_alloc_ws(LWPR_Workspace *ws, int nIn) {
   int nInS;
   double *storage;
   
   nInS = (nIn&1) ? nIn+1 : nIn;
  
   ws->derivOk = (int *) palloc0(sizeof(int)*nIn);
   
   if (ws->derivOk == NULL) return 0;
   
   ws->storage = storage = (double *) palloc0(sizeof(double)*(1 + 8*nInS*nIn + 7*nInS + 6*nIn));
   
   if (storage == NULL) {
      pfree(ws->derivOk);
      return 0;
   }
   
   if (((intptr_t)((void *) storage)) & 8) storage++;   
   ws->dwdM     = storage; storage+=nInS*nIn;
   ws->dJ2dM    = storage; storage+=nInS*nIn;
   ws->ddwdMdM  = storage; storage+=nInS*nIn;
   ws->ddJ2dMdM = storage; storage+=nInS*nIn;
   ws->xres     = storage; storage+=nInS*nIn;   
   ws->dx       = storage; storage+=nInS;
   ws->xu       = storage; storage+=nInS;
   ws->xc       = storage; storage+=nInS;   
   ws->xmz      = storage; storage+=nInS;      
   
   ws->dsdx     = storage; storage+=nInS*nIn;      
   ws->Dx       = storage; storage+=nInS;
   /* The following variables are needed for calculating 
   ** gradients and Hessians of the predictions.
   ** In theory they could use the same space as, say, dwdM etc.
   ** However, small savings in memory consumption is probably
   ** not worth the hassle */
   ws->sum_dwdx        = storage; storage+=nInS;
   ws->sum_ydwdx_wdydx = storage; storage+=nInS;   
   ws->sum_ddwdxdx     = storage; storage+=nInS*nIn;      
   ws->sum_ddRdxdx     = storage; storage+=nInS*nIn;            
   
   /* needs only nReg storage (<=nIn), no alignment necessary */
   ws->e_cv     = storage; storage+=nIn;   
   ws->Ps       = storage; storage+=nIn;   
   ws->Pse      = storage; storage+=nIn;      
   ws->ytarget  = storage; storage+=nIn;         
   ws->yres     = storage; storage+=nIn;            
   ws->s        = storage; storage+=nIn;            
   
   return 1;
}