#include "data_structures.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

unsigned long get_mwf_size(Parameters *p, int t_dim){
  unsigned long diam_width, diam_height, wf_updates, wf_elements, lnx, t_order, total_points;

  t_order = p->stencil.time_order;
  diam_width = (t_dim+1)*2*p->stencil.r;
  int nwf = p->stencil_ctx.num_wf;
  diam_height = t_dim*2*p->stencil.r + nwf;

  int bsl = p->stencil_ctx.bs_x + t_dim*p->stencil.r;
  lnx = (bsl < p->ldomain_shape[0] ? bsl : p->ldomain_shape[0]);

  wf_updates = (t_dim+1)*(t_dim+1)*2 * p->stencil.r; // Y-T projection
  wf_elements = (wf_updates - diam_width) * p->stencil.r + diam_width + diam_width*(nwf-1);

  switch(p->stencil.coeff){
  case CONSTANT_COEFFICIENT:
    total_points = ( (t_order+1)             *wf_elements + (diam_width + diam_height )*2*p->stencil.r) * lnx * sizeof(FLOAT_PRECISION);
    break;

  case VARIABLE_COEFFICIENT:
    total_points = ( (t_order+1 + (1+p->stencil.r) )*wf_elements + (diam_width + diam_height )*2*p->stencil.r) * lnx * sizeof(FLOAT_PRECISION);
    break;

  case VARIABLE_COEFFICIENT_AXSYM:
    total_points = ( (t_order+1 + (1+3*p->stencil.r) )*wf_elements + (diam_width + diam_height )*2*p->stencil.r) * lnx * sizeof(FLOAT_PRECISION);
    break;

  case VARIABLE_COEFFICIENT_NOSYM:
    total_points = ( (t_order+1 + (1+6*p->stencil.r) )*wf_elements + (diam_width + diam_height )*2*p->stencil.r) * lnx * sizeof(FLOAT_PRECISION);
    break;

  default:
    printf("ERROR: unknown type of stencil\n");
    exit(1);
    break;
  }
  //printf("npx:%d  nx:%d  lnx:%lu  updates:%lu  elements:%lu  total:%lu\n", p->t.shape[0],  p->ldomain_shape[0], lnx, wf_updates, wf_elements, total_points);
  return total_points;
}
double run_tuning_test(Parameters *tp){
  double t = 0.0;
  int reps = 1, orig_reps;
  float obt_perf;

  do{
    tp->nt = reps*(tp->t_dim+1)*2 + 2;
    tp->prof.ts_main = 0;
    dynamic_intra_diamond_ts(tp);
    t = tp->prof.ts_main;
    orig_reps =reps;
    reps *= (int) ceil(3.0/t);
  } while(t < 2.0);

  unsigned long lups = (tp->ln_stencils*tp->nt - tp->idiamond_pro_epi_logue_updates);
  obt_perf =  lups/tp->prof.ts_main;

  printf("%06.2f]  time:%es  lups:%lu  cache block size:%lukiB  reps:%d\n",
      obt_perf/(1e6), tp->prof.ts_main, lups, get_mwf_size(tp, tp->t_dim)*(tp->num_threads/tp->stencil_ctx.thread_group_size)/1024, orig_reps);

  return obt_perf;
}
void auto_tune_diam_nwf(Parameters *op){
  Parameters tp;
  copy_params_struct(*op, &tp);
  tp.stencil_shape[0] = tp.stencil_shape[0]/tp.t.shape[0];
  tp.stencil_shape[1] = tp.stencil_shape[1]/tp.t.shape[1];
  tp.stencil_shape[2] = tp.stencil_shape[2]/tp.t.shape[2];
  tp.t.shape[0]=1;
  tp.t.shape[1]=1;
  tp.t.shape[2]=1;
  tp.mpi_size = 1;

  if( (tp.wavefront == 1) && (tp.stencil_ctx.thread_group_size != 1) ) // multi-thread group
    tp.wavefront = -1;

  double exp_perf, cur_perf, prev_nwf_perf, prev_diam_perf;
  int i, lt_dim, max_t_dim, prev_max_nwf, diam_width, prev_t_dim;
  unsigned long wf_size, ntg;
  int tgs = tp.stencil_ctx.thread_group_size;
  int cache_size_cond, int_diam_cond, wf_len_cond, cuncurrency_cond, diam_concurrency;
  int diam_height;
  ntg = tp.num_threads / tgs;

  tp.stencil_ctx.num_wf = tgs; // set the number of wavefronts to the minimum possible value
  if((tp.mwd_type == 2) | (tp.mwd_type == 3)) tp.stencil_ctx.num_wf = tgs*tp.stencil.r;


  // find max possible diamond width and allocate memory accordingly, if not pre set
  if(op->t_dim == -1){
    max_t_dim = -1;
    for(i=tp.stencil_shape[1]/tp.stencil.r; i>=4; i-=4){
      lt_dim = i/2 - 1;
      diam_width = i*tp.stencil.r;
      diam_height = lt_dim*2*tp.stencil.r+1 + tp.stencil_ctx.num_wf-1;

      wf_size = get_mwf_size(&tp, lt_dim);
      cache_size_cond = wf_size*ntg < (unsigned long) (MAX_CACHE_SIZE*1024);

      cuncurrency_cond = (tp.stencil_shape[1]/diam_width) >= ntg;
      int_diam_cond = tp.stencil_shape[1]%diam_width == 0;
      wf_len_cond = diam_height <= tp.stencil_shape[2];

  //    printf("i:%d, diam_width %d,  cuncurrency_cond %d, cache_size_cond %d, int_diam_cond %d, wf_len_cond %d, cache_blk_size: %lu kB\n",
  //        i, diam_width, cuncurrency_cond, cache_size_cond, int_diam_cond, wf_len_cond, wf_size*ntg/1024);
      if( (int_diam_cond == 1) && (wf_len_cond == 1) && (cuncurrency_cond == 1)  && (cache_size_cond == 1) ){ // consider limitation in z and concurrency
        tp.t_dim = lt_dim;
        max_t_dim = lt_dim;
  //      tp.stencil_shape[1] = i*tp.stencil.r;
        break;
      }
    }

    if (max_t_dim == -1){
      op->t_dim = -1;
      return;
    }

    printf("[AUTO TUNE] max. diamond width: %d\n", (max_t_dim+1)*2*tp.stencil.r);
  } 

  // initialize the data of the tuning experiments
  init(&tp);
  arrays_allocate(&tp);
  init_coeff(&tp);
  domain_data_fill(&tp);

  // Allocate the wavefront profiling timers
  int num_thread_groups = (int) ceil(1.0*tp.num_threads/tp.stencil_ctx.thread_group_size);
  tp.stencil_ctx.t_wait        = (double *) malloc(sizeof(double)*tp.num_threads);
  tp.stencil_ctx.t_wf_main     = (double *) malloc(sizeof(double)*num_thread_groups);
  tp.stencil_ctx.t_wf_comm = (double *) malloc(sizeof(double)*num_thread_groups);
  tp.stencil_ctx.t_wf_prologue = (double *) malloc(sizeof(double)*num_thread_groups);
  tp.stencil_ctx.t_wf_epilogue = (double *) malloc(sizeof(double)*num_thread_groups);
  tp.stencil_ctx.wf_num_resolved_diamonds = (double *) malloc(sizeof(double)*num_thread_groups);
  tp.stencil_ctx.t_group_wait = (double *) malloc(sizeof(double)*num_thread_groups);
 
  if(op->t_dim == -1){ // tune both diamond widht and number of frontlines
    // brute force search for best diamond/nwf starting from small to large
    // test diamond sizes from smallest to largest
    prev_diam_perf = -1;
    prev_t_dim = 0;
    for(i=4; i<=(max_t_dim+1)*2; i+=4){ // loop over diamond sizes
      tp.t_dim = i/2 - 1;
      diam_width = i*tp.stencil.r;
      wf_size = get_mwf_size(&tp, tp.t_dim);
      diam_concurrency = tp.stencil_shape[1]/diam_width;

      cache_size_cond = wf_size*ntg > (unsigned long) (tp.cache_size*1024);
      cuncurrency_cond = diam_concurrency >= ntg;
      int_diam_cond = tp.stencil_shape[1]%diam_width == 0;
  //    printf("i:%d, diam_width %d,  cuncurrency_cond %d, cache_size_cond %d, int_diam_cond %d, wf_len_cond %d, cache_blk_size: %lu kB\n",
  //        i, diam_width, cuncurrency_cond, cache_size_cond, int_diam_cond, wf_len_cond, wf_size*ntg/1024);

      if( (int_diam_cond == 1) && (cuncurrency_cond == 1) && (cache_size_cond == 1) ){ // check diamond size validity
        printf("[AUTO TUNE] Diamond width:%02d  [wavefronts #: pefromance (MLUPS/s)]\n", (tp.t_dim+1)*2*tp.stencil.r);
        // loop over increasing number of wavefronts per update
        prev_nwf_perf = -1;
        tp.stencil_ctx.num_wf = tgs; // start with smallest possible number of updates
        if( (tp.mwd_type == 2) | (tp.mwd_type == 3) ) tp.stencil_ctx.num_wf = tgs*tp.stencil.r;
        tp.idiamond_pro_epi_logue_updates = (unsigned long) (tp.stencil_shape[0] * tp.stencil_shape[2]) * (unsigned long) (2*diam_concurrency) * ((tp.t_dim+1)*(tp.t_dim+1) + (tp.t_dim+1))*tp.stencil.r;

        while(1){
          wf_size = get_mwf_size(&tp, tp.t_dim);
          cache_size_cond = wf_size*ntg > (unsigned long) (tp.cache_size*1024);
          diam_height = tp.t_dim*2*tp.stencil.r+1 +tp.stencil_ctx.num_wf-1;
          wf_len_cond = diam_height <= tp.stencil_shape[2];

          if( (wf_len_cond==1) && (cache_size_cond == 1) ){
            printf("[AUTO TUNE]     [%03d: ",tp.stencil_ctx.num_wf);  
            exp_perf = run_tuning_test(&tp);

  //          printf("tgs:%d  nwf:%d  perf:%6.2f  prev_nwf_perf:%6.2f\n", tgs, tp.stencil_ctx.num_wf, exp_perf/1024/1024, prev_nwf_perf/1024/1024);

            // termination criteria for the nwf
            if (exp_perf < prev_nwf_perf){
              tp.stencil_ctx.num_wf -= tgs;
              break;
            }
            else{
              prev_nwf_perf = exp_perf;
              tp.stencil_ctx.num_wf += tgs;
            }

          }
          else{ // invalid wavefront length
            tp.stencil_ctx.num_wf -= tgs;
            break;
          }

        }
        if(tp.stencil_ctx.num_wf < tgs){
          tp.stencil_ctx.num_wf = tgs;
          //printf("ERROR: Invalid Wavefronts #\n");
          //exit(1);
        }

        // termination criteria for diamond size
        if (prev_nwf_perf < prev_diam_perf){
          tp.t_dim = prev_t_dim; // revert to previous diamond size
          tp.stencil_ctx.num_wf = prev_max_nwf;
          break;
        }
        else{
          prev_diam_perf = prev_nwf_perf;
          prev_t_dim = tp.t_dim;
          prev_max_nwf = tp.stencil_ctx.num_wf;
        }

      }
    }
    if (op->t_dim < 1) op->t_dim = 1;  

    op->t_dim = tp.t_dim;
    op->stencil_ctx.num_wf = tp.stencil_ctx.num_wf;


  } else { // diamond width is provided but not the number of frontlines
    
    diam_width =  ((tp.t_dim+1)*2)*tp.stencil.r;
    diam_concurrency = tp.stencil_shape[1]/diam_width;
    prev_nwf_perf = -1;
    tp.idiamond_pro_epi_logue_updates = (unsigned long) (tp.stencil_shape[0] * tp.stencil_shape[2]) * (unsigned long) (2*diam_concurrency) * ((tp.t_dim+1)*(tp.t_dim+1) + (tp.t_dim+1))*tp.stencil.r;

    while(1){
      diam_height = tp.t_dim*2*tp.stencil.r+1 +tp.stencil_ctx.num_wf-1;
      wf_len_cond = diam_height <= tp.stencil_shape[2];

      if(wf_len_cond==1){
        printf("[AUTO TUNE]     [%03d: ",tp.stencil_ctx.num_wf);  
        exp_perf = run_tuning_test(&tp);

//          printf("tgs:%d  nwf:%d  perf:%6.2f  prev_nwf_perf:%6.2f\n", tgs, tp.stencil_ctx.num_wf, exp_perf/1024/1024, prev_nwf_perf/1024/1024);

        // termination criteria for the nwf
        if (exp_perf < prev_nwf_perf){
          tp.stencil_ctx.num_wf -= tgs;
          break;
        }
        else{
          prev_nwf_perf = exp_perf;
          tp.stencil_ctx.num_wf += tgs;
        }

      }
      else{ // invalid wavefront length
        tp.stencil_ctx.num_wf -= tgs;
        break;
      }

    }
    if(tp.stencil_ctx.num_wf < tgs){
      tp.stencil_ctx.num_wf = tgs;
    }
    if( (tp.mwd_type == 2) | (tp.mwd_type == 3) ){ 
      if(tp.stencil_ctx.num_wf < tgs*tp.stencil.r){
        tp.stencil_ctx.num_wf = tgs*tp.stencil.r;
      }
    }
    op->stencil_ctx.num_wf = tp.stencil_ctx.num_wf;
  }

  //simple tuning of blocking in X
  diam_concurrency = tp.stencil_shape[1]/((tp.t_dim+1)*2*tp.stencil.r);
  tp.idiamond_pro_epi_logue_updates = (unsigned long) (tp.stencil_shape[0] * tp.stencil_shape[2]) * (unsigned long) (2*diam_concurrency) * ((tp.t_dim+1)*(tp.t_dim+1) + (tp.t_dim+1))*tp.stencil.r;
  int prev_bs_x, base_bs_x;
  double div=1.0;
  if (tp.stencil_ctx.bs_x > tp.stencil_shape[0]/tp.t.shape[0]){//no blocking set by user
    tp.stencil_ctx.bs_x = tp.stencil_shape[0]/tp.t.shape[0];
    base_bs_x = tp.stencil_ctx.bs_x;
    printf("[AUTO TUNE]     Diamond width:%02d, wavefronts #:%d  [Blk. in X: pefromance (MLUPS/s)]\n", (tp.t_dim+1)*2*tp.stencil.r, tp.stencil_ctx.num_wf);
    printf("[AUTO TUNE]          [%03d: ", tp.stencil_ctx.bs_x);  
    prev_nwf_perf = run_tuning_test(&tp);
    while(1){
      div += 1;
      prev_bs_x = tp.stencil_ctx.bs_x; 
      tp.stencil_ctx.bs_x = ceil( ((double)(base_bs_x)) /div);
      printf("[AUTO TUNE]          [%03d: ", tp.stencil_ctx.bs_x);  
      exp_perf = run_tuning_test(&tp);

      if(exp_perf < prev_nwf_perf){
        op->stencil_ctx.bs_x = prev_bs_x;
        break;
      }
      prev_nwf_perf = exp_perf;
    }
  }


  free(tp.stencil_ctx.t_wait);
  free(tp.stencil_ctx.t_wf_main);
  free(tp.stencil_ctx.t_wf_comm);
  free(tp.stencil_ctx.t_wf_prologue);
  free(tp.stencil_ctx.t_wf_epilogue);
  free(tp.stencil_ctx.wf_num_resolved_diamonds);
  free(tp.stencil_ctx.t_group_wait);
  arrays_free(&tp);
  mpi_halo_finalize(&tp);
}

