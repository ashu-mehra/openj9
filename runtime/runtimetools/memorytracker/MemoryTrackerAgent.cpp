/*******************************************************************************
 * Copyright (c) 2011, 2014 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/
#if defined(WIN32)
#ifdef __cplusplus
extern "C" {
#endif
#include <windows.h>
#include <stdio.h>
#include <psapi.h>
#ifdef __cplusplus
}
#endif
#endif
#include "MemoryTrackerAgent.hpp"
#include "thread_api.h"

#include <unistd.h>
#include <sys/mman.h>
#include <inttypes.h>

/* used to store reference to the agent */
static MemoryTrackerAgent* memoryTrackerAgent;
#if 0
uintptr_t omrResidentMemory[100];
uintptr_t languageResidentMemory[100];
uintptr_t omrCumulativeMemory[100];
uintptr_t languageCumulativeMemory[100];
int omrDepth[100];
int languageDepth[100];

int indent = 0;
int indentIncrement = 3;
/**
 * Callback that handles printing out the memory categories
 *
 */
static UDATA displayMemoryTree(U_32 categoryCode, const char * categoryName, UDATA liveBytes, UDATA liveAllocations, BOOLEAN isRoot, U_32 parentCategoryCode, OMRMemCategoryWalkState * state)
{
	int *depthPtr = languageDepth;
	int *parentDepthPtr = languageDepth;
	uintptr_t memoryUsage = 0;

	if (categoryCode > OMRMEM_LANGUAGE_CATEGORY_LIMIT) {
		depthPtr = omrDepth;
		categoryCode = OMRMEM_OMR_CATEGORY_INDEX_FROM_CODE(categoryCode);
		memoryUsage = omrCumulativeMemory[categoryCode];
	} else {
		memoryUsage = languageCumulativeMemory[categoryCode];
	}

	if (memoryUsage == 0) {
		return J9MEM_CATEGORIES_KEEP_ITERATING;
	}

	if (parentCategoryCode > OMRMEM_LANGUAGE_CATEGORY_LIMIT) {
		parentDepthPtr = omrDepth;
		parentCategoryCode = OMRMEM_OMR_CATEGORY_INDEX_FROM_CODE(parentCategoryCode);
	}

	if (isRoot) {
		depthPtr[categoryCode] = 0;
	} else {
		depthPtr[categoryCode] = parentDepthPtr[parentCategoryCode] + 1;
	}
	if (!isRoot) {
		int i = 0;
		
		indent = indentIncrement * depthPtr[categoryCode];

		for (i = 0; i <= indent-indentIncrement; i++) {
			if (i % indentIncrement == 0) {
				fprintf(stdout, "|");
			} else {
				fprintf(stdout, " ");
			}
		}
		fprintf(stdout, "\n");

		for (i = 0; i < indent-indentIncrement; i++) {
			if (i % indentIncrement == 0) {
				fprintf(stdout, "|");
			} else {
				fprintf(stdout, " ");
			}
		}

		for (; i < indent; i++) {
			if (i == indent-indentIncrement) {
				fprintf(stdout, "+");
			} else {
				fprintf(stdout, "-");
			}
		}
	}
	fprintf(stdout, "%s: %" PRIdPTR "\n", categoryName, memoryUsage);
	return J9MEM_CATEGORIES_KEEP_ITERATING;
}
/**
 * Callback that calculates memory for each category by adding up all its children's memory usage
 *
 */
