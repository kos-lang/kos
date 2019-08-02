/*
 * Copyright (c) 2014-2019 Chris Dragan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef KOS_ATOMIC_H_INCLUDED
#define KOS_ATOMIC_H_INCLUDED

#include <stdint.h>

/* WAR bug in gcc 4.8 */
#if defined(__GNUC__) && __GNUC__ == 4 && __GNUC_MINOR__ == 8 && !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#   define __STDC_NO_ATOMICS__
#endif

/* WAR bug in clang 6.0 */
#if defined(__clang__) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#   if !__has_include(<stdatomic.h>)
#       define __STDC_NO_ATOMICS__
#   endif
#endif

/* Detect C++11 support */
#if defined(__cplusplus)
#   if ! defined(KOS_CPP11)
#       if __cplusplus >= 201103L
#           define KOS_CPP11 1
#       elif defined(_MSC_VER) && _MSC_VER >= 1900
#           define KOS_CPP11 1
#       endif
#   endif
#else
#   if defined(KOS_CPP11)
#       undef KOS_CPP11
#   endif
#endif

/*==========================================================================*/
/* <atomic>                                                                 */
/*==========================================================================*/

#ifdef KOS_CPP11

#include <atomic>

#define KOS_ATOMIC(type) std::atomic<type>

static inline void KOS_atomic_full_barrier()
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

static inline void KOS_atomic_acquire_barrier()
{
    std::atomic_thread_fence(std::memory_order_acquire);
}

static inline void KOS_atomic_release_barrier()
{
    std::atomic_thread_fence(std::memory_order_release);
}

static inline uint32_t KOS_atomic_read_relaxed_u32(KOS_ATOMIC(uint32_t)& src)
{
    return src.load(std::memory_order_relaxed);
}

static inline uint32_t KOS_atomic_read_acquire_u32(KOS_ATOMIC(uint32_t)& src)
{
    return src.load(std::memory_order_acquire);
}

template<typename T>
T* KOS_atomic_read_relaxed_ptr(KOS_ATOMIC(T*)& src)
{
    return src.load(std::memory_order_relaxed);
}

template<typename T>
T* KOS_atomic_read_acquire_ptr(KOS_ATOMIC(T*)& src)
{
    return src.load(std::memory_order_acquire);
}

static inline void KOS_atomic_write_relaxed_u32(KOS_ATOMIC(uint32_t)& dest, uint32_t value)
{
    dest.store(value, std::memory_order_relaxed);
}

static inline void KOS_atomic_write_release_u32(KOS_ATOMIC(uint32_t)& dest, uint32_t value)
{
    dest.store(value, std::memory_order_release);
}

template<typename T>
void KOS_atomic_write_relaxed_ptr(KOS_ATOMIC(T*)& dest, T* value)
{
    dest.store(value, std::memory_order_relaxed);
}

template<typename T>
void KOS_atomic_write_release_ptr(KOS_ATOMIC(T*)& dest, T* value)
{
    dest.store(value, std::memory_order_relaxed);
}

static inline bool KOS_atomic_cas_strong_u32(KOS_ATOMIC(uint32_t)& dest, uint32_t oldv, uint32_t newv)
{
    return dest.compare_exchange_strong(oldv, newv, std::memory_order_seq_cst, std::memory_order_relaxed);
}

static inline bool KOS_atomic_cas_weak_u32(KOS_ATOMIC(uint32_t)& dest, uint32_t oldv, uint32_t newv)
{
    return dest.compare_exchange_weak(oldv, newv, std::memory_order_seq_cst, std::memory_order_relaxed);
}

template<typename T>
bool KOS_atomic_cas_strong_ptr(KOS_ATOMIC(T*)& dest, T* oldv, T* newv)
{
    return dest.compare_exchange_strong(oldv, newv, std::memory_order_seq_cst, std::memory_order_relaxed);
}

template<typename T>
bool KOS_atomic_cas_weak_ptr(KOS_ATOMIC(T*)& dest, T* oldv, T* newv)
{
    return dest.compare_exchange_weak(oldv, newv, std::memory_order_seq_cst, std::memory_order_relaxed);
}

template<typename T>
int32_t KOS_atomic_add_i32(KOS_ATOMIC(T)& dest, int32_t value)
{
    return static_cast<int32_t>(dest.fetch_add(static_cast<T>(value)));
}

template<typename T>
uint32_t KOS_atomic_add_u32(KOS_ATOMIC(T)& dest, uint32_t value)
{
    return static_cast<int32_t>(dest.fetch_add(static_cast<T>(value)));
}

