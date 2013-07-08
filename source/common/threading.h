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

#ifndef _THREADING_H_
#define _THREADING_H_

#ifdef _WIN32
#include "Windows.h"
#else
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#endif

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
        sem_init(&this->semaphore, 0, 0);
    }

    ~Event()
    {
        sem_destroy(&this->semaphore);
    }

    void wait()
    {
        sem_wait(&this->semaphore);
    }

    void trigger()
    {
        sem_post(&this->semaphore);
    }

protected:

    /* the POSIX version uses a counting semaphore */
    sem_t semaphore;
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

#endif // ifndef _THREADING_H_
