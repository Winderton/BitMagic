/*
Copyright(c) 2019 Anatoliy Kuznetsov(anatoliy_kuznetsov at yahoo.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

For more information please visit:  http://bitmagic.io
*/


//#define BMSSE2OPT
//#define BMSSE42OPT
//#define BMAVX2OPT

#define BM64ADDR

#include <stdio.h>
#include <stdlib.h>
#undef NDEBUG
#include <cassert>
#include <time.h>
#include <math.h>
#include <string.h>

#include <iostream>
#include <iomanip>
#include <utility>
#include <memory>
#include <random>
#include <algorithm>
#include <stdarg.h>
#include <vector>


#include <bm.h>
#include <bmdbg.h>

using namespace bm;
using namespace std;


#include "gena.h"
#include "test_util.h"


#define POOL_SIZE 5000

//#define MEM_POOL


template<class T> T* pool_allocate(T** pool, int& i, size_t n)
{
    return i ? pool[i--] : (T*) ::malloc(n * sizeof(T));
}

inline void* pool_allocate2(void** pool, int& i, size_t n)
{
    return i ? pool[i--] : malloc(n * sizeof(void*));
}



template<class T> void pool_free(T** pool, int& i, T* p)
{
    i < POOL_SIZE ? (free(p),(void*)0) : pool[++i]=p;
}


class pool_block_allocator
{
public:

    static bm::word_t* allocate(size_t n, const void *)
    {
        int *idx = 0;
        bm::word_t** pool = 0;

        switch (n)
        {
        case bm::set_block_size:
            idx = &bit_blocks_idx_;
            pool = free_bit_blocks_;
            break;

        case 64:
            idx = &gap_blocks_idx0_;
            pool = gap_bit_blocks0_;
            break;

        case 128:
            idx = &gap_blocks_idx1_;
            pool = gap_bit_blocks1_;
            break;
        
        case 256:
            idx = &gap_blocks_idx2_;
            pool = gap_bit_blocks2_;
            break;

        case 512:
            idx = &gap_blocks_idx3_;
            pool = gap_bit_blocks3_;
            break;

        default:
            assert(0);
        }

        return pool_allocate(pool, *idx, n);
    }

    static void deallocate(bm::word_t* p, size_t n)
    {
        int *idx = 0;
        bm::word_t** pool = 0;

        switch (n)
        {
        case bm::set_block_size:
            idx = &bit_blocks_idx_;
            pool = free_bit_blocks_;
            break;

        case 64:
            idx = &gap_blocks_idx0_;
            pool = gap_bit_blocks0_;
            break;

        case 128:
            idx = &gap_blocks_idx1_;
            pool = gap_bit_blocks1_;
            break;
        
        case 256:
            idx = &gap_blocks_idx2_;
            pool = gap_bit_blocks2_;
            break;

        case 512:
            idx = &gap_blocks_idx3_;
            pool = gap_bit_blocks3_;
            break;

        default:
            assert(0);
        }

        pool_free(pool, *idx, p);
    }

private:
    static bm::word_t* free_bit_blocks_[];
    static int         bit_blocks_idx_;

    static bm::word_t* gap_bit_blocks0_[];
    static int         gap_blocks_idx0_;

    static bm::word_t* gap_bit_blocks1_[];
    static int         gap_blocks_idx1_;

    static bm::word_t* gap_bit_blocks2_[];
    static int         gap_blocks_idx2_;

    static bm::word_t* gap_bit_blocks3_[];
    static int         gap_blocks_idx3_;
};

bm::word_t* pool_block_allocator::free_bit_blocks_[POOL_SIZE];
int pool_block_allocator::bit_blocks_idx_ = 0;

bm::word_t* pool_block_allocator::gap_bit_blocks0_[POOL_SIZE];
int pool_block_allocator::gap_blocks_idx0_ = 0;

bm::word_t* pool_block_allocator::gap_bit_blocks1_[POOL_SIZE];
int pool_block_allocator::gap_blocks_idx1_ = 0;

bm::word_t* pool_block_allocator::gap_bit_blocks2_[POOL_SIZE];
int pool_block_allocator::gap_blocks_idx2_ = 0;

bm::word_t* pool_block_allocator::gap_bit_blocks3_[POOL_SIZE];
int pool_block_allocator::gap_blocks_idx3_ = 0;




class pool_ptr_allocator
{
public:

    static void* allocate(size_t n, const void *)
    {
        return pool_allocate2(free_ptr_blocks_, ptr_blocks_idx_, n);
    }

