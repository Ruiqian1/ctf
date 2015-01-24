
#include "pad.h"
#include "../shared/util.h"

namespace CTF_int {
  void pad_key(int              order,
               int64_t          num_pair,
               int const *      edge_len,
               int const *      padding,
               PairIterator     pairs,
               semiring const & sr,
               int const *      offsets){
    int64_t i, j, lda;
    int64_t knew, k;
    TAU_FSTART(pad_key);
    if (offsets == NULL){
  #ifdef USE_OMP
    #pragma omp parallel for private(knew, k, lda, i, j)
  #endif
      for (i=0; i<num_pair; i++){
        k    = pairs[i].k();
        lda  = 1;
        knew = 0;
        for (j=0; j<order; j++){
          knew += lda*(k%edge_len[j]);
          lda  *= (edge_len[j]+padding[j]);
          k     = k/edge_len[j];
        }
        pairs[i].write_key(knew);
      }
    } else {
  #ifdef USE_OMP
    #pragma omp parallel for private(knew, k, lda, i, j)
  #endif
      for (i=0; i<num_pair; i++){
        k    = pairs[i].k();
        lda  = 1;
        knew = 0;
        for (j=0; j<order; j++){
          knew += lda*((k%edge_len[j])+offsets[j]);
          lda  *= (edge_len[j]+padding[j]);
          k     = k/edge_len[j];
        }
        pairs[i].write_key(knew);
      }
    }
    TAU_FSTOP(pad_key);

  }

  void depad_tsr(int              order,
                 int64_t          num_pair,
                 int const *      edge_len,
                 int const *      sym,
                 int const *      padding,
                 int const *      prepadding,
                 char const *     pairsb,
                 char *           new_pairsb,
                 int64_t *        new_num_pair,
                 semiring const & sr){
    TAU_FSTART(depad_tsr);
    ConstPairIterator pairs = ConstPairIterator(&sr, pairsb);
    PairIterator new_pairs = PairIterator(&sr, new_pairsb);
#ifdef USE_OMP
    int64_t num_ins;
    int ntd = omp_get_max_threads();
    int64_t * num_ins_t = (int64_t*)CTF_alloc(sizeof(int64_t)*ntd);
    int64_t * pre_ins_t = (int64_t*)CTF_alloc(sizeof(int64_t)*ntd);

    TAU_FSTART(depad_tsr_cnt);
    #pragma omp parallel
    {
      int64_t i, j, st, end, tid;
      int64_t k;
      int64_t kparts[order];
      tid = omp_get_thread_num();

      st = (num_pair/ntd)*tid;
      if (tid == ntd-1)
        end = num_pair;
      else
        end = (num_pair/ntd)*(tid+1);

      num_ins_t[tid] = 0;
      for (i=st; i<end; i++){
        k = pairs[i].k();
        for (j=0; j<order; j++){
          kparts[j] = k%(edge_len[j]+padding[j]);
          if (kparts[j] >= (int64_t)edge_len[j] ||
              kparts[j] < prepadding[j]) break;
          k = k/(edge_len[j]+padding[j]);
        } 
        if (j==order){
          for (j=0; j<order; j++){
            if (sym[j] == SY){
              if (kparts[j+1] < kparts[j])
                break;
            }
            if (sym[j] == AS || sym[j] == SH){
              if (kparts[j+1] <= kparts[j])
                break;
            }
          }
          if (j==order){
            num_ins_t[tid]++;
          }
        }
      }
    }
    TAU_FSTOP(depad_tsr_cnt);

    pre_ins_t[0] = 0;
    for (int j=1; j<ntd; j++){
      pre_ins_t[j] = num_ins_t[j-1] + pre_ins_t[j-1];
    }

    TAU_FSTART(depad_tsr_move);
    #pragma omp parallel
    {
      int64_t i, j, st, end, tid;
      int64_t k;
      int64_t kparts[order];
      tid = omp_get_thread_num();

      st = (num_pair/ntd)*tid;
      if (tid == ntd-1)
        end = num_pair;
      else
        end = (num_pair/ntd)*(tid+1);

      for (i=st; i<end; i++){
        k = pairs[i].k();
        for (j=0; j<order; j++){
          kparts[j] = k%(edge_len[j]+padding[j]);
          if (kparts[j] >= (int64_t)edge_len[j] ||
              kparts[j] < prepadding[j]) break;
          k = k/(edge_len[j]+padding[j]);
        } 
        if (j==order){
          for (j=0; j<order; j++){
            if (sym[j] == SY){
              if (kparts[j+1] < kparts[j])
                break;
            }
            if (sym[j] == AS || sym[j] == SH){
              if (kparts[j+1] <= kparts[j])
                break;
            }
          }
          if (j==order){
            new_pairs[pre_ins_t[tid]].write(pairs[i]);
            pre_ins_t[tid]++;
          }
        }
      }
    }
    TAU_FSTOP(depad_tsr_move);
    num_ins = pre_ins_t[ntd-1];

    *new_num_pair = num_ins;
    CTF_free(pre_ins_t);
    CTF_free(num_ins_t);
#else
    int64_t i, j, num_ins;
    int64_t * kparts;
    int64_t k;

    CTF_alloc_ptr(sizeof(int64_t)*order, (void**)&kparts);

    num_ins = 0;
    for (i=0; i<num_pair; i++){
      k = pairs[i].k();
      for (j=0; j<order; j++){
        kparts[j] = k%(edge_len[j]+padding[j]);
        if (kparts[j] >= (int64_t)edge_len[j] ||
            kparts[j] < prepadding[j]) break;
        k = k/(edge_len[j]+padding[j]);
      } 
      if (j==order){
        for (j=0; j<order; j++){
          if (sym[j] == SY){
            if (kparts[j+1] < kparts[j])
              break;
          }
          if (sym[j] == AS || sym[j] == SH){
            if (kparts[j+1] <= kparts[j])
              break;
          }
        }
        if (j==order){
          new_pairs[num_ins].write(pairs[i]);
          num_ins++;
        }
      }
    }
    *new_num_pair = num_ins;
    CTF_free(kparts);

#endif
    TAU_FSTOP(depad_tsr);
  }


