//#include "d912pxy_mem_mgr.h"
/*
MIT License

Copyright(c) 2019 AlraiLux

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Alrai: We're putting all calls to malloc, realloc, etc. here so it's easier to 
change stuff later. Hopefully we'll catch possible nullptr's that can happen when 
system is under stress or whenever alloc calls just decide to fail.
*/

#include "stdafx.h"
#include "d912pxy_stackwalker.h"

static UINT32 recordOprtNewCaller = 0;

void * operator new(std::size_t n)
{
#ifdef _DEBUG
	void* ret = NULL;

	if (!d912pxy_s(memMgr)) {			   
		return d912pxy_mem_mgr::pxy_malloc_dbg_uninit(n, __FILE__, __LINE__, __FUNCTION__);
	}
	else {
		if (recordOprtNewCaller)
			d912pxy_s(memMgr)->pxy_malloc_dbg(&ret, n, 0, 0, "operator new");
		else 
			d912pxy_s(memMgr)->pxy_malloc_dbg(&ret, n, "<no data on new caller>", __LINE__, "operator new");
	}

#else
	void* ret;
	PXY_MALLOC(ret, n, void*);
#endif	
	return ret;
}

void operator delete(void * p) 
{
	PXY_FREE(p);
}


d912pxy_mem_mgr::d912pxy_mem_mgr() : d912pxy_noncom() {	
#ifdef _DEBUG
	blocksAllocated.SetValue(0);
	blockList.clear();

	stkWlk = new d912pxy_StackWalker(3, 4);

	recursionCheck = 0;
#endif

	d912pxy_s(memMgr) = this;
}

d912pxy_mem_mgr::~d912pxy_mem_mgr() {
#ifdef _DEBUG
	delete stkWlk;	
	LogLeaked();
#endif

/*	free(d912pxy_s(memMgr));
	d912pxy_s(memMgr) = NULL;*/
}

void* d912pxy_mem_mgr::inRealloc(void* block, size_t sz) { // Returns pointer or nullptr if failed.
	
	return realloc(block, sz);

}

void* d912pxy_mem_mgr::inMalloc(size_t sz) { // Returns pointer or nullptr if failed.

	return malloc(sz);

}

void d912pxy_mem_mgr::inFree(void* block) {

	free(block); 

}


bool d912pxy_mem_mgr::pxy_malloc_dbg(void** cp, size_t sz, const char* file, const int line, const char* function) { // Calls pxy_malloc until it gets a success or fails after trying "tries" times.
#ifdef _DEBUG
	if (*cp != NULL) { // Were we passed a non null pointer to malloc? Possible memory leak condition.
		LOG_ERR_DTDM("A malloc was called with a possible valid pointer. Size requested: %u. %S %u %S", sz, file, line, function);
		//pxy_free(*cp); // Let's free that current pointer to avoid a memory leak. // Nevermind, we can't assume just because it isn't null that we should free it... but we will log it.
	}

	sz += sizeof(d912pxy_dbg_mem_block);

	void* tempPointer = inMalloc(sz);

	if (!tempPointer)
	{
		LOG_ERR_DTDM("malloc_retry(%llu) called @ %S %u %S", sz, file, line, function);

		tempPointer = pxy_malloc_retry(sz);

		if (!tempPointer)
		{
			LOG_ERR_DTDM("A malloc failed. Debug Info: %S %u %S", file, line, function);			
			return false;
		}
	}

	d912pxy_dbg_mem_block* blkdsc = (d912pxy_dbg_mem_block*)tempPointer;

	blkdsc->file = (char*)file;
	blkdsc->function = (char*)function;
	blkdsc->line = line;
	blkdsc->sz = sz;
	blkdsc->trashCheck = 0xAAAAAAAA;

	*cp = (void*)((intptr_t)tempPointer + sizeof(d912pxy_dbg_mem_block));

	blocksAllocated.Hold();

	if (recursionCheck || !allowTrackBlocks)
	{
		blkdsc->trashCheck = 0xAAAAAAA9;
		blocksAllocated.Release();
		return true;
	}

	recursionCheck = 1;

	if (file == NULL)
	{
		stkWlk->ShowCallstack();

		
		blkdsc->file = stkWlk->ReturnCaller();
	}

	blockList[blocksAllocated.Add(1)] = blkdsc;
	blkdsc->uid = blocksAllocated.GetValue();

	recursionCheck = 0;

	blocksAllocated.Release();

#endif
	return true;
}

bool d912pxy_mem_mgr::pxy_realloc_dbg(void** cp, size_t sz, const char* file, const int line, const char* function) { // Calls pxy_realloc until it gets a success or fails after trying "tries" times.
	
	void* origBlk = (void*)((intptr_t)(*cp) - sizeof(d912pxy_dbg_mem_block));

	sz += sizeof(d912pxy_dbg_mem_block);

	void* tempPointer = inRealloc(origBlk, sz);

	if (!tempPointer)
	{
		LOG_ERR_DTDM("realloc_retry(%llu) called @ %S %u %S", sz, file, line, function);

		tempPointer = pxy_realloc_retry(origBlk, sz);

		if (!tempPointer)
		{
			LOG_ERR_DTDM("A realloc failed. Debug Info: %S %u %S", file, line, function);
			return false;
		}
	}

	d912pxy_dbg_mem_block* blkdsc = (d912pxy_dbg_mem_block*)tempPointer;

	blkdsc->file = (char*)file;
	blkdsc->function = (char*)function;
	blkdsc->line = line;
	blkdsc->sz = sz;	

	*cp = (void*)((intptr_t)tempPointer + sizeof(d912pxy_dbg_mem_block));

	return true;

}