    static void deallocate(void* p, size_t)
    {
        pool_free(free_ptr_blocks_, ptr_blocks_idx_, p);
    }

private:
    static void*  free_ptr_blocks_[];
    static int    ptr_blocks_idx_;
};

void* pool_ptr_allocator::free_ptr_blocks_[POOL_SIZE];
int pool_ptr_allocator::ptr_blocks_idx_ = 0;

#if defined(BMSSE2OPT) || defined(BMSSE42OPT) || defined(BMAVX2OPT) || defined(BMAVX512OPT)
#else
# define MEM_DEBUG
#endif

#ifdef MEM_DEBUG


class dbg_block_allocator
{
public:
static size_t na_;
static size_t nf_;

    static bm::word_t* allocate(size_t n, const void *)
    {
        ++na_;
        assert(n);
        bm::word_t* p =
            (bm::word_t*) ::malloc((n+1) * sizeof(bm::word_t));
        if (!p)
        {
            std::cerr << "ERROR Failed allocation!" << endl;
            exit(1);
        }
        *p = (bm::word_t)n;
        return ++p;
    }

    static void deallocate(bm::word_t* p, size_t n)
    {
        ++nf_;
        --p;
        if (*p != n)
        {
            printf("Block memory deallocation ERROR! n = %i (expected %i)\n", (int)n, (int)*p);
            assert(0);
            exit(1);
        }
        ::free(p);
    }

    static size_t balance()
    {
        return nf_ - na_;
    }
};

size_t dbg_block_allocator::na_ = 0;
size_t dbg_block_allocator::nf_ = 0;

class dbg_ptr_allocator
{
public:
static size_t na_;
static size_t nf_;

    static void* allocate(size_t n, const void *)
    {
        ++na_;
        assert(sizeof(size_t) == sizeof(void*));
        void* p = ::malloc((n+1) * sizeof(void*));
        if (!p)
        {
            std::cerr << "ERROR! Failed allocation!" << endl;
            exit(1);
        }
        size_t* s = (size_t*) p;
        *s = n;
        return (void*)++s;
    }

    static void deallocate(void* p, size_t n)
    {
        ++nf_;
        size_t* s = (size_t*) p;
        --s;
        if(*s != n)
        {
            printf("Ptr memory deallocation ERROR!\n");
            assert(0);
            exit(1);
        }
        ::free(s);
    }

    static size_t balance()
    {
        return nf_ - na_;
    }

};

size_t dbg_ptr_allocator::na_ = 0;
size_t dbg_ptr_allocator::nf_ = 0;


typedef mem_alloc<dbg_block_allocator, dbg_ptr_allocator, alloc_pool<dbg_block_allocator, dbg_ptr_allocator> > dbg_alloc;

typedef bm::bvector<dbg_alloc> bvect64;
//typedef bm::bvector_mini<dbg_block_allocator> bvect_mini;

#else

#ifdef MEM_POOL

typedef mem_alloc<pool_block_allocator, pool_ptr_allocator> pool_alloc;
typedef bm::bvector<pool_alloc> bvect64;
//typedef bm::bvector_mini<bm::block_allocator> bvect_mini;


#else

typedef bm::bvector<> bvect64;
//typedef bm::bvector_mini<bm::block_allocator> bvect_mini;

#endif

#endif

typedef std::vector<bm::id64_t> ref_vect;

