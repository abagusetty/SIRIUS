// Copyright (c) 2013-2014 Anton Kozhevnikov, Thomas Schulthess
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

/** \file platform.h
 *   
 *  \brief Contains definition and implementation of Platform class.
 */

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <mpi.h>
#include <omp.h>
#include <signal.h>
#include <unistd.h>
#ifdef _GPU_
#include "gpu_interface.h"
#endif
#include <vector>
#include <fstream>
#include "typedefs.h"

/// Platform specific functions.
class Platform
{
    private:

        static int num_fft_threads_;
    
    public:

        static void initialize(bool call_mpi_init, bool call_cublas_init = true);

        static void finalize();

        static int mpi_rank(MPI_Comm comm = MPI_COMM_WORLD);

        static int num_mpi_ranks(MPI_Comm comm = MPI_COMM_WORLD);

        static void abort();

        /// Broadcast array 
        template <typename T>
        static void bcast(T* buffer, int count, const MPI_Comm& comm, int root);
        
        /// Broadcast array 
        template <typename T>
        static void bcast(T* buffer, int count, int root);
       
        /// Perform all-to-one in-place reduction
        template <typename T>
        static void reduce(T* buf, int count, const MPI_Comm& comm, int root);

        /// Perform all-to-one out-of-place reduction 
        template <typename T>
        static void reduce(T* sendbuf, T* recvbuf, int count, const MPI_Comm& comm, int root);
        
        /// Perform the in-place (the output buffer is used as the input buffer) all-to-all reduction 
        template <typename T>
        static void allreduce(T* buffer, int count, const MPI_Comm& comm);

        /// Perform the in-place (the output buffer is used as the input buffer) all-to-all reduction 
        template<mpi_op_t op, typename T>
        static void allreduce(T* buffer, int count, const MPI_Comm& comm);
        
        /// Perform the in-place (the output buffer is used as the input buffer) all-to-all reduction 
        template <typename T>
        static void allreduce(T* buffer, int count);
        
        /// Perform the in-place (the output buffer is used as the input buffer) all-to-all reduction 
        template<mpi_op_t op, typename T>
        static void allreduce(T* buffer, int count);

        template <typename T>
        static void allgather(T* sendbuf, T* recvbuf, int offset, int count);
        
        template <typename T>
        static void allgather(T* buf, int offset, int count, MPI_Comm comm = MPI_COMM_WORLD);

        template <typename T> 
        static void gather(T* sendbuf, T* recvbuf, int *recvcounts, int *displs, int root, MPI_Comm comm);

        template <typename T>
        static void scatter(T* sendbuf, T* recvbuf, int* sendcounts, int* displs, int root, MPI_Comm comm);

        /// Non-blocking send.
        template <typename T>
        static void isend(T* buf, int count, int dest, int tag, MPI_Comm comm);

        /// Blocking recieve.
        template <typename T>
        static void recv(T* buf, int count, int source, int tag, MPI_Comm comm);
        
        /// Returm maximum number of OMP threads.
        /** Maximum number of OMP threads is controlled by environment variable OMP_NUM_THREADS */
        static inline int max_num_threads()
        {
            return omp_get_max_threads();
        }

        /// Returm number of actually running OMP threads. 
        static inline int num_threads()
        {
            return omp_get_num_threads();
        }
        
        /// Return thread id.
        static inline int thread_id()
        {
            return omp_get_thread_num();
        }
        
        /// Return number of threads for independent FFT transformations.
        static inline int num_fft_threads()
        {
            return num_fft_threads_;
        }
        
        /// Set the number of FFT threads
        static inline void set_num_fft_threads(int num_fft_threads__)
        {
            num_fft_threads_ = num_fft_threads__;
        }
        
        /// Global barrier
        static void barrier(MPI_Comm comm = MPI_COMM_WORLD)
        {
            MPI_Barrier(comm);
        }

        //static void write_proc_status(const char* src_file, const int src_line)
        //{
        //    int pid = getpid();
        //    //== 
        //    //== char hostname[1024];
        //    //== gethostname(hostname, 1024);
        //    //== hostname[1023] = 0;

        //    std::stringstream fname;
        //    fname << "/proc/" << pid << "/status";
        //    
        //    std::ifstream ifs(fname.str().c_str());
        //    if (ifs.is_open())
        //    {
        //        printf("[mpi_rank: %i] line %i of file %s \n", mpi_rank(), src_line, src_file);
        //        
        //        std::string str; 
        //        while (std::getline(ifs, str))
        //        {
        //            printf("[mpi_rank: %i] %s\n", mpi_rank(), str.c_str());
        //        } 
        //    }
        //}

        //static void memory_usage_info(const char* src_file, const int src_line)
        //{
        //    int pid = getpid();
        //    std::stringstream fname;
        //    fname << "/proc/" << pid << "/status";
        //    
        //    std::ifstream ifs(fname.str().c_str());
        //    if (ifs.is_open())
        //    {

        //    }

        //}

        static void get_proc_status(size_t* VmHWM, size_t* VmRSS)
        {
            *VmHWM = 0;
            *VmRSS = 0;

            std::stringstream fname;
            fname << "/proc/" << getpid() << "/status";
            
            std::ifstream ifs(fname.str().c_str());
            if (ifs.is_open())
            {
                size_t tmp;
                std::string str; 
                std::string units;
                while (std::getline(ifs, str))
                {
                    auto p = str.find("VmHWM:");
                    if (p != std::string::npos)
                    {
                        std::stringstream s(str.substr(p + 7));
                        s >> tmp;
                        s >> units;
        
                        if (units != "kB")
                        {
                            printf("Platform::get_proc_status(): wrong units");
                            abort();
                        }
                        *VmHWM = tmp * 1024;
                    }
        
                    p = str.find("VmRSS:");
                    if (p != std::string::npos)
                    {
                        std::stringstream s(str.substr(p + 7));
                        s >> tmp;
                        s >> units;
        
                        if (units != "kB")
                        {
                            printf("Platform::get_proc_status(): wrong units");
                            abort();
                        }
                        *VmRSS = tmp * 1024;
                    }
                } 
            }
        
        }
};

#include "platform.hpp"

#endif