void d912pxy_mem_mgr::pxy_free_dbg(void** cp, const char* file, const int line, const char* function) { // Free and NULL
#ifdef _DEBUG
	if (*cp == NULL)
	{
		LOG_ERR_DTDM("free(NULL) @ %S %u %S", file, line, function);		
	}

	void* origBlk = (void*)((intptr_t)(*cp) - sizeof(d912pxy_dbg_mem_block));
	d912pxy_dbg_mem_block* blkdsc = (d912pxy_dbg_mem_block*)origBlk;
							  
	if (blkdsc->trashCheck != 0xAAAAAAAA)
	{
		if (blkdsc->trashCheck == 0xAAAAAAA9)
		{
			goto justFree;
		}

		LOG_ERR_DTDM("free trash check failed @ %S %u %S", file, line, function);
		*cp = NULL;
		return;
	}
	else {
		blkdsc->trashCheck = 0;
	}

	blocksAllocated.Hold();
	blockList.erase(blkdsc->uid);	
	blocksAllocated.Release();

	if (blkdsc->line == 0)
		free(blkdsc->file);

justFree:
	inFree(origBlk);
	*cp = NULL;	
#endif
}

void * d912pxy_mem_mgr::pxy_malloc_retry(size_t sz)
{
	void* tempPointer;

	for (UINT i = 0; i < PXY_MEM_MGR_TRIES; i++) {

		tempPointer = inMalloc(sz);

		if (tempPointer != NULL) {		
			return tempPointer;
		}

		Sleep(PXY_MEM_MGR_RETRY_WAIT); // Wait a moment
	}

	return NULL;
}

void * d912pxy_mem_mgr::pxy_realloc_retry(void * block, size_t sz)
{
	void* tempPointer;

	for (UINT i = 0; i < PXY_MEM_MGR_TRIES; i++) {

		tempPointer = inRealloc(block, sz);

		if (tempPointer != NULL) {			
			return tempPointer;
		}

		Sleep(PXY_MEM_MGR_RETRY_WAIT); // Wait a moment
	}

	return NULL;
}

void * d912pxy_mem_mgr::pxy_malloc(size_t sz)
{
	void* ret = inMalloc(sz);

	if (!ret)
	{
		ret = pxy_malloc_retry(sz);

		if (!ret)
			LOG_ERR_THROW2(-1, "out of memory @ malloc");
	}

	return ret;
}

void * d912pxy_mem_mgr::pxy_realloc(void * block, size_t sz)
{
	void* ret = inRealloc(block, sz);

	if (!ret)
	{
		ret = pxy_realloc_retry(block, sz);

		if (!ret)
			LOG_ERR_THROW2(-1, "out of memory @ realloc");
	}

	return ret;

}

void d912pxy_mem_mgr::pxy_free(void * block)
{
	inFree(block);
}

void * d912pxy_mem_mgr::pxy_malloc_dbg_uninit(size_t sz, const char * file, const int line, const char * function)
{
	sz += sizeof(d912pxy_dbg_mem_block);

	void* tempPointer = malloc(sz);

	if (!tempPointer)
	{
		//megai2: that is critical
		MessageBox(0, L"memmgr pre init malloc fail", L"d912pxy", MB_ICONERROR);
		TerminateProcess(GetCurrentProcess(), -1);
	}

	d912pxy_dbg_mem_block* blkdsc = (d912pxy_dbg_mem_block*)tempPointer;

	blkdsc->file = (char*)file;
	blkdsc->function = (char*)function;
	blkdsc->line = line;
	blkdsc->sz = sz;
	blkdsc->trashCheck = 0xAAAAAAA9;	

	return (void*)((intptr_t)tempPointer + sizeof(d912pxy_dbg_mem_block));
}

void d912pxy_mem_mgr::LogLeaked()
{
#ifdef _DEBUG
	if (!blockList.empty())
	{
		LOG_ERR_DTDM("found %llu leaked dynamic allocated blocks", blockList.size());

		for (std::map<UINT, d912pxy_dbg_mem_block*>::iterator it = blockList.begin(); it != blockList.end(); ++it)
		{
			LOG_ERR_DTDM("%S %u %S : %u bytes", it->second->file, it->second->line, it->second->function, it->second->sz);
		}

		blockList.clear();
	}
#endif
}

void d912pxy_mem_mgr::PostInit()
{
	NonCom_Init(NULL, L"memmgr");
	recordOprtNewCaller = d912pxy_s(config)->GetValueUI32(PXY_CFG_LOG_DBG_MEM_MGR_SAVE_NEW_CALLER);
}