static
void SyntaxTest()
{
    cout << "------------------------------------ SyntaxTest()" << endl;
    ref_vect vect;
    generate_vect_simpl0(vect);
    {
        bvect64 bv0;

        load_BV_set_ref(bv0, vect);
        compare_BV_set_ref(bv0, vect);

        
        auto idx = vect.size()-1;
        auto v = vect[idx];
        assert(bv0.test(v));
        
        bvect64 bv1(bv0);
        compare_BV_set_ref(bv1, vect);
        
        bvect64 bv2;
        bv2 = bv0;
        compare_BV_set_ref(bv1, vect);
        
        bvect64 bv3(std::move(bv2));
        assert(bv2.none());
        compare_BV_set_ref(bv3, vect);
        
        bvect64 bv4;
        bv4.move_from(bv3);
        assert(bv3.none());
        compare_BV_set_ref(bv4, vect);
        
        bv0 &= bv4;
        compare_BV_set_ref(bv0, vect);
        bv0 |= bv4;
        compare_BV_set_ref(bv0, vect);
        bv0 -= bv4;
        assert(bv0.none());
        bv1 ^= bv4;
        assert(bv1.none());
    }
    
    {
        bvect64 bv0, bv1, bv2;

        load_BV_set_ref(bv0, vect);
        bv1 = bv2 = bv0;
        bool b = (bv1 == bv0);
        assert(b);
        b = (bv1 == bv2);
        assert(b);
     
        {
            bvect64 bv3;
            bv3 = bv1 | bv2;
            b = (bv1 == bv3);
            assert(b);
            compare_BV_set_ref(bv3, vect);
        }
        {
            bvect64 bv3;
            bv3 = bv1 & bv2;
            b = (bv1 == bv3);
            assert(b);
            compare_BV_set_ref(bv3, vect);
        }
        {
            bvect64 bv3;
            bv3 = bv1 - bv2;
            assert(bv3.none());
        }
        {
            bvect64 bv3;
            bv3 = bv1 ^ bv2;
            assert(bv3.count() == 0ULL);
        }
    }
    
    {
        bvect64 bv1;
        bvect64::reference ref = bv1[10];
        bool bn = !ref;
        assert(bn);
        bool bn2 = ~ref;
        assert(bn2);
        bv1[10] = bn2;
        bv1[10] = bn;
        bn = bn2 = false;
        ref.flip();
        assert(!bv1.test(10));
        bv1[bm::id_max-1] = 1;
        assert(bv1[bm::id_max-1]);
        auto ref1 = bv1[bm::id_max-1];
        ref1.flip();
        assert(!ref1);
        assert(!bv1.test(bm::id_max-1));
    }
    {
        bvect64 bv1;
        auto ii = bv1.inserter();
        ii = bm::id_max / 2;
        assert(bv1.test(bm::id_max / 2));
    }
    
    {
        bvect64 bv1 {0, 10, 31, 32, 62, 63,
             (5 * bm::bits_in_array), (5 * bm::bits_in_array)+1,
             bm::id_max32-1, bm::id_max32, bm::id64_t(bm::id_max32)+1,
             bm::id_max48-1
            };
        compare_BV(bv1, vect);
    }
    cout << "------------------------------------ SyntaxTest() OK" << endl;
}

static
void GenericBVectorTest()
{
    cout << "------------------------------------ GenericBVectorTest()" << endl;
    
    {
        bvect64 bv0;
        ref_vect vect;
        generate_vect_simpl0(vect);
        
        load_BV_set_ref(bv0, vect);
        compare_BV_set_ref(bv0, vect);

        bvect64 bv1(bm::BM_GAP);
        load_BV_set_ref(bv1, vect);
        compare_BV_set_ref(bv1, vect);
        
        int cmp = bv0.compare(bv1);
        assert(cmp == 0);
        
        bvect64::statistics st1, st2, st3;
        bv0.calc_stat(&st1);
        assert(st1.ptr_sub_blocks == 5);
        bv0.optimize(0, bvect64::opt_compress, &st2);
        assert(st1.ptr_sub_blocks == st2.ptr_sub_blocks);
        bv1.calc_stat(&st3);
        assert(st1.ptr_sub_blocks == st3.ptr_sub_blocks);
        assert(st2.gap_blocks == st3.gap_blocks);
    }

    cout << "------------------------------------ GenericBVectorTest() OK" << endl;
}


int main(void)
{
    time_t      start_time = time(0);
    time_t      finish_time;
    
    // -----------------------------------------------------------------
    
    SyntaxTest();
    GenericBVectorTest();
    
    // -----------------------------------------------------------------

    finish_time = time(0);
    cout << "Test execution time = " << finish_time - start_time << endl;

#ifdef MEM_DEBUG
    cout << "[--------------  Allocation digest -------------------]" << endl;
    cout << "Number of BLOCK allocations = " <<  dbg_block_allocator::na_ << endl;
    cout << "Number of PTR allocations = " <<  dbg_ptr_allocator::na_ << endl << endl;

    if(dbg_block_allocator::balance() != 0)
    {
        cout << "ERROR! Block memory leak! " << endl;
        cout << dbg_block_allocator::balance() << endl;
        exit(1);
    }

    if(dbg_ptr_allocator::balance() != 0)
    {
        cout << "ERROR! Ptr memory leak! " << endl;
        cout << dbg_ptr_allocator::balance() << endl;
        exit(1);
    }
    cout << "[------------  Debug Allocation balance OK ----------]" << endl;
#endif

    return 0;

}