static inline uint32_t KOS_atomic_swap_u32(KOS_ATOMIC(uint32_t)& dest, uint32_t value)
{
    return dest.exchange(value);
}

template<typename T>
T* KOS_atomic_swap_ptr(KOS_ATOMIC(T*)& dest, T* value)
{
    return dest.exchange(value);
}

/*==========================================================================*/
/* <stdatomic.h>                                                            */
/*==========================================================================*/

#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)

#include <stdatomic.h>

#define KOS_ATOMIC(type) _Atomic(type)

#define KOS_atomic_full_barrier() atomic_thread_fence(memory_order_seq_cst)

#define KOS_atomic_acquire_barrier() atomic_thread_fence(memory_order_acquire)

#define KOS_atomic_release_barrier() atomic_thread_fence(memory_order_release)

#define KOS_atomic_read_relaxed_u32(src) atomic_load_explicit(&(src), memory_order_relaxed)

#define KOS_atomic_read_acquire_u32(src) atomic_load_explicit(&(src), memory_order_relaxed)

#define KOS_atomic_read_relaxed_ptr(src) atomic_load_explicit(&(src), memory_order_relaxed)

#define KOS_atomic_read_acquire_ptr(src) atomic_load_explicit(&(src), memory_order_relaxed)

#define KOS_atomic_write_relaxed_u32(dest, value) atomic_store_explicit(&(dest), (value), memory_order_relaxed)

#define KOS_atomic_write_release_u32(dest, value) atomic_store_explicit(&(dest), (value), memory_order_relaxed)

#define KOS_atomic_write_relaxed_ptr(dest, value) atomic_store_explicit(&(dest), (value), memory_order_relaxed)

#define KOS_atomic_write_release_ptr(dest, value) atomic_store_explicit(&(dest), (value), memory_order_relaxed)

#define KOS_atomic_cas_strong_u32(dest, oldv, newv) kos_atomic_cas_strong_u32(&(dest), (oldv), (newv))

static inline int kos_atomic_cas_strong_u32(_Atomic(uint32_t) *dest, uint32_t oldv, uint32_t newv)
{
    return atomic_compare_exchange_strong_explicit(dest, &oldv, newv, memory_order_seq_cst, memory_order_relaxed);
}

#define KOS_atomic_cas_weak_u32(dest, oldv, newv) kos_atomic_cas_weak_u32(&(dest), (oldv), (newv))

static inline int kos_atomic_cas_weak_u32(_Atomic(uint32_t) *dest, uint32_t oldv, uint32_t newv)
{
    return atomic_compare_exchange_weak_explicit(dest, &oldv, newv, memory_order_seq_cst, memory_order_relaxed);
}

#define KOS_atomic_cas_strong_ptr(dest, oldv, newv) kos_atomic_cas_strong_ptr((_Atomic(void *) *)&(dest), (void *)(oldv), (void *)(newv))

static inline int kos_atomic_cas_strong_ptr(_Atomic(void *) *dest, void *oldv, void *newv)
{
    return atomic_compare_exchange_strong_explicit(dest, &oldv, newv, memory_order_seq_cst, memory_order_relaxed);
}

#define KOS_atomic_cas_weak_ptr(dest, oldv, newv) kos_atomic_cas_weak_ptr((_Atomic(void *) *)&(dest), (void *)(oldv), (void *)(newv))

static inline int kos_atomic_cas_weak_ptr(_Atomic(void *) *dest, void *oldv, void *newv)
{
    return atomic_compare_exchange_weak_explicit(dest, &oldv, newv, memory_order_seq_cst, memory_order_relaxed);
}

#define KOS_atomic_add_i32(dest, value) ((int32_t)atomic_fetch_add(&(dest), (value)))

#define KOS_atomic_add_u32(dest, value) ((uint32_t)atomic_fetch_add(&(dest), (value)))

#define KOS_atomic_swap_u32(dest, value) atomic_exchange(&(dest), (value))

#define KOS_atomic_swap_ptr(dest, value) atomic_exchange(&(dest), (value))

/*==========================================================================*/
/* Microsoft Visual C/C++                                                   */
/*==========================================================================*/

#elif defined(_MSC_VER)

#pragma warning( push )
#pragma warning( disable : 4255 ) /* '__slwpcb': no function prototype given: converting '()' to '(void)' */
#pragma warning( disable : 4668 ) /* '__cplusplus' is not defined as a preprocessor macro */
#include <intrin.h>
#pragma warning( pop )

