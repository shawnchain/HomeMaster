/*
 * fifo.c
 *
 *  Created on: 2017年4月1日
 *      Author: shawn
 */

#include "fifo.h"
#include "lib.h"

/**
 * Check whether the fifo is empty
 *
 * \note Calling fifo_isempty() is safe while a concurrent
 *       execution context is calling fifo_push() or fifo_pop()
 *       only if the CPU can atomically update a pointer
 *       (which the AVR and other 8-bit processors can't do).
 *
 * \sa fifo_isempty_locked
 */
 bool fifo_isempty(const FIFOPtr *fb)
{
	return fb->head == fb->tail;
}

/**
 * Check whether the fifo is full
 *
 * \note Calling fifo_isfull() is safe while a concurrent
 *       execution context is calling fifo_pop() and the
 *       CPU can update a pointer atomically.
 *       It is NOT safe when the other context calls
 *       fifo_push().
 *       This limitation is not usually problematic in a
 *       consumer/producer scenario because the
 *       fifo_isfull() and fifo_push() are usually called
 *       in the producer context.
 */
 bool fifo_isfull(const FIFOPtr *fb)
{
	return
		((fb->head == fb->begin) && (fb->tail == fb->end))
		|| (fb->tail == fb->head - 1);
}

/**
 * Push a character on the fifo buffer.
 *
 * \note Calling \c fifo_push() on a full buffer is undefined.
 *       The caller must make sure the buffer has at least
 *       one free slot before calling this function.
 *
 * \note It is safe to call fifo_pop() and fifo_push() from
 *       concurrent contexts, unless the CPU can't update
 *       a pointer atomically (which the AVR and other 8-bit
 *       processors can't do).
 *
 * \sa fifo_push_locked
 */
 void fifo_push(FIFOPtr *fp, Object *p)
{
	/* Write at tail position */
    Object *_p = *(fp->tail);
    if(_p){
        RELEASE_OBJECT(_p); // release previous object as we'll overwrite the position
    }
	*(fp->tail) = p;

	if (UNLIKELY(fp->tail == fp->end))
		/* wrap tail around */
		fp->tail = fp->begin;
	else
		/* Move tail forward */
		fp->tail++;
}

/**
 * Pop a character from the fifo buffer.
 *
 * \note Calling \c fifo_pop() on an empty buffer is undefined.
 *       The caller must make sure the buffer contains at least
 *       one character before calling this function.
 *
 * \note It is safe to call fifo_pop() and fifo_push() from
 *       concurrent contexts.
 */
 Object* fifo_pop(FIFOPtr *fb)
{
    Object *p = NULL;
	if (UNLIKELY(fb->head == fb->end)){
		/* wrap head around */
		fb->head = fb->begin;
		p = *(fb->end);
        *(fb->end) = NULL;
	}else{
		/* move head forward */
		p = *(fb->head);
        *(fb->head) = NULL;
        fb->head++;
    }
    return p;
}

/**
 * Make the fifo empty, discarding all its current contents.
 */
 void fifo_flush(FIFOPtr *fb){
    // release each object
    while(!fifo_isempty(fb)){
        Object *p = fifo_pop(fb);
        RELEASE_OBJECT(p);
    }
}

/**
 * FIFO Initialization.
 */
 void fifo_init(FIFOPtr *fb, Object **ptrs, size_t size)
{
	/* FIFO buffers have a known bug with 1-byte buffers. */
	ASSERT(size > 1);

	fb->head = fb->tail = fb->begin = ptrs;
	fb->end = ptrs + size - 1;
}

/**
 * \return Lenght of the FIFOBuffer \a fb.
 */
 size_t fifo_len(FIFOPtr *fb)
{
	return fb->end - fb->begin;
}
