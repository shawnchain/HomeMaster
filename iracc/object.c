/*
 * object.c
 *
 *  Created on: 2017年4月1日
 *      Author: shawn
 */

#include "object.h"
#include "lib.h"

static Object* retain_object(Object *o){
    ATOMIC(o->refcount++;);
    return o;
}

static Object* release_object(Object *o){
    if(o->refcount > 0){
        ATOMIC(o->refcount--;);
        if(o->refcount > 0){
            return o; // object still alive
        }else{
            i_free((void*)o);
        }
    }
    return NULL;
}

Object* alloc_object(size_t size){
    if(size < sizeof(Object)) return NULL;
    Object *p = (Object*)i_malloc(size);
    i_memset(p,0,size);
    p->refcount = 1;
    p->release_ptr = release_object;
    p->retain_ptr = retain_object;
    return p;
}