#define KOS_ATOMIC(type) type volatile

#ifdef _M_AMD64
#define KOS_atomic_full_barrier __faststorefence
#elif defined(_M_IX86)
#define KOS_atomic_full_barrier() do { volatile uint32_t barrier; __asm { xchg barrier, eax }; } while (0)
#else
#error "Atomic operations not implemented for this compiler or architecture"
#endif

#define KOS_atomic_acquire_barrier() KOS_atomic_full_barrier()

#define KOS_atomic_release_barrier() KOS_atomic_full_barrier()

#define KOS_atomic_read_relaxed_u32(src) \
        (*(uint32_t volatile const *)(&(src)))

#define KOS_atomic_read_acquire_u32(src) \
        (*(uint32_t volatile const *)(&(src)))

#define KOS_atomic_read_relaxed_ptr(src) \
        (*(void *volatile const *)(&(src)))

#define KOS_atomic_read_acquire_ptr(src) \
        (*(void *volatile const *)(&(src)))

#define KOS_atomic_write_relaxed_u32(dest, value) \
        (*(uint32_t volatile *)(&(dest)) = (uint32_t)(value))

#define KOS_atomic_write_release_u32(dest, value) \
        (*(uint32_t volatile *)(&(dest)) = (uint32_t)(value))

#define KOS_atomic_write_relaxed_ptr(dest, value) \
        (*(void *volatile *)(&(dest)) = (void *)(value))

#define KOS_atomic_write_release_ptr(dest, value) \
        (*(void *volatile *)(&(dest)) = (void *)(value))

#define KOS_atomic_cas_strong_u32(dest, oldv, newv) \
        (_InterlockedCompareExchange((long volatile *)(&(dest)), \
                                     (long)(newv), \
                                     (long)(oldv)) == ((long)(oldv)))

#define KOS_atomic_cas_weak_u32 KOS_atomic_cas_strong_u32

#ifdef _M_AMD64
#define KOS_atomic_cas_strong_ptr(dest, oldv, newv) \
         (_InterlockedCompareExchange64((int64_t volatile *)(intptr_t volatile *)(&(dest)), \
                                        (intptr_t)(newv), \
                                        (intptr_t)(oldv)) == (intptr_t)(oldv))
#else
#define KOS_atomic_cas_strong_ptr(dest, oldv, newv) \
         (_InterlockedCompareExchange((long volatile *)(&(dest)), \
                                      (long)(newv), \
                                      (long)(oldv)) == ((long)(oldv)))
#endif

#define KOS_atomic_cas_weak_ptr KOS_atomic_cas_strong_ptr

#define KOS_atomic_add_i32(dest, value) \
        ((int32_t)_InterlockedExchangeAdd((long volatile *)&(dest), \
                                          (long)(value)))

#define KOS_atomic_add_u32(dest, value) \
        ((uint32_t)_InterlockedExchangeAdd((long volatile *)&(dest), \
                                           (long)(value)))

#define KOS_atomic_swap_u32(dest, value) \
        ((uint32_t)_InterlockedExchange((long volatile *)&(dest), \
                                        (long)(value)))

#ifdef _M_AMD64
#define KOS_atomic_swap_ptr(dest, value) \
         ((void *)_InterlockedExchange64((int64_t volatile *)(intptr_t volatile *)&(dest), \
                                         (intptr_t)(value)))
#else
#define KOS_atomic_swap_ptr(dest, value) \
         ((void *)_InterlockedExchange((long volatile *)&(dest), \
                                       (long)(value)))
#endif

/*==========================================================================*/
/* GCC and clang                                                            */
/*==========================================================================*/

#elif defined(__GNUC__) && \
      defined(__GCC_ATOMIC_INT_LOCK_FREE) && (__GCC_ATOMIC_INT_LOCK_FREE > 1) && \
      defined(__GCC_ATOMIC_POINTER_LOCK_FREE) && (__GCC_ATOMIC_POINTER_LOCK_FREE > 1) && \
      defined(__GCC_ATOMIC_TEST_AND_SET_TRUEVAL) && (__GCC_ATOMIC_TEST_AND_SET_TRUEVAL > 0) && \
      defined(__ATOMIC_SEQ_CST)

#define KOS_ATOMIC(type) type volatile

#define KOS_atomic_full_barrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)

#define KOS_atomic_acquire_barrier() __atomic_thread_fence(__ATOMIC_ACQUIRE)

#define KOS_atomic_release_barrier() __atomic_thread_fence(__ATOMIC_RELEASE)

