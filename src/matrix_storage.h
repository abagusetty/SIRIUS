// Copyright (c) 2013-2016 Anton Kozhevnikov, Thomas Schulthess
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are permitted provided that 
// the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the 
//    following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions 
//    and the following disclaimer in the documentation and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED 
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR 
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/** \file matrix_storage.h
 *
 *  \brief Contains definition and implementaiton of matrix_storage class.
 */

#ifndef __MATRIX_STORAGE_H__
#define __MATRIX_STORAGE_H__

#include "gvec.h"

namespace sirius {

enum class matrix_storage_t
{
    fft_slab,
    block_cyclic,
    slab
};

template <typename T, matrix_storage_t kind>
class matrix_storage;

template <typename T>
class matrix_storage<T, matrix_storage_t::fft_slab> 
{
    private:

        /// Type of processing unit used.
        device_t pu_;
        
        /// Local number of rows.
        int num_rows_loc_;

        /// Total number of columns.
        int num_cols_;

        /// Primary storage of matrix.
        mdarray<T, 2> prime_;

        /// Auxiliary matrix storage.
        /** This distribution is used by the FFT driver */
        mdarray<T, 2> extra_;

        /// Raw buffer for the extra storage.
        mdarray<T, 1> extra_buf_;

        /// Raw send-recieve buffer.
        mdarray<T, 1> send_recv_buf_;

        /// Column distribution in auxiliary matrix.
        splindex<block> spl_num_col_;

    public:

        matrix_storage(int num_rows_loc__,
                       int num_cols__,
                       device_t pu__)
            : pu_(pu__),
              num_rows_loc_(num_rows_loc__),
              num_cols_(num_cols__)
        {
            PROFILE();
            /* primary storage of PW wave functions: slabs */ 
            prime_ = mdarray<T, 2>(num_rows_loc_, num_cols_, memory_t::host, "matrix_storage.prime_");
        }

        inline void set_num_extra(int n__,
                                  Gvec_partition const& gvec_distr__,
                                  Communicator const& comm_col__,
                                  int idx0__ = 0)
        {
            /* this is how n columns of the matrix will be distributed between columns of the MPI grid */
            spl_num_col_ = splindex<block>(n__, comm_col__.size(), comm_col__.rank());
            
            /* trivial case */
            if (comm_col__.size() == 1) {
                if (pu_ == GPU && prime_.on_device()) {
                    extra_ = mdarray<T, 2>(prime_.template at<CPU>(0, idx0__),
                                           prime_.template at<GPU>(0, idx0__),
                                           num_rows_loc_,
                                           n__);
                } else {
                    extra_ = mdarray<T, 2>(prime_.template at<CPU>(0, idx0__),
                                           num_rows_loc_,
                                           n__);
                }
            } else {
                /* maximum local number of matrix columns */
                int max_n_loc = splindex_base<int>::block_size(n__, comm_col__.size());
                /* upper limit for the size of swapped extra matrix */
                size_t sz = gvec_distr__.gvec_count_fft() * max_n_loc;
                /* reallocate buffers if necessary */
                if (extra_buf_.size() < sz) {
                    extra_buf_ = mdarray<T, 1>(sz);
                    send_recv_buf_ = mdarray<T, 1>(sz, memory_t::host, "send_recv_buf_");
                }
                extra_ = mdarray<T, 2>(extra_buf_.template at<CPU>(), gvec_distr__.gvec_count_fft(), max_n_loc);
            }
        }

        inline void remap_forward(int idx0__,
                                  int n__,
                                  Gvec_partition const& gvec__,
                                  Communicator const& comm_col__)
        {
            PROFILE_WITH_TIMER("sirius::matrix_storage::remap_forward");

            set_num_extra(n__, gvec__, comm_col__, idx0__);

            /* trivial case */
            if (comm_col__.size() == 1) {
                return;
            }
            
            /* local number of columns */
            int n_loc = spl_num_col_.local_size();
            
            /* send and recieve dimensions */
            block_data_descriptor sd(comm_col__.size()), rd(comm_col__.size());
            for (int j = 0; j < comm_col__.size(); j++) {
                sd.counts[j] = spl_num_col_.local_size(j)                 * gvec__.gvec_fft_slab().counts[comm_col__.rank()];
                rd.counts[j] = spl_num_col_.local_size(comm_col__.rank()) * gvec__.gvec_fft_slab().counts[j];
            }
            sd.calc_offsets();
            rd.calc_offsets();

            comm_col__.alltoall(prime_.template at<CPU>(0, idx0__), sd.counts.data(), sd.offsets.data(),
                                send_recv_buf_.template at<CPU>(), rd.counts.data(), rd.offsets.data());
                              
            /* reorder recieved blocks */
            #pragma omp parallel for
            for (int i = 0; i < n_loc; i++) {
                for (int j = 0; j < comm_col__.size(); j++) {
                    int offset = gvec__.gvec_fft_slab().offsets[j];
                    int count  = gvec__.gvec_fft_slab().counts[j];
                    std::memcpy(&extra_(offset, i), &send_recv_buf_[offset * n_loc + count * i], count * sizeof(T));
                }
            }
        }