  void pad_tsr(int              order,
               int64_t          size,
               int const *      edge_len,
               int const *      sym,
               int const *      padding,
               int const *      phys_phase,
               int *            virt_phys_rank,
               int const *      virt_phase,
               char const *     old_data,
               char **          new_pairs,
               int64_t *        new_size,
               semiring const & sr){
    int i, imax, act_lda;
    int64_t new_el, pad_el;
    int pad_max, virt_lda, outside, offset, edge_lda;
    int * idx;  
    CTF_alloc_ptr(order*sizeof(int), (void**)&idx);
    char * padded_pairsb;
    
    pad_el = 0;
   
    for (;;){ 
      memset(idx, 0, order*sizeof(int));
      for (;;){
        if (sym[0] != NS)
          pad_max = idx[1]+1;
        else
          pad_max = (edge_len[0]+padding[0])/phys_phase[0];
        pad_el+=pad_max;
        for (act_lda=1; act_lda<order; act_lda++){
          idx[act_lda]++;
          imax = (edge_len[act_lda]+padding[act_lda])/phys_phase[act_lda];
          if (sym[act_lda] != NS)
            imax = idx[act_lda+1]+1;
          if (idx[act_lda] >= imax) 
            idx[act_lda] = 0;
          if (idx[act_lda] != 0) break;      
        }
        if (act_lda == order) break;
  
      }
      for (act_lda=0; act_lda<order; act_lda++){
        virt_phys_rank[act_lda]++;
        if (virt_phys_rank[act_lda]%virt_phase[act_lda]==0)
          virt_phys_rank[act_lda] -= virt_phase[act_lda];
        if (virt_phys_rank[act_lda]%virt_phase[act_lda]!=0) break;      
      }
      if (act_lda == order) break;
    }
    CTF_alloc_ptr(pad_el*(sizeof(int64_t)+sr.el_size), (void**)&padded_pairsb);
    PairIterator padded_pairs = PairIterator(&sr, padded_pairsb);
    new_el   = 0;
    offset   = 0;
    outside  = -1;
    virt_lda = 1;
    for (i=0; i<order; i++){
      offset += virt_phys_rank[i]*virt_lda;
      virt_lda*=(edge_len[i]+padding[i]);
    }
  
    for (;;){
      memset(idx, 0, order*sizeof(int));
      for (;;){
        if (sym[0] != NS){
          if (idx[1] < edge_len[0]/phys_phase[0]) {
            imax = idx[1];
            if (sym[0] != SY && virt_phys_rank[0] < virt_phys_rank[1])
              imax++;
            if (sym[0] == SY && virt_phys_rank[0] <= virt_phys_rank[1])
              imax++;
          } else {
            imax = edge_len[0]/phys_phase[0];
            if (virt_phys_rank[0] < edge_len[0]%phys_phase[0])
              imax++;
          }
          pad_max = idx[1]+1;
        } else {
          imax = edge_len[0]/phys_phase[0];
          if (virt_phys_rank[0] < edge_len[0]%phys_phase[0])
            imax++;
          pad_max = (edge_len[0]+padding[0])/phys_phase[0];
        }
        if (outside == -1){
          for (i=0; i<pad_max-imax; i++){
            padded_pairs[new_el+i].write_key(offset + (imax+i)*phys_phase[0]);
            padded_pairs[new_el+i].write_val(sr.addid);
          }
          new_el+=pad_max-imax;
        }  else {
          for (i=0; i<pad_max; i++){
            padded_pairs[new_el+i].write_key(offset + i*phys_phase[0]);
            padded_pairs[new_el+i].write_val(sr.addid);
          }
          new_el += pad_max;
        }
  
        edge_lda = edge_len[0]+padding[0];
        for (act_lda=1; act_lda<order; act_lda++){
          offset -= idx[act_lda]*edge_lda*phys_phase[act_lda];
          idx[act_lda]++;
          imax = (edge_len[act_lda]+padding[act_lda])/phys_phase[act_lda];
          if (sym[act_lda] != NS && idx[act_lda+1]+1 <= imax){
            imax = idx[act_lda+1]+1;
        //    if (virt_phys_rank[act_lda] < virt_phys_rank[sym[act_lda]])
        //      imax++;
          } 
          if (idx[act_lda] >= imax)
            idx[act_lda] = 0;
          offset += idx[act_lda]*edge_lda*phys_phase[act_lda];
          if (idx[act_lda] > edge_len[act_lda]/phys_phase[act_lda] ||
              (idx[act_lda] == edge_len[act_lda]/phys_phase[act_lda] &&
              (edge_len[act_lda]%phys_phase[act_lda] <= virt_phys_rank[act_lda]))){
            if (outside < act_lda)
              outside = act_lda;
          } else {
            if (outside == act_lda)
              outside = -1;
          }
          if (sym[act_lda] != NS && idx[act_lda] == idx[act_lda+1]){
            if (sym[act_lda] != SY && 
                virt_phys_rank[act_lda] >= virt_phys_rank[act_lda+1]){
              if (outside < act_lda)
                outside = act_lda;
            } 
            if (sym[act_lda] == SY && 
                virt_phys_rank[act_lda] > virt_phys_rank[act_lda+1]){
              if (outside < act_lda)
                outside = act_lda;
            } 
          }
          if (idx[act_lda] != 0) break;      
          edge_lda*=(edge_len[act_lda]+padding[act_lda]);
        }
        if (act_lda == order) break;
  
      }
      virt_lda = 1;
      for (act_lda=0; act_lda<order; act_lda++){
        offset -= virt_phys_rank[act_lda]*virt_lda;
        virt_phys_rank[act_lda]++;
        if (virt_phys_rank[act_lda]%virt_phase[act_lda]==0)
          virt_phys_rank[act_lda] -= virt_phase[act_lda];
        offset += virt_phys_rank[act_lda]*virt_lda;
        if (virt_phys_rank[act_lda]%virt_phase[act_lda]!=0) break;      
        virt_lda*=(edge_len[act_lda]+padding[act_lda]);
      }
      if (act_lda == order) break;
      
    }
    CTF_free(idx);
    DEBUG_PRINTF("order = %d new_el=" PRId64 ", size = " PRId64 ", pad_el = " PRId64 "\n", order, new_el, size, pad_el);
    ASSERT(new_el + size == pad_el);
    memcpy(padded_pairs[new_el].ptr, old_data,  size*(sizeof(int64_t)+sr.el_size));
    *new_pairs = padded_pairsb;
    *new_size = pad_el;
  }

