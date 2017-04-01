/*
 * lib.h
 *
 *  Created on: 2017年4月1日
 *      Author: shawn
 */

#ifndef IRACC_LIB_H_
#define IRACC_LIB_H_

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef ATOMIC
#define ATOMIC(x) x
#endif

#ifndef INLINE
#define INLINE static inline
#endif

#ifndef i_malloc
#define i_malloc(x) malloc(x)
#endif

#ifndef i_memset
#define i_memset(x,y,z) memset(x,y,z)
#endif

#ifndef i_memcpy
#define i_memcpy(x,y,z) memcpy(x,y,z)
#endif

#ifndef i_free
#define i_free(x) free(x)
#endif

#ifndef UNLIKELY
#define UNLIKELY(x) x
#endif

#ifndef ASSERT
#define ASSERT(x)
#endif

#ifndef MIN
#define MIN(x,y)  ((x<y)?x:y)
#endif

#include "log.h"
#include "object.h"

#endif /* IRACC_LIB_H_ */