void intra_diamond_info_init(Parameters *p){
  int i, in_dimension=0;
  int nt2, remain, min_z;
  int nt = p->nt;
  int diam_concurrency, num_thread_groups;
  unsigned long diam_width;


    p->wf_larger_blk_size = 0;
    p->larger_t_dim = 0;

    if( (p->stencil_ctx.thread_group_size !=-1) && ((p->t_dim == -1) || (p->stencil_ctx.num_wf==-1) )){ // thread group size is defined but not the diamond size
      if(p->mpi_rank == 0){
        p->stencil_ctx.enable_likwid_m = 0;
        auto_tune_diam_nwf(p);
        p->stencil_ctx.enable_likwid_m = 1;
      }
      if(p->mpi_size > 1){ // broad cast the autotuning params
        MPI_Bcast(&(p->t_dim), 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&(p->stencil_ctx.num_wf), 1, MPI_INT, 0, MPI_COMM_WORLD);
      }
      if(p->t_dim == -1){
        if(p->mpi_rank == 0) 
          printf("EXITING: No feasible diamond width for this problem configurations\n");
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
        exit(0);
      }
    }
    p->wf_blk_size = get_mwf_size(p, p->t_dim);


    diam_width = (p->t_dim+1) * 2 * p->stencil.r;
    diam_concurrency = (p->stencil_shape[1]/p->t.shape[1]) / diam_width;

    if (p->stencil_ctx.thread_group_size ==-1) { // setup default thread group information
      if(p->num_threads == 1)
        p->stencil_ctx.thread_group_size = 1;
      else
        p->stencil_ctx.thread_group_size = ( (diam_concurrency<2) ? p->num_threads : (p->num_threads/2) );
    }
    else { // thread group size is set by the user
      num_thread_groups = (int) ceil(1.0*p->num_threads/p->stencil_ctx.thread_group_size);
      //printf("threads:%d  num_thread_groups:%d thread_group_size:%d width:%d stencils:%d concurrency:%d\n",p->num_threads, num_thread_groups, p->stencil_ctx.thread_group_size, diam_width, p->stencil_shape[1]/p->t.shape[1], diam_concurrency);
      if(num_thread_groups > diam_concurrency)
        if(p->mpi_rank ==0){
          printf("###ERROR: the number of thread groups exceed the available concurrency. Consider using %d thread groups or less\n", ((diam_concurrency>1)?diam_concurrency-1:1));
          MPI_Barrier(MPI_COMM_WORLD);
          MPI_Finalize();
          exit(1);
        }



      // check for thread assignment validity
      if(p->stencil_ctx.thread_group_size > p->num_threads){
        if(p->mpi_rank ==0){
          printf("###WARNING: Requested thread group size is larger the total available threads \n");
        }
      }
    }

    // check for block size in X validity
    if(p->stencil_ctx.bs_x%p->stencil.r != 0) {
      if(p->mpi_rank ==0){
        fprintf(stderr, "###ERROR: Block size in in X must be multiples of the stencil radius\n");
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
        exit(1);
      }
    }

    // check if thread group sizes are equal
    if(p->num_threads%p->stencil_ctx.thread_group_size != 0){
      if(p->mpi_rank ==0){
        fprintf(stderr, "###ERROR: thread groups must be equal in size when using pre-computed wavefront assignment\n");
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
        exit(1);
      }
    }

    // Check for size validity in the direction of the wavefront
    if( (p->wavefront == 1) && (p->stencil_ctx.thread_group_size == 1) ){ // single-thread group
      min_z = (p->t_dim*2)*p->stencil.r+1;
      if(p->stencil_shape[2] < min_z){
        if(p->mpi_rank ==0) fprintf(stderr,"ERROR: The single core wavefront requires a minimum size of %d at the Z direction in the current configurations\n", min_z);
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
        exit(1);
      }
    }

    if( (p->stencil_ctx.num_wf%p->stencil_ctx.thread_group_size != 0) && (p->stencil_ctx.thread_group_size != 1) ){
      if(p->mpi_rank ==0) fprintf(stderr,"ERROR: num_wavefronts must be multiples of thread groups size\n");
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Finalize();
      exit(1);
    }


  if (p->mwd_type == 1){ // fixed execution to data
    if( (p->stencil_ctx.num_wf%p->stencil_ctx.thread_group_size != 0) && (p->stencil_ctx.thread_group_size != 1) ){
      if(p->mpi_rank ==0) fprintf(stderr,"ERROR: number of wavefronts must be multiples of thread group size\n");
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Finalize();
      exit(1);
    }
  }

  if( (p->mwd_type == 2) | (p->mwd_type == 3) ){ // relaxed synchronization
    if(p->stencil_ctx.num_wf/p->stencil_ctx.thread_group_size < p->stencil.r){
      if(p->mpi_rank ==0) fprintf(stderr,"ERROR: number of frontlines per thread must be greater or equal to the stencil radius in the relaxed synchronization implementation\n");
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Finalize();
      exit(1);
    }
  }

  if( (p->wavefront == 1) && (p->stencil_ctx.thread_group_size != 1) ) // multi-thread group
    p->wavefront = -1;

    // Check for size validity in the direction of the wavefront
    if( (p->wavefront != 0) && (p->stencil_ctx.thread_group_size != 1) ){ // multi-thread group

      min_z = (p->t_dim*2)*p->stencil.r+1 + p->stencil_ctx.num_wf -1;
      if(p->stencil_shape[2] < min_z){
        if(p->mpi_rank ==0) fprintf(stderr,"ERROR: The multi-core wavefront requires a minimum size of %d at the Z direction in the current configurations\n", min_z);

        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
        exit(1);
      }
    }


    // Allocate the wavefront profiling timers
    num_thread_groups = (int) ceil(1.0*p->num_threads/p->stencil_ctx.thread_group_size);
    p->stencil_ctx.t_wait        = (double *) malloc(sizeof(double)*p->num_threads);
    p->stencil_ctx.t_wf_main     = (double *) malloc(sizeof(double)*num_thread_groups);
    p->stencil_ctx.t_wf_comm = (double *) malloc(sizeof(double)*num_thread_groups);
    p->stencil_ctx.t_wf_prologue = (double *) malloc(sizeof(double)*num_thread_groups);
    p->stencil_ctx.t_wf_epilogue = (double *) malloc(sizeof(double)*num_thread_groups);
    p->stencil_ctx.wf_num_resolved_diamonds = (double *) malloc(sizeof(double)*num_thread_groups);
    p->stencil_ctx.t_group_wait = (double *) malloc(sizeof(double)*num_thread_groups);
    
    // number of Stencil updates in the prologue and epilogue (trapezoid and inverted trapezoid)
//    p->idiamond_pro_epi_logue_updates = (unsigned long) (p->stencil_shape[0]/p->t.shape[0] * p->stencil_shape[2]/p->t.shape[2]) * (unsigned long) (2*diam_concurrency) * ((diam_width*diam_width)/4 - (diam_width/2));

    if(p->stencil.type == REGULAR){
      p->idiamond_pro_epi_logue_updates = (unsigned long) (p->stencil_shape[0]/p->t.shape[0] * p->stencil_shape[2]/p->t.shape[2]) * (unsigned long) (2*diam_concurrency) * ((p->t_dim+1)*(p->t_dim+1) + (p->t_dim+1))*p->stencil.r;
    }else if(p->stencil.type == SOLAR){
      p->idiamond_pro_epi_logue_updates = (unsigned long) (p->stencil_shape[0]/p->t.shape[0] * p->stencil_shape[2]/p->t.shape[2] * diam_concurrency) 
                                         *((p->t_dim+1)*(p->t_dim+1)*2)*p->stencil.r;
    }

    if(p->source_point_enabled == 1){
      p->source_point_enabled = 0;
      if(p->mpi_rank ==0){
        printf("###INFO: Source point update disabled. Intra-diamond method does not support source point updates\n");
      }
    }
    if(p->t_dim < 1){
      if(p->mpi_rank == 0) fprintf(stderr,"ERROR: Diamond method does not support unrolling in time less than 1\n");
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Finalize();
      exit(1);
    }
    if(p->t_dim%2 == 0){
      fprintf(stderr,"ERROR: diamond method does not supports even time unrolling\n");
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Finalize();
      exit(1);
    }
    if( ( p->t.shape[0]>1 ) || (p->t.shape[2]>1) ){
      fprintf(stderr,"ERROR: Intra-diamond method supports domain decomposition across the Y direction only\n");
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Finalize();
      exit(1);
    }

    // round the number of time steps to the nearest valid number
    if(p->stencil.type == REGULAR){
      remain = (nt-2)%((p->t_dim+1)*2);
    }else if(p->stencil.type == SOLAR){
      remain = (nt-1)%((p->t_dim+1)*2);
    }
    if(remain != 0){
      nt2 = nt + (p->t_dim+1)*2 - remain;
      if(nt2 != nt){
        if( (p->mpi_rank ==0) && (p->verbose ==1) ){
          printf("###INFO: Modified nt from %03d to %03d for the intra-diamond method to work properly\n", nt ,nt2);
        }
        p->nt= nt2;
      }
    }



  // count the topology dimensions containing the source point
  for(i=0; i<3; i++)
    if(((p->source_pt[i]-p->stencil.r) >= p->gb[i]) && ((p->source_pt[i]-p->stencil.r) <= p->ge[i])) in_dimension++;

  if(in_dimension == 3){
    p->has_source=1;
    for(i=0; i<3; i++) p->lsource_pt[i] = p->source_pt[i] - p->gb[i];
  } else{
    p->has_source=0;
    for(i=0; i<3; i++) p->lsource_pt[i] = -1;
  }

  // Update problem size information
  int t_dim = p->t_dim;
  if(p->t.shape[1] > 1)
    p->ldomain_shape[1] += (t_dim+1)*p->stencil.r;

  p->is_last = 0;
  if(p->t.rank_coords[1] == (p->t.shape[1]-1)) {
    p->is_last = 1;
    if(p->t.shape[1] > 1)
      p->ldomain_shape[1] += 2*p->stencil.r; //consider the interior boundary layers in the last MPI rank
//      if(p->lsource_pt[1] >= p->lstencil_shape[1]+p->stencil.r) p->lsource_pt[1] += 2*p->stencil.r; // consider the interior boundary region
  }

  if (p->lstencil_shape[1] < p->stencil.r*(t_dim+1)*2){
    fprintf(stderr,"ERROR: Intra-diamond method requires the sub-domain size to fit at least one diamond: %d elements in Y [stencil_radius*2*(time_unrolls+1)]. Given %d elements\n"
        ,p->stencil.r*(t_dim+1)*2, p->lstencil_shape[1]);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    exit(1);
  }
  if (floor(p->lstencil_shape[1] / (p->stencil.r*(t_dim+1)*2.0)) != p->lstencil_shape[1] / (p->stencil.r*(t_dim+1)*2.0)){
    if (p->mpi_rank ==0) fprintf(stderr,"ERROR: Intra-diamond method requires the sub-domain size to be multiples of the diamond width: %d elements [stencil_radius*2*(time_unrolls+1)]\n"
        ,p->stencil.r*(t_dim+1)*2);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    exit(1);
  }
}
