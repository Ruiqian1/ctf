#include "coo.h"
#include "csr.h"
#include "../shared/util.h"
#include "../contraction/ctr_comm.h"

namespace CTF_int {
  int64_t get_coo_size(int64_t nnz, int val_size){
    val_size = std::max(val_size,64*((val_size + 63)/64));
    return nnz*(val_size+sizeof(int)*2)+2*sizeof(int64_t);
  }

  COO_Matrix::COO_Matrix(int64_t nnz, algstrct const * sr){
    int64_t size = get_coo_size(nnz, sr->el_size);
    all_data = (char*)alloc(size);
    ((int64_t*)all_data)[0] = nnz;
    ((int64_t*)all_data)[1] = sr->el_size;
    //printf("all_data %p vals %p\n",all_data,this->vals());
  }

  COO_Matrix::COO_Matrix(char * all_data_){
    all_data = all_data_;
  }
      
  COO_Matrix::COO_Matrix(CSR_Matrix const & csr, algstrct const * sr){
    int64_t nnz = csr.nnz(); 
    int64_t v_sz = csr.val_size(); 
    int const * csr_ja = csr.JA();
    int const * csr_ia = csr.IA();
    char const * csr_vs = csr.vals();

    int64_t size = get_coo_size(nnz, v_sz);
    all_data = (char*)alloc(size);
    ((int64_t*)all_data)[0] = nnz;
    ((int64_t*)all_data)[1] = v_sz;
    
    char * vs = vals();
    int * coo_rs = rows();
    int * coo_cs = cols();

    sr->init_shell(nnz, vs);
  
    sr->csr_to_coo(nnz, csr.nrow(), csr_vs, csr_ja, csr_ia, vs, coo_rs, coo_cs);
  }

  int64_t COO_Matrix::nnz() const {
    return ((int64_t*)all_data)[0];
  }

  int COO_Matrix::val_size() const {
    return ((int64_t*)all_data)[1];
  }

  int64_t COO_Matrix::size() const {
    return get_coo_size(nnz(),val_size());
  }
  
  char * COO_Matrix::vals() const {
    return all_data + 2*sizeof(int64_t);
  }

  int * COO_Matrix::rows() const {
    int64_t n = this->nnz();
    int v_sz = this->val_size();

    return (int*)(all_data + n*v_sz+2*sizeof(int64_t));
  } 

  int * COO_Matrix::cols() const {
    int64_t n = this->nnz();
    int v_sz = ((int64_t*)all_data)[1];

    return (int*)(all_data + n*(v_sz+sizeof(int))+2*sizeof(int64_t));
  } 

  void COO_Matrix::set_data(int64_t nz, int order, int const * lens, int const * rev_ordering, int nrow_idx, char const * tsr_data, algstrct const * sr, int const * phase){
    TAU_FSTART(convert_to_COO);
    ((int64_t*)all_data)[0] = nz;
    ((int64_t*)all_data)[1] = sr->el_size;
    int v_sz = sr->el_size;

    int * rev_ord_lens = (int*)alloc(sizeof(int)*order);
    int * ordering = (int*)alloc(sizeof(int)*order);
    int64_t * lda_col = (int64_t*)alloc(sizeof(int64_t)*(order-nrow_idx));
    int64_t * lda_row = (int64_t*)alloc(sizeof(int64_t)*nrow_idx);

    for (int i=0; i<order; i++){
      ordering[rev_ordering[i]]=i;
    }
    for (int i=0; i<order; i++){
    //  printf("[%d] %d -> %d\n", lens[i], i, ordering[i]);
      rev_ord_lens[ordering[i]] = lens[i]/phase[i];
      if (lens[i]%phase[i] > 0) rev_ord_lens[ordering[i]]++;
    }

    for (int i=0; i<order; i++){
      if (i==0 && i<nrow_idx){
        lda_row[0] = 1;
      }
      if (i>0 && i<nrow_idx){
        lda_row[i] = lda_row[i-1]*rev_ord_lens[i-1];
      }
      if (i==nrow_idx){
        lda_col[0] = 1;
      }
      if (i>nrow_idx){
        lda_col[i-nrow_idx] = lda_col[i-nrow_idx-1]*rev_ord_lens[i-1];
      //  printf("lda_col[%d] = %ld len[%d] = %d\n",i-nrow_idx, lda_col[i-nrow_idx], i, rev_ord_lens[i]);
      }
    }
 
    int * rs = rows();
    int * cs = cols();
    char * vs = vals();

#ifdef USE_OMP
    #pragma omp parallel for
#endif
    for (int64_t i=0; i<nz; i++){
      ConstPairIterator pi(sr, tsr_data);
      int64_t k = pi[i].k();
      cs[i] = 1;
      rs[i] = 1;
      for (int j=0; j<order; j++){
        int64_t kpart = (k%lens[j])/phase[j];
        if (ordering[j] < nrow_idx){
          rs[i] += kpart*lda_row[ordering[j]];
        } else {
          cs[i] += kpart*lda_col[ordering[j]-nrow_idx];
        //  printf("%d %ld %d %d %ld\n",j,kpart,ordering[j],nrow_idx,lda_col[ordering[j]-nrow_idx]);
        }
        k=k/lens[j];
      }
      //printf("k=%ld col = %d row = %d\n", pi[i].k(), cs[i], rs[i]);
      pi[i].read_val(vs+v_sz*i);
      //printf("wrote value at %p v_Sz = %d\n",vs+v_sz*i, v_sz);
      //sr->print(pi[i].d());
      //sr->print(vs+v_sz*i);
    }
    cdealloc(ordering);
    cdealloc(rev_ord_lens);
    cdealloc(lda_col);
    cdealloc(lda_row);
    TAU_FSTOP(convert_to_COO);
  }

