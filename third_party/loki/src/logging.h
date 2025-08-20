//
//          Copyright (C) 2020 The CVTE Inc - All rights reserved
//
//                      ██████╗██╗   ██╗████████╗███████╗
//                     ██╔════╝██║   ██║╚══██╔══╝██╔════╝
//                     ██║     ██║   ██║   ██║   █████╗
//                     ██║     ╚██╗ ██╔╝   ██║   ██╔══╝
//                     ╚██████╗ ╚████╔╝    ██║   ███████╗
//                      ╚═════╝  ╚═══╝     ╚═╝   ╚══════╝
//
//                          yangguang@cvte.com

#pragma once

#include <iostream>

#define CHECK(val)		\
	if (!(val)) {		\
	std::cerr << "CHECK failed" << std::endl		\
	<< #val << "=" << (val) << std::endl;							\
	abort();														\
	}

#define CHECK_NULL(val)				\
	if ((val) != NULL) {			\
	std::cerr << "CHECK_NULL failed" << std::endl		\
	<< #val << "!=NULL" << std::endl;							\
	abort();		\
	}

#define CHECK_NOTNULL(val)		\
	if ((val) == NULL) {			\
	std::cerr << "CHECK_NOTNULL failed" << std::endl		\
	<< #val << "==NULL" << std::endl;							\
	abort();		\
	}

#define CHECK_NE(val1, val2)		\
	if (!((val1) != (val2))) {			\
		std::cerr << "CHECK_NE failed" \
		<< #val1 << "=" << (val1) << std::endl				\
		<< #val2 << "=" << (val2) << std::endl;			\
	}

#define CHECK_EQ(val1, val2)		\
	if (!((val1) == (val2))) {			\
		std::cerr << "CHECK_EQ failed" \
		<< #val1 << "=" << (val1) << std::endl				\
		<< #val2 << "=" << (val2) << std::endl;			\
	}

#define CHECK_GE(val1, val2)		\
	if (!((val1) >= (val2))) {			\
		std::cerr << "CHECK_GE failed" \
		<< #val1 << "=" << (val1) << std::endl				\
		<< #val2 << "=" << (val2) << std::endl;			\
	}

#define CHECK_GT(val1, val2)		\
	if (!((val1) > (val2))) {			\
		std::cerr << "CHECK_GE failed" \
		<< #val1 << "=" << (val1) << std::endl				\
		<< #val2 << "=" << (val2) << std::endl;			\
	}

#define CHECK_LT(val1, val2)		\
	if (!((val1) < (val2))) {			\
		std::cerr << "CHECK_LT failed" \
		<< #val1 << "=" << (val1) << std::endl				\
		<< #val2 << "=" << (val2) << std::endl;			\
	}


#if defined(NDEBUG)
#define DCHECK_IS_ON 0
#else
#define DCHECK_IS_ON 1
#endif

#if (DCHECK_IS_ON == 1)

#define DCHECK(condition)		\
	CHECK(condition)

#define DCHECK_NOTNULL(val1)		\
	CHECK_NOTNULL(val1)			

#define DCHECK_NULL(val1)			\
	CHECK_NULL(val1)			

#define	DCHECK_EQ(val1, val2)				\
	CHECK_EQ(val1, val2)					\

#define DCHECK_NE(val1 ,val2)				\
	CHECK_NE(val1, val2)

#define DCHECK_GE(val1 ,val2)				\
	CHECK_GE(val1, val2)

#define DCHECK_GT(val1 ,val2)				\
	CHECK_GT(val1, val2)

#define DCHECK_LT(val1 ,val2)				\
	CHECK_LT(val1, val2)

#else 

#define DCHECK(condition)	
#define DCHECK_NOTNULL(val1)
#define DCHECK_NULL(val1)
#define DCHECK_EQ(val1, val2)
#define DCHECK_NE(val1, val2)
#define DCHECK_GE(val1, val2)
#define DCHECK_LT(val1, val2)
#endif // defined(NDEBUG)
