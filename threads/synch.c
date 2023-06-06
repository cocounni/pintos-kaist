/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
        list_insert_ordered(&sema->waiters, &thread_current()->elem, &thread_compare_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
        // semaphore waiters list에서 우선순위가 가장 높은 순으로 정렬
        list_sort(&sema->waiters, &thread_compare_priority, NULL);
        // unblock함수는 insert ordered로 ready_list에 넣음
		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
    }
	sema->value++;
    // 현재 thread보다 sema_up한 thread의 우선순위가 높을 수 있으므로
    thread_priority_yield();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

    struct thread *current_thread = thread_current();
    if (lock->holder) {
        current_thread->wait_on_lock = lock;
        list_insert_ordered(&lock->holder->donation_list, &current_thread->donation_elem,
                            &donate_compare_priority, NULL);
        // list_push_back(&lock->holder->donation_list, &current_thread->donation_elem);
        donate_priority();
    }


	sema_down (&lock->semaphore);
    current_thread->wait_on_lock = NULL;
	lock->holder = current_thread;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

    remove_lock_donation_list(lock);
    refresh_priority();

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
    //* list_elem은 condition variables를 위한 하나의 지정 값으로 보인다.
    //* condvar외에 쓰일 일이 없는 것 같음
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
    //! waiter 함수에 대한 인자 값을 어디서 받아오는가? 
	struct semaphore_elem waiter;
    
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// list_push_back (&cond->waiters, &waiter.elem);
    list_insert_ordered(&cond->waiters, &waiter.elem, &sema_compare_priority, NULL);
	lock_release (lock);  // semaphore접근을 위해 lock해제
    // sema_down을 통해 해당 semaphore의 waiters list에서 기다리게 함
	sema_down (&waiter.semaphore);
	lock_acquire (lock);  // semaphore에 다시 접근하지 못하게 lock
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));  // lock holder에 현재 thread 설정

	if (!list_empty (&cond->waiters)) {
        // priority 정렬
        list_sort(&cond->waiters, &sema_compare_priority, NULL);
        // priority가 가장 높은 thread를 ready_list로
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

// add function
// cond_wait에서 cond->waiters list에 semaphore_elem을 sema_compare_priority순서대로 넣어줌
// semaphore_elem은 list_elem인자와 semaphore인자가 있음
// semaphore의 waiters list를 넣어야 하므로 인자는 list_elem
bool sema_compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux) {
    // cond의 list_elem에서 semaphore를 list_entry로 찾고, 그 semaphore의 waiter list를 변수로 저장
    struct list a_sema_list = list_entry(a, struct semaphore_elem, elem)->semaphore.waiters;
    struct list b_sema_list = list_entry(b, struct semaphore_elem, elem)->semaphore.waiters;
    // waiter list에서 가장 앞의 thread의 우선순위를 변수로 저장 및 return
    int a_priority = list_entry(list_begin(&a_sema_list), struct thread, elem)->priority;
    int b_priority = list_entry(list_begin(&b_sema_list), struct thread, elem)->priority;
    return a_priority > b_priority;
}

bool donate_compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux) {
    int priority_a  = list_entry(a, struct thread, donation_elem)->priority;
    int priority_b = list_entry(b, struct thread, donation_elem)->priority;
    return priority_a > priority_b;
}

void donate_priority() {
    struct thread *current_thread = thread_current();

    while (1) {
    // gitbook에 나와있던 8levels를 의미하는 것이었다!
    // for (int i = 0; i < 8; i++) {
        // if (!current_thread->wait_on_lock->holder)
        if (!current_thread->wait_on_lock)
            break;

        struct thread *lock_thread = current_thread->wait_on_lock->holder;

        lock_thread->priority = current_thread->priority;
        current_thread = lock_thread;
    }
    
}

void remove_lock_donation_list(struct lock *lock) {
    struct thread *current_thread = thread_current();
    struct list_elem *list_e = list_begin(&current_thread->donation_list);

    for (list_e; list_e != list_end(&current_thread->donation_list); list_e = list_next(list_e)) {
        // struct thread *removal_thread = list_entry(list_e, struct thread, elem);
        struct thread *removal_thread = list_entry(list_e, struct thread, donation_elem); //! modified code
        // if (removal_thread == lock->holder) {
        //     list_remove(list_e);
        // }
       //! 오류나서 수정한 것, 코드 해석하기
        if (removal_thread->wait_on_lock == lock) 
            list_remove(&removal_thread->donation_elem);
    }
}

void refresh_priority (void) {
    struct thread *current_thread = thread_current();
    current_thread->priority = current_thread->original_priority;
    // donation_list이 존재하면, 우선순위를 가장 높은애로 다시 바꿔주기
    if(!list_empty(&current_thread->donation_list)) {
        list_sort(&current_thread->donation_list, &donate_compare_priority, NULL);

        int highest_priority = list_entry(list_front(&current_thread->donation_list), struct thread, donation_elem)->priority;
        
        if (current_thread->priority < highest_priority)
            current_thread->priority = highest_priority;
    }
}