  void COO_Matrix::get_data(int64_t nz, int order, int const * lens, int const * rev_ordering, int nrow_idx, char * tsr_data, algstrct const * sr, int const * phase, int const * phase_rank){
    TAU_FSTART(convert_to_COO);
    ASSERT(((int64_t*)all_data)[0] == nz);
    ASSERT(((int64_t*)all_data)[1] == sr->el_size);
    int v_sz = sr->el_size;

    int * rev_ord_lens = (int*)alloc(sizeof(int)*order);
    int * ordering = (int*)alloc(sizeof(int)*order);
    int64_t * lda_col = (int64_t*)alloc(sizeof(int64_t)*(order-nrow_idx));
    int64_t * lda_row = (int64_t*)alloc(sizeof(int64_t)*nrow_idx);

    for (int i=0; i<order; i++){
      ordering[rev_ordering[i]]=i;
    }
    for (int i=0; i<order; i++){
    //  printf("[%d] %d -> %d\n", lens[i], i, ordering[i]);
      rev_ord_lens[ordering[i]] = lens[i]/phase[i];
      if (lens[i]%phase[i] > 0) rev_ord_lens[ordering[i]]++;
    }

    for (int i=0; i<order; i++){
      if (i==0 && i<nrow_idx){
        lda_row[0] = 1;
      }
      if (i>0 && i<nrow_idx){
        lda_row[i] = lda_row[i-1]*rev_ord_lens[i-1];
      }
      if (i==nrow_idx){
        lda_col[0] = 1;
      }
      if (i>nrow_idx){
        lda_col[i-nrow_idx] = lda_col[i-nrow_idx-1]*rev_ord_lens[i-1];
      //  printf("lda_col[%d] = %ld len[%d] = %d\n",i-nrow_idx, lda_col[i-nrow_idx], i, rev_ord_lens[i]);
      }
    }
 
    int * rs = rows();
    int * cs = cols();
    char * vs = vals();

#ifdef USE_OMP
    #pragma omp parallel for
#endif
    for (int64_t i=0; i<nz; i++){
      PairIterator pi(sr, tsr_data);
      int64_t k = 0;
      int64_t lda_k = 1;
      for (int j=0; j<order; j++){
        int64_t kpart;
        if (ordering[j] < nrow_idx){
          kpart = ((rs[i]-1)/lda_row[ordering[j]])%rev_ord_lens[ordering[j]];
        } else {
          kpart = ((cs[i]-1)/lda_col[ordering[j]-nrow_idx])%rev_ord_lens[ordering[j]];
        }
        //  printf("%d %ld %d %d %ld\n",j,kpart,ordering[j],nrow_idx,lda_col[ordering[j]-nrow_idx]);
       // if (j>0){ kpart *= lens[j-1]; }
        k+=(kpart*phase[j]+phase_rank[j])*lda_k;
/*        if (k>=tot_sz){
          printf("%d kpart %ld k1 %d phase[j-1] %d phase_rank[j-1] %d lda_k %ld\n",j-1,((k-(kpart*phase[j]+phase_rank[j])*lda_k)/(lda_k/lens[j-1])-phase_rank[j-1])/phase[j-1],k-(kpart*phase[j]+phase_rank[j])*lda_k,phase[j-1],phase_rank[j-1],lda_k/lens[j-1]);
          printf("%d kpart %ld k2 %ld phase[j] %d phase_rank[j] %d lda_k %ld\n",j,kpart,(kpart*phase[j]+phase_rank[j])*lda_k,phase[j],phase_rank[j],lda_k);
        }*/
        lda_k *= lens[j];
      }
      //ASSERT(k<tot_sz);
//      if (k>=tot_sz) printf("k=%ld tot_sz=%ld c = %d r = %d\n",k,tot_sz,cs[i],rs[i]);
//      printf("p[%d %d] [%d,%d]->%ld\n",phase_rank[0],phase_rank[1],rs[i],cs[i],k);
      pi[i].write_key(k);
      pi[i].write_val(vs+v_sz*i);
      //printf("k=%ld col = %d row = %d\n", pi[i].k(), cs[i], rs[i]);
      //sr->print(pi[i].d());
//      memcpy(pi[i].d(), vs+v_sz*i, v_sz);
    }
    PairIterator pi2(sr, tsr_data);
    TAU_FSTART(COO_to_kvpair_sort);
    pi2.sort(nz);
    TAU_FSTOP(COO_to_kvpair_sort);
    cdealloc(ordering);
    cdealloc(rev_ord_lens);
    cdealloc(lda_col);
    cdealloc(lda_row);
    TAU_FSTOP(convert_to_COO);
  }


  void COO_Matrix::coomm(char const * A, algstrct const * sr_A, int m, int n, int k, char const * alpha, char const * B, algstrct const * sr_B, char const * beta, char * C, algstrct const * sr_C, bivar_function const * func){
    COO_Matrix cA((char*)A);
    int64_t nz = cA.nnz(); 
    int const * rs = cA.rows();
    int const * cs = cA.cols();
    char const * vs = cA.vals();
    if (func != NULL){
      assert(sr_C->isequal(beta, sr_C->mulid()));
      assert(alpha == NULL || sr_C->isequal(alpha, sr_C->mulid()));
      func->ccoomm(m,n,k,vs,rs,cs,nz,B,C);
    } else {
      ASSERT(sr_B->el_size == sr_A->el_size);
      ASSERT(sr_C->el_size == sr_A->el_size);
      sr_A->coomm(m,n,k,alpha,vs,rs,cs,nz,B,beta,C,func);
    }
  }
}
