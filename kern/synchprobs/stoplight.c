/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

struct lock *lock0;
struct lock *lock1;
struct lock *lock2;
struct lock *lock3;
struct lock * lockquadrant(uint32_t);

/*
 * Called by the driver during initialization.
 */

void
stoplight_init() {
	lock0 = lock_create("lock0");
	lock1 = lock_create("lock1");
	lock2 = lock_create("lock2");
	lock3 = lock_create("lock3");
	return;
}

struct lock * lockquadrant(uint32_t quad){
	switch (quad) {
		case 0:
			return lock0;
		case 1:
			return lock1;
		case 2:
			return lock2;
		case 3:
			return lock3;
	}
	return lock0;
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	lock_destroy(lock0);
	lock_destroy(lock1);
	lock_destroy(lock2);
	lock_destroy(lock3);
return;
}

//Acquire lock for 1 quadrant. And move
void
turnright(uint32_t direction, uint32_t index)
{

	lock_acquire(lockquadrant(direction));
	inQuadrant(direction, index);
	leaveIntersection(index);
	lock_release(lockquadrant(direction));
	return;
}

//Acquire the locks for 2 quadrants (in increasing order) and move.
void
gostraight(uint32_t direction, uint32_t index)
{
	uint32_t newdir = (direction+3)%4;
	uint32_t order[2];
	if(newdir<direction){
		order[0] = newdir;
		order[1] = direction;
	}
	else{
		order[0] = direction;
		order[1] = newdir;
	}
	lock_acquire(lockquadrant(order[0]));
	lock_acquire(lockquadrant(order[1]));
	inQuadrant(direction, index);
	inQuadrant(newdir, index);
	leaveIntersection(index);
	lock_release(lockquadrant(order[0]));
	lock_release(lockquadrant(order[1]));
	return;
}

//Acquire locks for 3 quadrants in increasing order and move.
void
turnleft(uint32_t direction, uint32_t index)
{
	uint32_t first = (direction+3)%4;
	uint32_t second = (direction+2)%4;
	uint32_t order[3];
	if(direction<first){
		order[0] = direction;
		order[1] = second;
		order[2] = first;
	}
	else if(direction<second){
		order[0] = first;
		order[1] = direction;
		order[2] = second;
	}
	else{
		order[0] = second;
		order[1] = first;
		order[2] = direction;
	}
	lock_acquire(lockquadrant(order[0]));
	lock_acquire(lockquadrant(order[1]));
	lock_acquire(lockquadrant(order[2]));
	inQuadrant(direction, index);
	inQuadrant(first, index);
	inQuadrant(second, index);
	leaveIntersection(index);
	lock_release(lockquadrant(order[0]));	
	lock_release(lockquadrant(order[1]));
	lock_release(lockquadrant(order[2]));
	return;
}