        inline void remap_backward(int idx0__,
                                   int n__,
                                   Gvec_partition const& gvec__,
                                   Communicator const& comm_col__)
        {
            PROFILE_WITH_TIMER("sirius::matrix_storage::remap_backward");

            if (comm_col__.size() == 1) {
                return;
            }

            assert(n__ == spl_num_col_.global_index_size());

            /* this is how n wave-functions are distributed between column ranks */
            splindex<block> spl_n(n__, comm_col__.size(), comm_col__.rank());
            /* local number of columns */
            int n_loc = spl_n.local_size();

            /* reorder sending blocks */
            #pragma omp parallel for
            for (int i = 0; i < n_loc; i++) {
                for (int j = 0; j < comm_col__.size(); j++) {
                    int offset = gvec__.gvec_fft_slab().offsets[j];
                    int count  = gvec__.gvec_fft_slab().counts[j];
                    std::memcpy(&send_recv_buf_[offset * n_loc + count * i], &extra_(offset, i), count * sizeof(T));
                }
            }

            /* send and recieve dimensions */
            block_data_descriptor sd(comm_col__.size()), rd(comm_col__.size());
            for (int j = 0; j < comm_col__.size(); j++) {
                sd.counts[j] = spl_n.local_size(comm_col__.rank()) * gvec__.gvec_fft_slab().counts[j];
                rd.counts[j] = spl_n.local_size(j)                 * gvec__.gvec_fft_slab().counts[comm_col__.rank()];
            }
            sd.calc_offsets();
            rd.calc_offsets();

            comm_col__.alltoall(send_recv_buf_.template at<CPU>(), sd.counts.data(), sd.offsets.data(),
                                prime_.template at<CPU>(0, idx0__), rd.counts.data(), rd.offsets.data());
        }

        inline T& prime(int irow__, int icol__)
        {
            return prime_(irow__, icol__);
        }

        inline T const& prime(int irow__, int icol__) const
        {
            return prime_(irow__, icol__);
        }

        mdarray<T, 2>& prime()
        {
            return prime_;
        }

        mdarray<T, 2> const& prime() const
        {
            return prime_;
        }

        mdarray<T, 2>& extra()
        {
            return extra_;
        }

        mdarray<T, 2> const& extra() const
        {
            return extra_;
        }
        
        /// Local number of rows in prime matrix.
        inline int num_rows_loc() const
        {
            return num_rows_loc_;
        }

        inline splindex<block> const& spl_num_col() const
        {
            return spl_num_col_;
        }

        //#ifdef __GPU
        //void allocate_on_device()
        //{
        //    wf_coeffs_.allocate(memory_t::device);
        //}

        //void deallocate_on_device()
        //{
        //    wf_coeffs_.deallocate_on_device();
        //}

        //void copy_to_device(int i0__, int n__)
        //{
        //    acc::copyin(wf_coeffs_.at<GPU>(0, i0__), wf_coeffs_.at<CPU>(0, i0__), n__ * num_gvec_loc());
        //}

        //void copy_to_host(int i0__, int n__)
        //{
        //    acc::copyout(wf_coeffs_.at<CPU>(0, i0__), wf_coeffs_.at<GPU>(0, i0__), n__ * num_gvec_loc());
        //}
        //#endif

};

template <typename T>
class matrix_storage<T, matrix_storage_t::block_cyclic> 
{
    private:

        int num_rows_;
        
        int num_cols_;

        int bs_;

        BLACS_grid const& blacs_grid_;

        BLACS_grid const& blacs_grid_slice_;

        dmatrix<T> prime_;

        dmatrix<T> extra_;

        /// Raw buffer for the extra storage.
        mdarray<T, 1> extra_buf_;

        /// Column distribution in auxiliary matrix.
        splindex<block> spl_num_col_;

    public:

        matrix_storage(int num_rows__, int num_cols__, int bs__, BLACS_grid const& blacs_grid__, BLACS_grid const& blacs_grid_slice__)
            : num_rows_(num_rows__),
              num_cols_(num_cols__),
              bs_(bs__),
              blacs_grid_(blacs_grid__),
              blacs_grid_slice_(blacs_grid_slice__)
        {
            assert(blacs_grid_slice__.num_ranks_row() == 1);

            prime_ = dmatrix<T>(num_rows_, num_cols_, blacs_grid_, bs_, bs_);
        }
        
        /// Set extra-storage matrix.
        void set_num_extra(int n__)
        {
            /* this is how n wave-functions will be distributed between panels */
            spl_num_col_ = splindex<block>(n__, blacs_grid_slice_.num_ranks_col(), blacs_grid_slice_.rank_col());

            int bs = splindex_base<int>::block_size(n__, blacs_grid_slice_.num_ranks_col());
            if (blacs_grid_.comm().size() > 1) {
                size_t sz = num_rows_ * bs;
                if (extra_buf_.size() < sz) {
                    extra_buf_ = mdarray<T, 1>(sz);
                }
                extra_ = dmatrix<T>(&extra_buf_[0], num_rows_, n__, blacs_grid_slice_, 1, bs);
            } else {
                extra_ = dmatrix<T>(prime_.template at<CPU>(), num_rows_, n__, blacs_grid_slice_, 1, bs);
            }
        }

