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

/** \file wave_functions.h
 *   
 *  \brief Contains declaration and implementation of sirius::Wave_functions class.
 */

#ifndef __WAVE_FUNCTIONS_H__
#define __WAVE_FUNCTIONS_H__

#include "gvec.h"
#include "mpi_grid.h"
#include "linalg.h"

#include "matrix_storage.h"

namespace sirius {

/// Wave-functions representation.
class wave_functions
{
    private:

        Simulation_parameters const& params_;

        Communicator const& comm_;

        Gvec const& gkvec_;

        Gvec_partition gkvec_full_;

        splindex<block> spl_num_atoms_;

        block_data_descriptor mt_coeffs_distr_;

        std::vector<int> offset_mt_coeffs_;
        
        /// Total number of muffin-tin coefficients.
        int num_mt_coeffs_{0};

        /// Total number of wave-functions.
        int num_wf_{0};

        /// Plane-wave part of wave-functions.
        std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::fft_slab>> pw_coeffs_{nullptr};

        /// Muffin-tin part of wave-functions.
        std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::slab>> mt_coeffs_{nullptr};

    public:
        
        /// Constructor for PW wave-functions.
        wave_functions(Simulation_parameters const& params__,
                       Communicator const& comm__,
                       Gvec const& gkvec__,
                       int num_wf__)
            : params_(params__),
              comm_(comm__),
              gkvec_(gkvec__),
              gkvec_full_(gkvec_, mpi_comm_self()),
              num_wf_(num_wf__)
        {
            pw_coeffs_ = std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::fft_slab>>(
                new matrix_storage<double_complex, matrix_storage_t::fft_slab>(gkvec_.gvec_count(comm_.rank()),
                                                                               num_wf_,
                                                                               params_.processing_unit()));
        }

        /// Constructor for LAPW wave-functions.
        wave_functions(Simulation_parameters const& params__,
                       Communicator const& comm__,
                       Gvec const& gkvec__,
                       int num_atoms__,
                       std::function<int(int)> mt_size__,
                       int num_wf__)
            : params_(params__),
              comm_(comm__),
              gkvec_(gkvec__),
              gkvec_full_(gkvec_, mpi_comm_self()),
              num_wf_(num_wf__)
        {
            pw_coeffs_ = std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::fft_slab>>(
                new matrix_storage<double_complex, matrix_storage_t::fft_slab>(gkvec_.gvec_count(comm_.rank()),
                                                                               num_wf_,
                                                                               params_.processing_unit()));

            spl_num_atoms_ = splindex<block>(num_atoms__, comm_.size(), comm_.rank());
            mt_coeffs_distr_ = block_data_descriptor(comm_.size());
            
            for (int ia = 0; ia < num_atoms__; ia++) {
                int rank = spl_num_atoms_.local_rank(ia);
                if (rank == comm_.rank()) {
                    offset_mt_coeffs_.push_back(mt_coeffs_distr_.counts[rank]);
                }
                mt_coeffs_distr_.counts[rank] += mt_size__(ia);
                
            }
            mt_coeffs_distr_.calc_offsets();

            num_mt_coeffs_ = mt_coeffs_distr_.offsets.back() + mt_coeffs_distr_.counts.back();
            
            mt_coeffs_ = std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::slab>>(
                new matrix_storage<double_complex, matrix_storage_t::slab>(mt_coeffs_distr_.counts[comm_.rank()],
                                                                           num_wf_,
                                                                           params_.processing_unit()));
        }

        inline matrix_storage<double_complex, matrix_storage_t::fft_slab>& pw_coeffs()
        {
            return *pw_coeffs_;
        }

        inline matrix_storage<double_complex, matrix_storage_t::fft_slab> const& pw_coeffs() const
        {
            return *pw_coeffs_;
        }

        inline matrix_storage<double_complex, matrix_storage_t::slab>& mt_coeffs()
        {
            return *mt_coeffs_;
        }

        inline matrix_storage<double_complex, matrix_storage_t::slab> const& mt_coeffs() const
        {
            return *mt_coeffs_;
        }