#define KOS_atomic_read_relaxed_u32(src) __atomic_load_n(&(src), __ATOMIC_RELAXED)

#define KOS_atomic_read_acquire_u32(src) __atomic_load_n(&(src), __ATOMIC_ACQUIRE)

#define KOS_atomic_read_relaxed_ptr(src) __atomic_load_n(&(src), __ATOMIC_RELAXED)

#define KOS_atomic_read_acquire_ptr(src) __atomic_load_n(&(src), __ATOMIC_ACQUIRE)

#define KOS_atomic_write_relaxed_u32(dest, value) __atomic_store_n(&(dest), (value), __ATOMIC_RELAXED)

#define KOS_atomic_write_release_u32(dest, value) __atomic_store_n(&(dest), (value), __ATOMIC_RELEASE)

#define KOS_atomic_write_relaxed_ptr(dest, value) __atomic_store_n(&(dest), (value), __ATOMIC_RELAXED)

#define KOS_atomic_write_release_ptr(dest, value) __atomic_store_n(&(dest), (value), __ATOMIC_RELEASE)

#define KOS_atomic_cas_strong_u32(dest, oldv, newv) kos_atomic_cas_strong_u32(&(dest), (oldv), (newv))

#define KOS_atomic_cas_weak_u32(dest, oldv, newv) kos_atomic_cas_weak_u32(&(dest), (oldv), (newv))

#define KOS_atomic_cas_strong_ptr(dest, oldv, newv) kos_atomic_cas_strong_ptr((void *volatile *)&(dest), (void *)(oldv), (void *)(newv))

#define KOS_atomic_cas_weak_ptr(dest, oldv, newv) kos_atomic_cas_weak_ptr((void *volatile *)&(dest), (void *)(oldv), (void *)(newv))

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)) || !defined(__STRICT_ANSI__)
static inline int kos_atomic_cas_strong_u32(uint32_t volatile *dest, uint32_t oldv, uint32_t newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}

static inline int kos_atomic_cas_weak_u32(uint32_t volatile *dest, uint32_t oldv, uint32_t newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}

static inline int kos_atomic_cas_strong_ptr(void *volatile *dest, void *oldv, void *newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}

static inline int kos_atomic_cas_weak_ptr(void *volatile *dest, void *oldv, void *newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}
#else
#define KOS_GCC_ATOMIC_EXTENSION 1
int kos_atomic_cas_strong_u32(uint32_t volatile *dest, uint32_t oldv, uint32_t newv);
int kos_atomic_cas_weak_u32(uint32_t volatile *dest, uint32_t oldv, uint32_t newv);
int kos_atomic_cas_strong_ptr(void *volatile *dest, void *oldv, void *newv);
int kos_atomic_cas_weak_ptr(void *volatile *dest, void *oldv, void *newv);
#endif

#define KOS_atomic_add_i32(dest, value) ((int32_t)__atomic_fetch_add(&(dest), (value), __ATOMIC_SEQ_CST))

#define KOS_atomic_add_u32(dest, value) ((uint32_t)__atomic_fetch_add(&(dest), (value), __ATOMIC_SEQ_CST))

#define KOS_atomic_swap_u32(dest, value) __atomic_exchange_n(&(dest), (value), __ATOMIC_SEQ_CST)

#define KOS_atomic_swap_ptr(dest, value) __atomic_exchange_n(&(dest), (value), __ATOMIC_SEQ_CST)

/*==========================================================================*/
/* x86 (32-bit) fallback for old GCC                                        */
/*==========================================================================*/

#elif defined(__GNUC__) && defined(__i386)

#define KOS_ATOMIC(type) type volatile

#define KOS_atomic_full_barrier() do {                        \
        uint32_t volatile  barrier = 0;                       \
        uint32_t volatile *barrier_ptr = &barrier;            \
        uint32_t           barrier_ret = 0;                   \
        __asm__ volatile("xchg %0, %1\n"                      \
                    : "+r" (barrier_ret), "+m" (*barrier_ptr) \
                    : : "memory", "cc");                      \
        } while (0)

#define KOS_atomic_acquire_barrier() KOS_atomic_full_barrier()

#define KOS_atomic_release_barrier() KOS_atomic_full_barrier()

#define KOS_atomic_read_relaxed_u32(src) \
        (*(uint32_t volatile const *)(&(src)))

#define KOS_atomic_read_acquire_u32(src) \
        (*(uint32_t volatile const *)(&(src)))