  void zero_padding(int              order,
                    int64_t          size,
                    int              nvirt,
                    int const *      edge_len,
                    int const *      sym,
                    int const *      padding,
                    int const *      phase,
                    int const *      virt_dim,
                    int const *      cphase_rank,
                    char *           vdata,
                    semiring const & sr){

    if (order == 0) return;
    TAU_FSTART(zero_padding);
    #ifdef USE_OMP
    #pragma omp parallel
    #endif
    {
      int i, act_lda, act_max, buf_offset, curr_idx, sym_idx;
      int is_outside;
      int64_t p;
      int * idx, * virt_rank, * phase_rank, * virt_len;
      char * data;

      CTF_alloc_ptr(order*sizeof(int), (void**)&idx);
      CTF_alloc_ptr(order*sizeof(int), (void**)&virt_rank);
      CTF_alloc_ptr(order*sizeof(int), (void**)&phase_rank);
      CTF_alloc_ptr(order*sizeof(int), (void**)&virt_len);
      for (int dim=0; dim<order; dim++){
        virt_len[dim] = edge_len[dim]/phase[dim];
      }

      memcpy(phase_rank, cphase_rank, order*sizeof(int));
      memset(virt_rank, 0, sizeof(int)*order);

      int tid, ntd, vst, vend;
    #ifdef USE_OMP
      tid = omp_get_thread_num();
      ntd = omp_get_num_threads();
    #else
      tid = 0;
      ntd = 1;
    #endif

      int * st_idx=NULL, * end_idx; 
      int64_t st_index = 0;
      int64_t end_index = size/nvirt;

      if (ntd <= nvirt){
        vst = (nvirt/ntd)*tid;
        vst += MIN(nvirt%ntd,tid);
        vend = vst+(nvirt/ntd);
        if (tid < nvirt % ntd) vend++;
      } else {
        int64_t vrt_sz = size/nvirt;
        int64_t chunk = size/ntd;
        int64_t st_chunk = chunk*tid + MIN(tid,size%ntd);
        int64_t end_chunk = st_chunk+chunk;
        if (tid<(size%ntd))
          end_chunk++;
        vst = st_chunk/vrt_sz;
        vend = end_chunk/vrt_sz;
        if ((end_chunk%vrt_sz) > 0) vend++;
        
        st_index = st_chunk-vst*vrt_sz;
        end_index = end_chunk-(vend-1)*vrt_sz;
      
        CTF_alloc_ptr(order*sizeof(int), (void**)&st_idx);
        CTF_alloc_ptr(order*sizeof(int), (void**)&end_idx);

        int * ssym;
        CTF_alloc_ptr(order*sizeof(int), (void**)&ssym);
        for (int dim=0;dim<order;dim++){
          if (sym[dim] != NS) ssym[dim] = SY;
          else ssym[dim] = NS;
        }
      
        //calculate index with all indices, to properly load balance with symmetry
        //then clip the first index to avoid logic inside the inner loop
        calc_idx_arr(order, virt_len, ssym, st_index, st_idx);
        calc_idx_arr(order, virt_len, ssym, end_index, end_idx);

        CTF_free(ssym);

        if (st_idx[0] != 0){
          st_index -= st_idx[0];
          st_idx[0] = 0;
        }
        if (end_idx[0] != 0){
          end_index += virt_len[0]-end_idx[0];
        }
        CTF_free(end_idx);
      }
      ASSERT(tid != ntd-1 || vend == nvirt);
      for (p=0; p<nvirt; p++){
        if (p>=vst && p<vend){
          int is_sh_pad0 = 0;
          if (((sym[0] == AS || sym[0] == SH) && phase_rank[0] >= phase_rank[1]) ||
              ( sym[0] == SY                  && phase_rank[0] >  phase_rank[1]) ) {
            is_sh_pad0 = 1;
          }
          int pad0 = (padding[0]+phase_rank[0])/phase[0];
          int len0 = virt_len[0]-pad0;
          int plen0 = virt_len[0];
          data = vdata + sr.el_size*p*(size/nvirt);

          if (p==vst && st_index != 0){
            idx[0] = 0;
            memcpy(idx+1,st_idx+1,(order-1)*sizeof(int));
            buf_offset = st_index;
          } else {
            buf_offset = 0;
            memset(idx, 0, order*sizeof(int));
          }
          
          for (;;){
            is_outside = 0;
            for (i=1; i<order; i++){
              curr_idx = idx[i]*phase[i]+phase_rank[i];
              if (curr_idx >= edge_len[i] - padding[i]){
                is_outside = 1;
                break;
              } else if (i < order-1) {
                sym_idx   = idx[i+1]*phase[i+1]+phase_rank[i+1];
                if (((sym[i] == AS || sym[i] == SH) && curr_idx >= sym_idx) ||
                    ( sym[i] == SY                  && curr_idx >  sym_idx) ) {
                  is_outside = 1;
                  break;
                }
              }
            }
    /*        for (i=0; i<order; i++){
              printf("phase_rank[%d] = %d, idx[%d] = %d, ",i,phase_rank[i],i,idx[i]);
            }
            printf("\n");
            printf("data["PRId64"]=%lf is_outside = %d\n", buf_offset+p*(size/nvirt), data[buf_offset], is_outside);*/
      
            if (sym[0] != NS) plen0 = idx[1]+1;

            if (is_outside){
    //          std::fill(data+buf_offset, data+buf_offset+plen0, 0.0);
              //for (int64_t j=buf_offset; j<buf_offset+plen0; j++){
              //}
              sr.set(data+buf_offset*sr.el_size, sr.addid, plen0);
            } else {
              int s1 = MIN(plen0-is_sh_pad0,len0);
    /*          if (sym[0] == SH) s1 = MIN(s1, len0-1);*/
    //          std::fill(data+buf_offset+s1, data+buf_offset+plen0, 0.0);
              //for (int64_t j=buf_offset+s1; j<buf_offset+plen0; j++){
              //  data[j] = 0.0;
              //}
              sr.set(data+(buf_offset+s1)*sr.el_size, sr.addid, plen0);
            }
            buf_offset+=plen0;
            if (p == vend-1 && buf_offset >= end_index) break;
            /* Increment indices and set up offsets */
            for (i=1; i < order; i++){
              idx[i]++;
              act_max = virt_len[i];
              if (sym[i] != NS){
    //            sym_idx   = idx[i+1]*phase[i+1]+phase_rank[i+1];
    //            act_max   = MIN(act_max,((sym_idx-phase_rank[i])/phase[i]+1));
                act_max = MIN(act_max,idx[i+1]+1);
              }
              if (idx[i] >= act_max)
                idx[i] = 0;
              ASSERT(edge_len[i]%phase[i] == 0);
              if (idx[i] > 0)
                break;
            }
            if (i >= order) break;
          }
        }
        for (act_lda=0; act_lda < order; act_lda++){
          phase_rank[act_lda] -= virt_rank[act_lda];
          virt_rank[act_lda]++;
          if (virt_rank[act_lda] >= virt_dim[act_lda])
            virt_rank[act_lda] = 0;
          phase_rank[act_lda] += virt_rank[act_lda];
          if (virt_rank[act_lda] > 0)
            break;
        }
      }
      //ASSERT(buf_offset == size/nvirt);
      CTF_free(idx);
      CTF_free(virt_rank);
      CTF_free(virt_len);
      CTF_free(phase_rank);
      if (st_idx != NULL) CTF_free(st_idx);
    }
    TAU_FSTOP(zero_padding);
  }
}