static UDATA memCategoryCallBack (U_32 categoryCode, const char * categoryName, UDATA liveBytes, UDATA liveAllocations, BOOLEAN isRoot, U_32 parentCategoryCode, OMRMemCategoryWalkState * state)
{
	if (!isRoot) {
		uintptr_t *categoryMemoryPtr = languageCumulativeMemory;
		uintptr_t *parentCategoryMemoryPtr = languageCumulativeMemory;

		if (categoryCode > OMRMEM_LANGUAGE_CATEGORY_LIMIT) {
			categoryCode = OMRMEM_OMR_CATEGORY_INDEX_FROM_CODE(categoryCode);
			categoryMemoryPtr = omrCumulativeMemory;
		}

		if (parentCategoryCode > OMRMEM_LANGUAGE_CATEGORY_LIMIT) {
			parentCategoryCode = OMRMEM_OMR_CATEGORY_INDEX_FROM_CODE(parentCategoryCode);
			parentCategoryMemoryPtr = omrCumulativeMemory;
		}

		parentCategoryMemoryPtr[parentCategoryCode] += categoryMemoryPtr[categoryCode];
	}

	return J9MEM_CATEGORIES_KEEP_ITERATING;
}

/**
 * Callback used by to count memory categories with j9mem_walk_categories
 */
static UDATA
countMemoryCategoriesCallback (OMRMemCategoryWalkState * state)
{
	state->userData1 = (void *)((UDATA)state->userData1 + 1);
	return J9MEM_CATEGORIES_KEEP_ITERATING;
}
#endif

/*********************************************************
 * Methods exported for agent
 */
#ifdef __cplusplus
extern "C" {
#endif

void JNICALL
Agent_OnUnload(JavaVM * vm)
{
	MemoryTrackerAgent* agent = MemoryTrackerAgent::getAgent(vm, &memoryTrackerAgent);
	agent->shutdown();
	agent->kill();
}

jint JNICALL
Agent_OnLoad(JavaVM * vm, char * options, void * reserved)
{
	MemoryTrackerAgent* agent = MemoryTrackerAgent::getAgent(vm, &memoryTrackerAgent);
	return agent->setup(options);
}

jint JNICALL
Agent_OnAttach(JavaVM * vm, char * options, void * reserved)
{
	MemoryTrackerAgent* agent = MemoryTrackerAgent::getAgent(vm, &memoryTrackerAgent);
	return  agent->setup(options);
	/* need to add code here that will get jvmti_env and jni env and then call startGenerationThread */
}

#ifdef __cplusplus
}
#endif

/************************************************************************
 * Start of class methods
 *
 */

/**
 * This method is used to initialize an instance
 * @returns true if initialiization was successful
 */
bool MemoryTrackerAgent::init()
{
	PORT_ACCESS_FROM_JAVAVM(getJavaVM());

	if (!RuntimeToolsIntervalAgent::init()){
		return false;
	}

	return true;

}

/**
 * This method should be called to destroy the object and free the associated memory
 */
void MemoryTrackerAgent::kill()
{
	PORT_ACCESS_FROM_JAVAVM(getJavaVM());
	j9mem_free_memory(this);
}


/**
 * This method is used to create an instance
 *
 * @param vm java vm that can be used by this manager
 * @returns an instance of the class
 */
MemoryTrackerAgent* MemoryTrackerAgent::newInstance(JavaVM * vm)
{
	J9JavaVM * javaVM = ((J9InvocationJavaVM *)vm)->j9vm;
	PORT_ACCESS_FROM_JAVAVM(javaVM);
	MemoryTrackerAgent* obj = (MemoryTrackerAgent*) j9mem_allocate_memory(sizeof(MemoryTrackerAgent), OMRMEM_CATEGORY_VM);
	if (obj) {
		new (obj) MemoryTrackerAgent(vm);
		if (!obj->init()){
			obj->kill();
			obj = NULL;
		}
	}
	return obj;
}

#define ROUNDING_GRANULARITY    8
#define ROUNDED_FOOTER_OFFSET(number)   (((number) + (ROUNDING_GRANULARITY - 1) + sizeof(struct J9MemTag)) & ~(uintptr_t)(ROUNDING_GRANULARITY - 1))
#define ROUNDED_BYTE_AMOUNT(number)             (ROUNDED_FOOTER_OFFSET(number) + sizeof(struct J9MemTag))

#define ROUND_DOWN_TO(number, granularity) ((number) & ~(granularity-1))
#define ROUND_UP_TO(number, granularity) (((number) + (granularity-1)) & ~(granularity-1))

