/*
 * object.h
 *
 *  Created on: 2017年4月1日
 *      Author: shawn
 */

#ifndef IRACC_OBJECT_H_
#define IRACC_OBJECT_H_

#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

////////////////////////////////////////////////////////////////////////////////
// Object
////////////////////////////////////////////////////////////////////////////////
struct Object;
typedef struct Object{
    volatile uint8_t refcount;
    struct Object* (*release_ptr)(struct Object*);
    struct Object* (*retain_ptr)(struct Object*);
}Object;

Object* alloc_object(size_t size);
#define RELEASE_OBJECT(x) ((x && ((Object*)x)->release_ptr)?(((Object*)x)->release_ptr((Object*)x)):NULL)
#define RETAIN_OBJECT(x) ((x && ((Object*)x)->retain_ptr)?(((Object*)x)->retain_ptr((Object*)x)):NULL)

#define OBJECT_RELEASE(x) RELEASE_OBJECT(x)
#define OBJECT_RETAIN(x) RETAIN_OBJECT(x)

#ifdef __cplusplus
}
#endif

#endif /* IRACC_OBJECT_H_ */
