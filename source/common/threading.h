/*****************************************************************************
 * x265: threading class and intrinsics
 *****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Steve Borho <steve@borho.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@multicorewareinc.com
 *****************************************************************************/

#ifndef X265_THREADING_H
#define X265_THREADING_H

#include "common.h"
#include "x265.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <semaphore.h>
#include <cstdlib>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#endif

#include <stdint.h>

#if MACOS
#include <sys/param.h>
#include <sys/sysctl.h>
#endif

#ifdef __GNUC__                         /* GCCs builtin atomics */

#include <unistd.h>
#include <limits.h>

#define CTZ64(id, x)                        id = (unsigned long)__builtin_ctzll(x)
#define ATOMIC_OR(ptr, mask)                __sync_or_and_fetch(ptr, mask)
#define ATOMIC_CAS(ptr, oldval, newval)     __sync_val_compare_and_swap(ptr, oldval, newval)
#define ATOMIC_CAS32(ptr, oldval, newval)   __sync_val_compare_and_swap(ptr, oldval, newval)
#define ATOMIC_INC(ptr)                     __sync_add_and_fetch((volatile int32_t*)ptr, 1)
#define ATOMIC_DEC(ptr)                     __sync_add_and_fetch((volatile int32_t*)ptr, -1)
#define GIVE_UP_TIME()                      usleep(0)

#elif defined(_MSC_VER)                 /* Windows atomic intrinsics */

#include <intrin.h>

#if !_WIN64
inline int _BitScanReverse64(DWORD *id, uint64_t x64) // fake 64bit CLZ
{
    uint32_t high32 = (uint32_t)(x64 >> 32);
    uint32_t low32 = (uint32_t)x64;

    if (high32)
    {
        _BitScanReverse(id, high32);
        *id += 32;
        return 1;
    }
    else if (low32)
        return _BitScanReverse(id, low32);
    else
        return *id = 0;
}

inline int _BitScanForward64(DWORD *id, uint64_t x64) // fake 64bit CLZ
{
    uint32_t high32 = (uint32_t)(x64 >> 32);
    uint32_t low32 = (uint32_t)x64;

    if (high32)
    {
        _BitScanForward(id, high32);
        *id += 32;
        return 1;
    }
    else if (low32)
        return _BitScanForward(id, low32);
    else
        return *id = 0;
}

#endif // if !_WIN64

#if _WIN32_WINNT <= _WIN32_WINNT_WINXP
/* Windows XP did not define this intrinsic */
FORCEINLINE LONGLONG x265_interlocked_OR64(__inout LONGLONG volatile *Destination,
                                           __in    LONGLONG           Value)
{
    LONGLONG Old;

    do
    {
        Old = *Destination;
    }
    while (_InterlockedCompareExchange64(Destination, Old | Value, Old) != Old);

    return Old;
}

#define ATOMIC_OR(ptr, mask)            x265_interlocked_OR64((volatile LONG64*)ptr, mask)
#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma intrinsic(_InterlockedCompareExchange64)
#endif
#else // if _WIN32_WINNT <= _WIN32_WINNT_WINXP
#define ATOMIC_OR(ptr, mask)            InterlockedOr64((volatile LONG64*)ptr, mask)
#endif // if _WIN32_WINNT <= _WIN32_WINNT_WINXP

#define CTZ64(id, x)                        _BitScanForward64(&id, x)
#define ATOMIC_CAS(ptr, oldval, newval)     (uint64_t)_InterlockedCompareExchange64((volatile LONG64*)ptr, newval, oldval)
#define ATOMIC_CAS32(ptr, oldval, newval)   (uint64_t)_InterlockedCompareExchange((volatile LONG*)ptr, newval, oldval)
#define ATOMIC_INC(ptr)                     InterlockedIncrement((volatile LONG*)ptr)
#define ATOMIC_DEC(ptr)                     InterlockedDecrement((volatile LONG*)ptr)
#define GIVE_UP_TIME()                      Sleep(0)

#endif // ifdef __GNUC__


namespace x265 {
// x265 private namespace

#ifdef _WIN32

typedef HANDLE ThreadHandle;

class Lock
{
public:

    Lock()
    {
        InitializeCriticalSection(&this->handle);
    }

    ~Lock()
    {
        DeleteCriticalSection(&this->handle);
    }

    void acquire()
    {
        EnterCriticalSection(&this->handle);
    }