        void remap_forward(int idx0__, int n__)
        {
            PROFILE_WITH_TIMER("sirius::matrix_storage::remap_forward");
            set_num_extra(n__);
            if (blacs_grid_.comm().size() > 1) {
                #ifdef __SCALAPACK
                linalg<CPU>::gemr2d(num_rows_, n__, prime_, 0, idx0__, extra_, 0, 0, blacs_grid_.context());
                #else
                TERMINATE_NO_SCALAPACK
                #endif
            }
        }

        void remap_backward(int idx0__, int n__)
        {
            PROFILE_WITH_TIMER("sirius::matrix_storage::remap_backward");
            if (blacs_grid_.comm().size() > 1) {
                #ifdef __SCALAPACK
                linalg<CPU>::gemr2d(num_rows_, n__, extra_, 0, 0, prime_, 0, idx0__, blacs_grid_.context());
                #else
                TERMINATE_NO_SCALAPACK
                #endif
            }
        }

        dmatrix<double_complex>& prime()
        {
            return prime_;
        }

        dmatrix<double_complex>& extra()
        {
            return extra_;
        }

        inline splindex<block> const& spl_num_col() const
        {
            return spl_num_col_;
        }
};

template <typename T>
class matrix_storage<T, matrix_storage_t::slab> 
{
    private:

        device_t pu_;

        int num_rows_loc_;
        
        int num_cols_;

        /// Primary storage of matrix.
        mdarray<T, 2> prime_;

        /// Auxiliary matrix storage.
        /** This distribution is used by the FFT driver */
        mdarray<T, 2> extra_;

        /// Raw buffer for the extra storage.
        mdarray<T, 1> extra_buf_;

        /// Column distribution in auxiliary matrix.
        splindex<block> spl_num_col_;

    public:

        matrix_storage(int num_rows_loc__,
                       int num_cols__,
                       device_t pu__)
            : pu_(pu__),
              num_rows_loc_(num_rows_loc__),
              num_cols_(num_cols__)
        {
            PROFILE();
            /* primary storage data: slabs */ 
            prime_ = mdarray<T, 2>(num_rows_loc_, num_cols_, memory_t::host, "matrix_storage.prime_");
        }

        inline splindex<block> const& spl_num_col() const
        {
            return spl_num_col_;
        }

        inline int num_rows_loc() const
        {
            return num_rows_loc_;
        }

        mdarray<T, 2>& prime()
        {
            return prime_;
        }

        mdarray<T, 2> const& prime() const
        {
            return prime_;
        }

        inline T& prime(int irow__, int icol__)
        {
            return prime_(irow__, icol__);
        }

        inline T const& prime(int irow__, int icol__) const
        {
            return prime_(irow__, icol__);
        }

        mdarray<T, 2>& extra()
        {
            return extra_;
        }

        mdarray<T, 2> const& extra() const
        {
            return extra_;
        }

        inline void set_num_extra(int num_rows__, Communicator const& comm__, int n__, int idx0__ = 0)
        {
            /* this is how n wave-functions will be distributed between MPI ranks */
            spl_num_col_ = splindex<block>(n__, comm__.size(), comm__.rank());

            /* trivial case */
            if (comm__.size() == 1) {
                assert(num_rows__ == num_rows_loc_);
                if (pu_ == GPU && prime_.on_device()) {
                    extra_ = mdarray<T, 2>(prime_.template at<CPU>(0, idx0__),
                                           prime_.template at<GPU>(0, idx0__),
                                           num_rows__,
                                           n__);
                } else {
                    extra_ = mdarray<T, 2>(prime_.template at<CPU>(0, idx0__),
                                           num_rows__,
                                           n__);
                }
            } else {
                /* maximum local number of matrix columns */
                int max_n_loc = splindex_base<int>::block_size(n__, comm__.size());
                /* upper limit for the size of swapped extra matrix */
                size_t sz = num_rows__ * max_n_loc;
                /* reallocate buffers if necessary */
                if (extra_buf_.size() < sz) {
                    extra_buf_ = mdarray<T, 1>(sz);
                }
                extra_ = mdarray<T, 2>(extra_buf_.template at<CPU>(), num_rows__, max_n_loc);
            }
        }

        inline void remap_forward(int num_rows__, Communicator const& comm__, int n__, int idx0__ = 0)
        {
            set_num_extra(num_rows__, comm__, n__, idx0__);

            /* trivial case */
            if (comm__.size() == 1) {
                return;
            }

        }

        inline void remap_backward(int num_rows__, Communicator const& comm__, int n__, int idx0__ = 0)
        {
            if (comm__.size() > 1) {
                STOP();
            }

        }

};

}

#endif // __MATRIX_STORAGE_H__