        inline Simulation_parameters const& params() const
        {
            return params_;
        }

        inline splindex<block> const& spl_num_atoms() const
        {
            return spl_num_atoms_;
        }

        inline int offset_mt_coeffs(int ialoc__) const
        {
            return offset_mt_coeffs_[ialoc__];
        }

        inline void copy_from(wave_functions const& src__,
                              int i0__,
                              int n__,
                              int j0__)
        {
            switch (params_.processing_unit()) {
                case CPU: {
                    std::memcpy(pw_coeffs().prime().at<CPU>(0, j0__),
                                src__.pw_coeffs().prime().at<CPU>(0, i0__),
                                pw_coeffs().num_rows_loc() * n__ * sizeof(double_complex));
                    if (params_.full_potential()) {
                        std::memcpy(mt_coeffs().prime().at<CPU>(0, j0__),
                                    src__.mt_coeffs().prime().at<CPU>(0, i0__),
                                    mt_coeffs().num_rows_loc() * n__ * sizeof(double_complex));
                    }
                    break;
                }
                case GPU: {
                    #ifdef __GPU
                    acc::copy(wf_coeffs_.at<GPU>(0, j0__), src__.wf_coeffs_.at<GPU>(0, i0__), num_gvec_loc_ * n__);
                    if (params_.full_potential()) {
                        acc::copy(&mt_coeffs().prime_(0, j0__), &src__.mt_coeffs().prime_(0, i0__), mt_coeffs().num_rows_loc() * n__ * sizeof(double_complex));
                    }
                    #endif
                    break;
                }
            }
        }
        
        inline void copy_from(wave_functions const& src__, int i0__, int n__)
        {
            copy_from(src__, i0__, n__, i0__);
        }

        inline void prepare_full_column_distr(int n__)
        {
            pw_coeffs().set_num_extra(n__, gkvec_full_, comm_);
            if (params_.full_potential()) {
                mt_coeffs().set_num_extra(num_mt_coeffs_, comm_, n__);
            }
        }

        inline void remap_to_full_column_distr(int n__)
        {
            pw_coeffs().remap_forward(0, n__, gkvec_full_, comm_);
            if (params_.full_potential()) {
                mt_coeffs().remap_forward(num_mt_coeffs_, comm_, n__);
            }
        }

        inline void remap_to_prime_distr(int n__)
        {
            pw_coeffs().remap_backward(0, n__, gkvec_full_, comm_);
            if (params_.full_potential()) {
                mt_coeffs().remap_backward(num_mt_coeffs_, comm_, n__);
            }
        }

        /// Compute L2 norm of first n wave-functions.
        inline mdarray<double,1> l2norm(int n__) const
        {
            mdarray<double, 1> norm(n__);
            norm.zero();
            
            #pragma omp parallel for
            for (int i = 0; i < n__; i++) {
                for (int ig = 0; ig < pw_coeffs().num_rows_loc(); ig++) {
                    norm[i] += (std::pow(pw_coeffs().prime(ig, i).real(), 2) + std::pow(pw_coeffs().prime(ig, i).imag(), 2));
                }
                if (gkvec_.reduced()) {
                    if (comm_.rank() == 0) {
                        norm[i] = 2 * norm[i] - std::pow(pw_coeffs().prime(0, i).real(), 2);
                    } else {
                        norm[i] *= 2;
                    }
                }
                if (params_.full_potential() && mt_coeffs().num_rows_loc()) {
                    for (int j = 0; j < mt_coeffs().num_rows_loc(); j++) {
                        norm[i] += (std::pow(mt_coeffs().prime(j, i).real(), 2) + std::pow(mt_coeffs().prime(j, i).imag(), 2));
                    }
                }
            }
            comm_.allreduce(norm.at<CPU>(), n__);
            for (int i = 0; i < n__; i++) {
                norm[i] = std::sqrt(norm[i]);
            }

            return std::move(norm);
        }

        Communicator const& comm() const
        {
            return comm_;
        }

        template <typename T>
        inline void transform_from(wave_functions& wf__,
                                   int nwf__,
                                   matrix<T>& mtrx__,
                                   int n__);