    void release()
    {
        LeaveCriticalSection(&this->handle);
    }

protected:

    CRITICAL_SECTION handle;
};

class Event
{
public:

    Event()
    {
        this->handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    }

    ~Event()
    {
        CloseHandle(this->handle);
    }

    void wait()
    {
        WaitForSingleObject(this->handle, INFINITE);
    }

    bool timedWait(uint32_t milliseconds)
    {
        /* returns true if event was signaled */
        return WaitForSingleObject(this->handle, milliseconds) == WAIT_OBJECT_0;
    }

    void trigger()
    {
        SetEvent(this->handle);
    }

protected:

    HANDLE handle;
};

#else /* POSIX / pthreads */

typedef pthread_t ThreadHandle;

class Lock
{
public:

    Lock()
    {
        pthread_mutex_init(&this->handle, NULL);
    }

    ~Lock()
    {
        pthread_mutex_destroy(&this->handle);
    }

    void acquire()
    {
        pthread_mutex_lock(&this->handle);
    }

    void release()
    {
        pthread_mutex_unlock(&this->handle);
    }

protected:

    pthread_mutex_t handle;
};

class Event
{
public:

    Event()
    {
        m_counter = 0;
        if (pthread_mutex_init(&m_mutex, NULL) ||
            pthread_cond_init(&m_cond, NULL))
        {
            x265_log(NULL, X265_LOG_ERROR, "fatal: unable to initialize conditional variable\n");
        }
    }

    ~Event()
    {
        pthread_cond_destroy(&m_cond);
        pthread_mutex_destroy(&m_mutex);
    }

    void wait()
    {
        pthread_mutex_lock(&m_mutex);
        /* blocking wait on conditional variable, mutex is atomically released
         * while blocked. When condition is signaled, mutex is re-acquired */
        while (m_counter == 0)
            pthread_cond_wait(&m_cond, &m_mutex);
        m_counter--;
        pthread_mutex_unlock(&m_mutex);
    }

    bool timedWait(uint32_t waitms)
    {
        bool bTimedOut = false;
        pthread_mutex_lock(&m_mutex);
        if (m_counter == 0)
        {
            struct timeval tv;
            struct timespec ts;
            gettimeofday(&tv, NULL);
            /* convert current time from (sec, usec) to (sec, nsec) */
            ts.tv_sec = tv.tv_sec;
            ts.tv_nsec = tv.tv_usec * 1000;

            ts.tv_nsec += 1000 * 1000 * (waitms % 1000);    /* add ms to tv_nsec */
            ts.tv_sec += ts.tv_nsec / (1000 * 1000 * 1000); /* overflow tv_nsec */
            ts.tv_nsec %= (1000 * 1000 * 1000);             /* clamp tv_nsec */
            ts.tv_sec += waitms / 1000;                     /* add seconds */

            /* blocking wait on conditional variable, mutex is atomically released
             * while blocked. When condition is signaled, mutex is re-acquired.
             * ts is absolute time to stop waiting */
            bTimedOut = pthread_cond_timedwait(&m_cond, &m_mutex, &ts) == ETIMEDOUT;
        }
        if (m_counter > 0)
            m_counter--;
        pthread_mutex_unlock(&m_mutex);
        return bTimedOut;
    }

    void trigger()
    {
        pthread_mutex_lock(&m_mutex);
        if (m_counter < UINT32_MAX)
            m_counter++;
        /* Signal a single blocking thread */
        pthread_cond_signal(&m_cond);
        pthread_mutex_unlock(&m_mutex);
    }

protected:

    pthread_mutex_t m_mutex;
    pthread_cond_t  m_cond;
    uint32_t        m_counter;
};

#endif // ifdef _WIN32

class ScopedLock
{
public:

    ScopedLock(Lock &instance) : inst(instance)
    {
        this->inst.acquire();
    }

    ~ScopedLock()
    {
        this->inst.release();
    }

protected:

    // do not allow assignments
    ScopedLock &operator =(const ScopedLock &);

    Lock &inst;
};

//< Simplistic portable thread class.  Shutdown signalling left to derived class
class Thread
{
private:

    ThreadHandle thread;

public:

    Thread();

    virtual ~Thread();

    //< Derived class must implement ThreadMain.
    virtual void threadMain() = 0;

    //< Returns true if thread was successfully created
    bool start();

    void stop();
};
} // end namespace x265

#endif // ifndef X265_THREADING_H