#define PAGE_IS_RESIDENT(value) ((value) & 0x1)
/**
 * method that generates the calls to runAction at the appropriate intervals *.
 */
void MemoryTrackerAgent::runAction()
{
#if 0
	PORT_ACCESS_FROM_JAVAVM(getJavaVM());
	OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);
	struct J9MemTag *ptr = NULL;
	intptr_t pageSize = getpagesize();
	int32_t rc = 0;

	memset(omrResidentMemory, 0, sizeof(omrResidentMemory));
	memset(languageResidentMemory, 0, sizeof(languageResidentMemory));

	omrmem_get_memory_list_monitor();

	ptr = omrmem_get_memory_list_head();
	while (ptr != NULL) {
		uintptr_t memoryCounter = 0;
		uintptr_t startAddr = (uintptr_t)ptr;
		uintptr_t endAddr = startAddr + ROUNDED_BYTE_AMOUNT(ptr->allocSize);
		uintptr_t roundedStart = ROUND_DOWN_TO(startAddr, pageSize);
		uintptr_t roundedEnd = ROUND_UP_TO(endAddr, pageSize);
		size_t length = roundedEnd - roundedStart;
		int32_t numPages = length/pageSize;
		uint8_t vec[numPages];

		memset(vec, 0, numPages);
		rc = mincore((void *)roundedStart, length, (unsigned char *)vec);
		if (0 != rc) {
			perror("mincore failed");
			return;
		} else {
			uintptr_t page = 0;

			for (page = roundedStart; page < roundedEnd; page += pageSize) {
				int32_t pageIndex = (page - roundedStart) / pageSize;

				if (PAGE_IS_RESIDENT(vec[pageIndex])) {
					memoryCounter += pageSize;
				}
			}
			/* subtract the part not present in first and last page */
			if (PAGE_IS_RESIDENT(vec[0])) {
				memoryCounter -= (startAddr - roundedStart);
			}
			if (PAGE_IS_RESIDENT(vec[numPages - 1])) {
				memoryCounter -= (roundedEnd - endAddr);
			}
		}
		uint32_t categoryCode = ptr->category->categoryCode;
		if (categoryCode > OMRMEM_LANGUAGE_CATEGORY_LIMIT) {
			categoryCode = OMRMEM_OMR_CATEGORY_INDEX_FROM_CODE(categoryCode);
			omrResidentMemory[categoryCode] += memoryCounter;
		} else {
			languageResidentMemory[categoryCode] += memoryCounter;
		}
		ptr = ptr->next;
	}
	omrmem_release_memory_list_monitor();

	omrthread_update_memory_usage();

	memcpy(omrCumulativeMemory, omrResidentMemory, sizeof(omrResidentMemory));
	memcpy(languageCumulativeMemory, languageResidentMemory, sizeof(languageResidentMemory));

	if (0 == rc) {
		OMRMemCategoryWalkState walkState;
		memset(&walkState, 0, sizeof(OMRMemCategoryWalkState));
		walkState.walkFunction = &memCategoryCallBack;
		walkState.userData1 = this;
		walkState.postOrder = TRUE;
		j9mem_walk_categories(&walkState);

		memset(&walkState, 0, sizeof(OMRMemCategoryWalkState));
		walkState.walkFunction = &displayMemoryTree;
		walkState.userData1 = this;
		walkState.postOrder = FALSE;
		j9mem_walk_categories(&walkState);
		message("\n");
	}
#endif /* 0 */
}

/**
 * This is called once for each argument, the agent should parse the arguments
 * it supports and pass any that it does not understand to parent classes
 *
 * @param options string containing the argument
 *
 * @returns one of the AGENT_ERROR_ values
 */
jint MemoryTrackerAgent::parseArgument(char* option)
{
	PORT_ACCESS_FROM_JAVAVM(getJavaVM());
	jint rc = AGENT_ERROR_NONE;

	/* check for threshHold option */
	rc = RuntimeToolsIntervalAgent::parseArgument(option);

	return rc;
}