        inline void transform_from(wave_functions& wf__,
                                   int nwf__,
                                   dmatrix<double_complex>& mtrx__,
                                   int irow0__,
                                   int n__);
};

template<>
inline void wave_functions::transform_from<double_complex>(wave_functions& wf__,
                                                           int nwf__,
                                                           matrix<double_complex>& mtrx__,
                                                           int n__)
{
    assert(&params_ == &wf__.params());
    assert(pw_coeffs().num_rows_loc() == wf__.pw_coeffs().num_rows_loc());
    if (params_.full_potential()) {
        assert(mt_coeffs().num_rows_loc() == wf__.mt_coeffs().num_rows_loc());
    }

    if (params_.processing_unit() == CPU) {
        linalg<CPU>::gemm(0, 0, pw_coeffs().num_rows_loc(), n__, nwf__,
                          wf__.pw_coeffs().prime().at<CPU>(), wf__.pw_coeffs().prime().ld(),
                          mtrx__.at<CPU>(), mtrx__.ld(),
                          pw_coeffs().prime().at<CPU>(), pw_coeffs().prime().ld());
        if (params_.full_potential() && mt_coeffs().num_rows_loc()) {
            linalg<CPU>::gemm(0, 0, mt_coeffs().num_rows_loc(), n__, nwf__,
                              wf__.mt_coeffs().prime().at<CPU>(), wf__.mt_coeffs().prime().ld(),
                              mtrx__.at<CPU>(), mtrx__.ld(),
                              mt_coeffs().prime().at<CPU>(), mt_coeffs().prime().ld());
        }
    }
    #ifdef __GPU
    if (pu_ == GPU) {
        linalg<GPU>::gemm(0, 0, num_rows_loc(), n__, nwf__, wf__.prime().at<GPU>(), wf__.prime().ld(),
                          mtrx__.at<GPU>(), mtrx__.ld(), prime_.at<GPU>(), prime_.ld());
    }
    #endif
}

template<>
inline void wave_functions::transform_from<double>(wave_functions& wf__,
                                                   int nwf__,
                                                   matrix<double>& mtrx__,
                                                   int n__)
{
    assert(&params_ == &wf__.params());
    assert(pw_coeffs().num_rows_loc() == wf__.pw_coeffs().num_rows_loc());
    if (params_.full_potential()) {
        assert(mt_coeffs().num_rows_loc() == wf__.mt_coeffs().num_rows_loc());
    }

    if (params_.processing_unit() == CPU) {
        linalg<CPU>::gemm(0, 0, 2 * pw_coeffs().num_rows_loc(), n__, nwf__,
                          (double*)wf__.pw_coeffs().prime().at<CPU>(), 2 * wf__.pw_coeffs().prime().ld(),
                          mtrx__.at<CPU>(), mtrx__.ld(),
                          (double*)pw_coeffs().prime().at<CPU>(), 2 * pw_coeffs().prime().ld());
        if (params_.full_potential()) {
            TERMINATE_NOT_IMPLEMENTED;
        }
    }
    #ifdef __GPU
    if (pu_ == GPU) {
        linalg<GPU>::gemm(0, 0, 2 * num_rows_loc(), n__, nwf__, (double*)wf__.prime().at<GPU>(), 2 * wf__.prime().ld(),
                          mtrx__.at<GPU>(), mtrx__.ld(), (double*)prime_.at<GPU>(), 2 * prime_.ld());
    }
    #endif
}

inline void wave_functions::transform_from(wave_functions& wf__,
                           int nwf__,
                           dmatrix<double_complex>& mtrx__,
                           int irow0__,
                           int n__)
{
    matrix<double_complex> z(nwf__, n__);
    z.zero();

    for (int icol = 0; icol < n__; icol++) {
        auto location_col = mtrx__.spl_col().location(icol);
        if (location_col.second == mtrx__.rank_col()) {
            for (int irow = 0; irow < nwf__; irow++) {
                auto location_row = mtrx__.spl_row().location(irow0__ + irow);
                if (location_row.second == mtrx__.rank_row()) {
                    z(irow, icol) = mtrx__(location_row.first, location_col.first);
                }
            }
        }
    }
    mtrx__.blacs_grid().comm().allreduce(z.at<CPU>(), static_cast<int>(z.size()));

    this->transform_from<double_complex>(wf__, nwf__, z, n__);
}

inline mdarray<double, 1>& inner_prod_buf(size_t new_size__)
{
    static mdarray<double, 1> buf;
    if (new_size__ > buf.size()) {
        buf = mdarray<double, 1>(new_size__);
    }
    return buf;
}

inline void inner(wave_functions& bra__,
                  int i0__,
                  int m__,
                  wave_functions& ket__,
                  int j0__,
                  int n__,
                  mdarray<double_complex, 2>& result__,
                  int irow__,
                  int icol__)
{
    PROFILE_WITH_TIMER("sirius::wave_functions::inner");
    
    assert(&bra__.params() == &ket__.params());
    assert(&bra__.comm() == &ket__.comm());
    assert(bra__.pw_coeffs().num_rows_loc() == ket__.pw_coeffs().num_rows_loc());
    if (bra__.params().full_potential()) {
        assert(bra__.mt_coeffs().num_rows_loc() == ket__.mt_coeffs().num_rows_loc());
    }

    auto& comm = bra__.comm();
    auto pu = bra__.params().processing_unit();

    /* single rank, CPU: store result directly in the output matrix */
    if (comm.size() == 1 && pu == CPU) {
        linalg<CPU>::gemm(2, 0, m__, n__, bra__.pw_coeffs().num_rows_loc(),
                          bra__.pw_coeffs().prime().at<CPU>(0, i0__), bra__.pw_coeffs().prime().ld(),
                          ket__.pw_coeffs().prime().at<CPU>(0, j0__), ket__.pw_coeffs().prime().ld(),
                          result__.at<CPU>(irow__, icol__), result__.ld());
        if (bra__.params().full_potential() && bra__.mt_coeffs().num_rows_loc()) {
            double_complex alpha(1, 0);
            linalg<CPU>::gemm(2, 0, m__, n__, bra__.mt_coeffs().num_rows_loc(),
                              alpha,
                              bra__.mt_coeffs().prime().at<CPU>(0, i0__), bra__.mt_coeffs().prime().ld(),
                              ket__.mt_coeffs().prime().at<CPU>(0, j0__), ket__.mt_coeffs().prime().ld(),
                              alpha,
                              result__.at<CPU>(irow__, icol__), result__.ld());
        }
    } else {
        auto& buf = inner_prod_buf(2 * m__ * n__);
        switch (pu) {
            case CPU: {
                linalg<CPU>::gemm(2, 0, m__, n__, bra__.pw_coeffs().num_rows_loc(),
                                  bra__.pw_coeffs().prime().at<CPU>(0, i0__), bra__.pw_coeffs().prime().ld(),
                                  ket__.pw_coeffs().prime().at<CPU>(0, j0__), ket__.pw_coeffs().prime().ld(),
                                  (double_complex*)buf.at<CPU>(), m__);
                if (bra__.params().full_potential() && bra__.mt_coeffs().num_rows_loc()) {
                    double_complex alpha(1, 0);
                    linalg<CPU>::gemm(2, 0, m__, n__, bra__.mt_coeffs().num_rows_loc(),
                                      alpha,
                                      bra__.mt_coeffs().prime().at<CPU>(0, i0__), bra__.mt_coeffs().prime().ld(),
                                      ket__.mt_coeffs().prime().at<CPU>(0, j0__), ket__.mt_coeffs().prime().ld(),
                                      alpha,
                                      (double_complex*)buf.at<CPU>(), m__);
                }
                break;
            }
            case GPU: {
                #ifdef __GPU
                buf.allocate(memory_t::device);
                linalg<GPU>::gemm(2, 0, m__, n__, bra__.num_rows_loc(),
                                  bra__.prime().at<GPU>(0, i0__), bra__.prime().ld(),
                                  ket__.prime().at<GPU>(0, j0__), ket__.prime().ld(),
                                  (double_complex*)buf.at<GPU>(), m__);
                buf.copy_to_host(2 * m__ * n__);
                buf.deallocate_on_device();
                #else
                TERMINATE_NO_GPU
                #endif
                break;
            }
        }

        comm.allreduce(&buf[0], 2 * m__ * n__);

        for (int i = 0; i < n__; i++) {
            std::memcpy(&result__(irow__, icol__ + i), &buf[2 * i * m__], m__ * sizeof(double_complex));
        }
    }
}

inline void inner(wave_functions& bra__,
                  int i0__,
                  int m__,
                  wave_functions& ket__,
                  int j0__,
                  int n__,
                  mdarray<double, 2>& result__,
                  int irow__,
                  int icol__)
{
    PROFILE_WITH_TIMER("sirius::wave_functions::inner");

    assert(&bra__.params() == &ket__.params());
    assert(&bra__.comm() == &ket__.comm());
    assert(bra__.pw_coeffs().num_rows_loc() == ket__.pw_coeffs().num_rows_loc());
    if (bra__.params().full_potential()) {
        TERMINATE_NOT_IMPLEMENTED;
    }

    auto& comm = bra__.comm();
    auto pu = bra__.params().processing_unit();

    /* single rank, CPU: store result directly in the output matrix */
    if (comm.size() == 1 && pu == CPU) {
        linalg<CPU>::gemm(2, 0, m__, n__, bra__.pw_coeffs().num_rows_loc(),
                          (double*)bra__.pw_coeffs().prime().at<CPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                          (double*)ket__.pw_coeffs().prime().at<CPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                          result__.at<CPU>(irow__, icol__), result__.ld());
        
        for (int j = 0; j < n__; j++) {
            for (int i = 0; i < m__; i++) {
                result__(irow__ + i, icol__ + j) = 2 * result__(irow__ + i, icol__ + j) -
                                                   bra__.pw_coeffs().prime(0, i0__ + i).real() * ket__.pw_coeffs().prime(0, j0__ + j).real();
            }
        }
    } else {
        auto& buf = inner_prod_buf(m__ * n__);
        double alpha = 2;
        double beta = 0;
        switch (pu) {
            case CPU: {
                linalg<CPU>::gemm(1, 0, m__, n__, 2 * bra__.pw_coeffs().num_rows_loc(),
                                  alpha,
                                  (double*)bra__.pw_coeffs().prime().at<CPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                                  (double*)ket__.pw_coeffs().prime().at<CPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                                  beta,
                                  buf.at<CPU>(), m__);
                if (comm.rank() == 0) {
                    /* subtract one extra G=0 contribution */
                    linalg<CPU>::ger(m__, n__, -1.0,
                                    (double*)bra__.pw_coeffs().prime().at<CPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                                    (double*)ket__.pw_coeffs().prime().at<CPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                                    buf.at<CPU>(), m__); 
                }
                break;
            }
            case GPU: {
                #ifdef __GPU
                buf.allocate(memory_t::device);
                linalg<GPU>::gemm(1, 0, m__, n__, 2 * bra__.num_rows_loc(),
                                  &alpha,
                                  (double*)bra__.pw_coeffs().prime().at<GPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                                  (double*)ket__.pw_coeffs().prime().at<GPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                                  &beta,
                                  buf.at<GPU>(), m__);
                double alpha1 = -1;
                if (comm.rank() == 0) {
                    /* subtract one extra G=0 contribution */
                    linalg<GPU>::ger(m__, n__, &alpha1,
                                    (double*)bra__.pw_coeffs().prime().at<GPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                                    (double*)ket__.pw_coeffs().prime().at<GPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                                    buf.at<GPU>(), m__); 
                }
                buf.copy_to_host(m__ * n__);
                buf.deallocate_on_device();
                #else
                TERMINATE_NO_GPU
                #endif
                break;
            }
        }

        comm.allreduce(&buf[0], m__ * n__);

        for (int i = 0; i < n__; i++) {
            std::memcpy(&result__(irow__, icol__ + i), &buf[i * m__], m__ * sizeof(double));
        }
    }
}

}

#endif