#define KOS_atomic_read_relaxed_ptr(src) \
        (*(void *volatile const *)(&(src)))

#define KOS_atomic_read_acquire_ptr(src) \
        (*(void *volatile const *)(&(src)))

#define KOS_atomic_write_relaxed_u32(dest, value) \
        (*(uint32_t volatile *)(&(dest)) = (uint32_t)(value))

#define KOS_atomic_write_release_u32(dest, value) \
        (*(uint32_t volatile *)(&(dest)) = (uint32_t)(value))

#define KOS_atomic_write_relaxed_ptr(dest, value) \
        (*(void *volatile *)(&(dest)) = (void *)(value))

#define KOS_atomic_write_release_ptr(dest, value) \
        (*(void *volatile *)(&(dest)) = (void *)(value))

#define KOS_atomic_cas_strong_u32(dest, oldv, newv)              \
    __extension__ ({                                             \
       uint32_t           at_ret;                                \
       uint32_t volatile *at_ptr = (uint32_t volatile *)&(dest); \
       __asm__ volatile("lock cmpxchgl %2, %1\n"                 \
                    : "=a" (at_ret), "+m" (*at_ptr)              \
                    : "r" (newv), "0" (oldv)                     \
                    : "memory");                                 \
       at_ret == oldv;                                           \
    })

#define KOS_atomic_cas_weak_u32 KOS_atomic_cas_strong_u32

#define KOS_atomic_cas_strong_ptr(dest, oldv, newv)        \
    __extension__ ({                                       \
       void           *at_ret;                             \
       void *volatile *at_ptr = (void *volatile *)&(dest); \
       __asm__ volatile("lock cmpxchgl %2, %1\n"           \
                    : "=a" (at_ret), "+m" (*at_ptr)        \
                    : "r" (newv), "0" (oldv)               \
                    : "memory");                           \
       at_ret == oldv;                                     \
    })

#define KOS_atomic_cas_weak_ptr KOS_atomic_cas_strong_ptr

#define KOS_atomic_add_i32(dest, value)                           \
    __extension__ ({                                              \
        int32_t            at_ret = (int32_t)(value);             \
        uint32_t volatile *at_ptr = (uint32_t volatile *)&(dest); \
        __asm__ volatile("lock xaddl %0, %1\n"                    \
                    : "+r" (at_ret), "+m" (*at_ptr)               \
                    : : "memory", "cc");                          \
        at_ret;                                                   \
    })

#define KOS_atomic_add_u32(dest, value)                           \
    __extension__ ({                                              \
        uint32_t           at_ret = (uint32_t)(value);            \
        uint32_t volatile *at_ptr = (uint32_t volatile *)&(dest); \
        __asm__ volatile("lock xaddl %0, %1\n"                    \
                    : "+r" (at_ret), "+m" (*at_ptr)               \
                    : : "memory", "cc");                          \
        at_ret;                                                   \
    })

#define KOS_atomic_swap_u32(dest, value)                          \
    __extension__ ({                                              \
        uint32_t           at_ret = (uint32_t)(value);            \
        uint32_t volatile *at_ptr = (uint32_t volatile *)&(dest); \
        __asm__ volatile("xchgl %0, %1\n"                         \
                    : "+r" (at_ret), "+m" (*at_ptr)               \
                    : : "memory", "cc");                          \
        at_ret;                                                   \
    })

#define KOS_atomic_swap_ptr(dest, value)                    \
    __extension__ ({                                        \
        void           *at_ret = (void *)(value);           \
        void *volatile *at_ptr = (void *volatile *)&(dest); \
        __asm__ volatile("xchgl %0, %1\n"                   \
                    : "+r" (at_ret), "+m" (*at_ptr)         \
                    : : "memory", "cc");                    \
        at_ret;                                             \
    })

/*==========================================================================*/
/* x86_64 fallback for old GCC                                              */
/*==========================================================================*/

#elif defined(__GNUC__) && defined(__x86_64)

#define KOS_ATOMIC(type) type volatile

#define KOS_atomic_full_barrier() do {                        \
        uint32_t volatile  barrier = 0;                       \
        uint32_t volatile *barrier_ptr = &barrier;            \
        uint32_t           barrier_ret = 0;                   \
        __asm__ volatile("lock or %0, %1\n"                   \
                    : "+r" (barrier_ret), "+m" (*barrier_ptr) \
                    : : "memory", "cc");                      \
        } while (0)

#define KOS_atomic_acquire_barrier() KOS_atomic_full_barrier()

#define KOS_atomic_release_barrier() KOS_atomic_full_barrier()

#define KOS_atomic_read_relaxed_u32(src) \
        (*(uint32_t volatile const *)(&(src)))

#define KOS_atomic_read_acquire_u32(src) \
        (*(uint32_t volatile const *)(&(src)))

#define KOS_atomic_read_relaxed_ptr(src) \
        (*(void *volatile const *)(&(src)))

#define KOS_atomic_read_acquire_ptr(src) \
        (*(void *volatile const *)(&(src)))

#define KOS_atomic_write_relaxed_u32(dest, value) \
        (*(uint32_t volatile *)(&(dest)) = (uint32_t)(value))

#define KOS_atomic_write_release_u32(dest, value) \
        (*(uint32_t volatile *)(&(dest)) = (uint32_t)(value))

#define KOS_atomic_write_relaxed_ptr(dest, value) \
        (*(void *volatile *)(&(dest)) = (void *)(value))

#define KOS_atomic_write_release_ptr(dest, value) \
        (*(void *volatile *)(&(dest)) = (void *)(value))

#define KOS_atomic_cas_strong_u32(dest, oldv, newv)              \
    __extension__ ({                                             \
       uint32_t           at_ret;                                \
       uint32_t volatile *at_ptr = (uint32_t volatile *)&(dest); \
       __asm__ volatile("lock cmpxchgl %2, %1\n"                 \
                    : "=a" (at_ret), "+m" (*at_ptr)              \
                    : "r" (newv), "0" (oldv)                     \
                    : "memory");                                 \
       at_ret == oldv;                                           \
    })

#define KOS_atomic_cas_weak_u32 KOS_atomic_cas_strong_u32

#define KOS_atomic_cas_strong_ptr(dest, oldv, newv)        \
    __extension__ ({                                       \
       void           *at_ret;                             \
       void *volatile *at_ptr = (void *volatile *)&(dest); \
       __asm__ volatile("lock cmpxchgq %2, %1\n"           \
                    : "=a" (at_ret), "+m" (*at_ptr)        \
                    : "r" (newv), "0" (oldv)               \
                    : "memory");                           \
       at_ret == oldv;                                     \
    })

#define KOS_atomic_cas_weak_ptr KOS_atomic_cas_strong_ptr

#define KOS_atomic_add_i32(dest, value)                           \
    __extension__ ({                                              \
        int32_t            at_ret = (int32_t)(value);             \
        uint32_t volatile *at_ptr = (uint32_t volatile *)&(dest); \
        __asm__ volatile("lock xaddl %0, %1\n"                    \
                    : "+r" (at_ret), "+m" (*at_ptr)               \
                    : : "memory", "cc");                          \
        at_ret;                                                   \
    })

#define KOS_atomic_add_u32(dest, value)                           \
    __extension__ ({                                              \
        uint32_t           at_ret = (uint32_t)(value);            \
        uint32_t volatile *at_ptr = (uint32_t volatile *)&(dest); \
        __asm__ volatile("lock xaddl %0, %1\n"                    \
                    : "+r" (at_ret), "+m" (*at_ptr)               \
                    : : "memory", "cc");                          \
        at_ret;                                                   \
    })

#define KOS_atomic_swap_u32(dest, value)                          \
    __extension__ ({                                              \
        uint32_t           at_ret = (uint32_t)(value);            \
        uint32_t volatile *at_ptr = (uint32_t volatile *)&(dest); \
        __asm__ volatile("xchgl %0, %1\n"                         \
                    : "+r" (at_ret), "+m" (*at_ptr)               \
                    : : "memory", "cc");                          \
        at_ret;                                                   \
    })

#define KOS_atomic_swap_ptr(dest, value)                    \
    __extension__ ({                                        \
        void           *at_ret = (void *)(value);           \
        void *volatile *at_ptr = (void *volatile *)&(dest); \
        __asm__ volatile("xchgq %0, %1\n"                   \
                    : "+r" (at_ret), "+m" (*at_ptr)         \
                    : : "memory", "cc");                    \
        at_ret;                                             \
    })

#else

#error "Atomic operations not implemented for this compiler or architecture"

#endif

void kos_atomic_move_ptr(KOS_ATOMIC(void *) *dest,
                         KOS_ATOMIC(void *) *src,
                         unsigned            ptr_count);

void kos_spin_lock(KOS_ATOMIC(uint32_t) *lock);
void kos_spin_unlock(KOS_ATOMIC(uint32_t) *lock);

#endif
