#include "memory_manager.h"

#include <iostream>
#include <cassert>

size_t CustomMemoryManagerConstants::getPageNum(size_t ptr) { return ptr >> 21; }
size_t CustomMemoryManagerConstants::getPageNum(void* ptr) { return (size_t)ptr >> 21; }
int CustomMemoryManagerConstants::getPageHash(size_t ptr) { return (ptr >> 21) & (TOTAL_PAGE_NUM-1); }
int CustomMemoryManagerConstants::getPageHash(void* ptr) { return ((size_t)ptr >> 21) & (TOTAL_PAGE_NUM-1); }
int CustomMemoryManagerConstants::getSmallPageNum(size_t ptr) { return (ptr & ((1 << 21) - 1)) >> 12; }
int CustomMemoryManagerConstants::getSmallPageNum(void* ptr) { return ((size_t)ptr & ((1 << 21) - 1)) >> 12; }

using namespace CustomMemoryManagerConstants;

CustomMemoryManager::CustomMemoryManager():
	internalPool(MemoryListPool(this, INTERNAL_POOL_SIZE))
{
	const size_t from = (size_t)internalPool.baseAddress;
	const size_t to = from + INTERNAL_POOL_SIZE - PAGE_SIZE;
	for (size_t pageAddress = from; pageAddress <= to; pageAddress += PAGE_SIZE)
	{
		int pageHash = getPageHash(pageAddress);
		size_t pageNum = getPageNum(pageAddress);
		void* address = internalPool.allocate(sizeof(Page));
		Page* page = new (address) Page(Page::PageType::INTERNAL, &internalPool, pageNum);
		pages[pageHash].push_back(page);
	}

	//std::cout << getPageNum((size_t)internalPool.baseAddress) << std::endl;
	//std::cout << getPageNum(to) << std::endl;
}

CustomMemoryManager::~CustomMemoryManager()
{

}

void CustomMemoryManager::grow()
{
	void* ptr = internalPool.allocate(sizeof(MemoryListPool));
	MemoryListPool* hugePool = new (ptr) MemoryListPool(this, nextHugePoolSize);
	hugePools.push_back(hugePool);
	const size_t from = (size_t)hugePool->baseAddress;
	const size_t to = from + nextHugePoolSize - PAGE_SIZE;
	for (size_t pageAddress = from; pageAddress <= to; pageAddress += PAGE_SIZE)
	{
		int pageHash = getPageHash(pageAddress);
		size_t pageNum = getPageNum(pageAddress);
		void* address = internalPool.allocate(sizeof(Page));
		Page* page = new (address) Page(Page::PageType::HUGE, hugePools.back(), pageNum);
		pages[pageHash].push_back(page);
	}
	nextHugePoolSize <<= 1;

	// std::cout << getPageNum(address) << std::endl;
	// std::cout << getPageNum(to) << std::endl;
}

void* CustomMemoryManager::allocate(size_t size)
{
	// find an available memory pool of the right size
	if (size <= SMALL_THRESHOLD)
	{
		int index = std::lower_bound(SMALL_BLOCK_SIZES.begin(), SMALL_BLOCK_SIZES.end(), size) - SMALL_BLOCK_SIZES.begin();
		return allocateFromBlockPool(smallMutexes[index], freeSmallPools[index], true, SMALL_BLOCK_SIZES[index]);
	}
	else if (size <= LARGE_THRESHOLD)
	{
		int index = std::lower_bound(LARGE_BLOCK_SIZES.begin(), LARGE_BLOCK_SIZES.end(), size) - LARGE_BLOCK_SIZES.begin();
		return allocateFromBlockPool(largeMutexes[index], freeLargePools[index], false, LARGE_BLOCK_SIZES[index]);
	}
	else
	{
		return allocateFromListPool(size);
	}
}

void* CustomMemoryManager::allocateFromBlockPool(std::shared_mutex& mutex, std::list<MemoryBlockPool*>& pools, bool isSmallPool, int blockSize)
{
	{
		std::shared_lock<std::shared_mutex> lock(mutex);
		if (!pools.empty())
		{
			auto pool = pools.front();
			void* ptr = pool->allocate(0);
			if (ptr != nullptr)
				return ptr;
		}
	}

	{
		std::unique_lock<std::shared_mutex> lock(mutex);
		while (!pools.empty())
		{
			auto pool = pools.front();
			void* ptr = pool->allocate(0);
			if (ptr != nullptr)
				return ptr;
			pool->isOnQueue = false;
			pools.pop_front();
		}
		if (isSmallPool)
		{
			auto pool = allocateSmallPage(blockSize, pools);
			void* ptr = pool->allocate(0);
			assert(ptr != nullptr);
			return ptr;
		}
		else if (blockSize == SMALL_POOL_SIZE)
		{
			auto pool = allocateSmallBlockPoolPage();
			void* ptr = pool->allocate(0);
			assert(ptr != nullptr);
			return ptr;
		}
		else
		{
			auto pool = allocateLargeBlockPoolPage(blockSize, pools);
			void* ptr = pool->allocate(0);
			assert(ptr != nullptr);
			return ptr;
		}
	}
}

void* CustomMemoryManager::allocateFromListPool(size_t size)
{
	std::unique_lock<std::shared_mutex> lock(hugePoolsMutex);
	for (auto pool : hugePools)
	{
		void* ptr = pool->allocate(size);
		if (ptr != nullptr)
			return ptr;
	}
	while (true)
	{
		grow();
		void* ptr = hugePools.back()->allocate(size);
		if (ptr != nullptr)
			return ptr;
	}
}

void* CustomMemoryManager::allocateFromInternalPool(size_t size)
{
	std::unique_lock<std::shared_mutex> lock(internalPoolMutex);
	void* ptr = internalPool.allocate(size);
	assert(ptr != nullptr);
	return ptr;
}

MemoryBlockPool* CustomMemoryManager::allocateLargeBlockPoolPage(int blockSize, std::list<MemoryBlockPool*>& pools)
{
	return allocatePage(false, blockSize, pools);
}

MemoryBlockPool* CustomMemoryManager::allocateSmallBlockPoolPage()
{
	return allocatePage(true, SMALL_POOL_SIZE, freeSmallBlockPoolPages);
}

MemoryBlockPool* CustomMemoryManager::allocatePage(bool forSmallPages, int blockSize, std::list<MemoryBlockPool*>& pools)
{
	std::unique_lock<std::shared_mutex> lock(hugePoolsMutex);

	void* dataAddress = nullptr;
	MemoryListPool* hugePool = nullptr;
	for (auto pool : hugePools)
	{
		dataAddress = pool->allocateAligned(LARGE_POOL_SIZE);
		if (dataAddress != nullptr) {
			hugePool = pool;
			break;
		}
	}

	if (dataAddress == nullptr)
	{
		grow();
		hugePool = hugePools.back();
		dataAddress = hugePool->allocateAligned(LARGE_POOL_SIZE);
	}

	assert(dataAddress != nullptr);
	int pageHash = getPageHash(dataAddress);
	size_t pageNum = getPageNum(dataAddress);

	Page* newPage;
	MemoryBlockPool* dataPool;
	if (forSmallPages)
	{
		void* ptr = allocateFromInternalPool(sizeof(SmallBlockPoolPage));
		newPage = new (ptr) SmallBlockPoolPage(hugePool, pageNum, this, dataAddress, LARGE_POOL_SIZE, blockSize, pools);
		dataPool = &((SmallBlockPoolPage*)newPage)->dataPool;
	}
	else
	{
		void* ptr = allocateFromInternalPool(sizeof(SmallBlockPoolPage));
		newPage = new (ptr) LargeBlockPoolPage(hugePool, pageNum, this, dataAddress, LARGE_POOL_SIZE, blockSize, pools);
		dataPool = &((LargeBlockPoolPage*)newPage)->dataPool;
	}

	for (auto& page : pages[pageHash])
	{
		if (page->pageNum == pageNum)
		{
			// delete page;
			freeFromInternalPool(page);
			page = newPage;
			return dataPool;
		}
	}

	// std::cout << pageNum << std::endl;

	// not reachable
	assert(false);
	return nullptr;
}

MemoryBlockPool* CustomMemoryManager::allocateSmallPage(int blockSize, std::list<MemoryBlockPool*>& pools)
{
	void* dataAddress = allocateFromBlockPool(smallPagePoolMutex, freeSmallBlockPoolPages, false, SMALL_POOL_SIZE);
	int pageHash = getPageHash(dataAddress);
	size_t pageNum = getPageNum(dataAddress);
	int smallPageNum = getSmallPageNum(dataAddress);
	for (auto page : pages[pageHash])
	{
		if (page->pageNum == pageNum)
		{
			SmallBlockPoolPage* sPage = (SmallBlockPoolPage*)page;
			void* poolAddress = allocateFromInternalPool(sizeof(MemoryBlockPool));
			auto pool = new (poolAddress) MemoryBlockPool(this, dataAddress, SMALL_POOL_SIZE, blockSize, pools);
			assert(sPage->smallPools[smallPageNum] == nullptr);
			sPage->smallPools[smallPageNum] = pool;
			return pool;
		}
	}
	// not reachable
	assert(false);
	return nullptr;
}

void CustomMemoryManager::free(void* ptr)
{
	int pageHash = getPageHash(ptr);
	size_t pageNum = getPageNum(ptr);
	for (auto page : pages[pageHash])
	{
		if (page->pageNum == pageNum)
		{
			switch (page->t)
			{
			case Page::PageType::INTERNAL:
			{
				// not supposed to come here
				assert(false);
				freeFromInternalPool(ptr);
				return;
			}
			case Page::PageType::HUGE:
			{
				auto pool = page->hugePool;
				freeFromListPool(ptr, pool);
				return;
			}
			case Page::PageType::LARGE:
			{
				LargeBlockPoolPage* lPage = (LargeBlockPoolPage*)page;
				auto pool = &(lPage->dataPool);
				int index = std::lower_bound(LARGE_BLOCK_SIZES.begin(), LARGE_BLOCK_SIZES.end(), pool->blockSize) - LARGE_BLOCK_SIZES.begin();
				freeFromBlockPool(ptr, pool, largeMutexes[index], freeLargePools[index], false);
				return;
			}
			case Page::PageType::SMALL:
			{
				SmallBlockPoolPage* sPage = (SmallBlockPoolPage*)page;
				int smallPageNum = getSmallPageNum(ptr);
				auto pool = sPage->smallPools[smallPageNum];
				int index = std::lower_bound(SMALL_BLOCK_SIZES.begin(), SMALL_BLOCK_SIZES.end(), pool->blockSize) - SMALL_BLOCK_SIZES.begin();
				freeFromBlockPool(ptr, pool, smallMutexes[index], freeSmallPools[index], true);
				return;
			}
			}
		}
	}

}

void CustomMemoryManager::freeFromBlockPool(void* ptr, MemoryBlockPool* pool, std::shared_mutex& mutex, std::list<MemoryBlockPool*>& pools, bool isSmallPool)
{
	size_t poolSize = isSmallPool ? SMALL_POOL_SIZE : LARGE_POOL_SIZE;
	size_t freeSpace = pool->free(ptr);
	if (freeSpace < poolSize * 3 / 8)
		return;

	// std::cout << freeSpace << std::endl;

	if (!pool->isOnQueue)
	{
		std::unique_lock<std::shared_mutex> lock(mutex);
		if (!pool->isOnQueue)
		{
			pools.push_back(pool);
			pool->it = --pools.end();
			pool->isOnQueue = true;
		}
	}
	if (freeSpace == poolSize)
	{
		std::unique_lock<std::shared_mutex> lock(mutex);
		if (pool->isOnQueue && pool->it != pools.begin())
		{
			if (isSmallPool)
			{
				freeSmallPage(pool->baseAddress);
				// freeSmallPage((void*)pool);
				freeFromInternalPool(pool);
			}
			else
			{
				freePage(pool->baseAddress);
				// freePage((void*)pool);
			}
			pools.erase(pool->it);
		}
	}
}

void CustomMemoryManager::freeFromListPool(void* ptr, MemoryListPool* pool)
{
	std::unique_lock<std::shared_mutex> lock(hugePoolsMutex);
	pool->free(ptr);
}

void CustomMemoryManager::freeFromInternalPool(void* ptr)
{
	std::unique_lock<std::shared_mutex> lock(internalPoolMutex);
	internalPool.free(ptr);
}

void CustomMemoryManager::freePage(void* ptr)
{
	assert(getSmallPageNum(ptr) == 0);

	std::unique_lock<std::shared_mutex> lock(hugePoolsMutex);
	int pageHash = getPageHash(ptr);
	size_t pageNum = getPageNum(ptr);
	for (auto& page : pages[pageHash])
	{
		if (page->pageNum == pageNum)
		{
			auto hugePool = page->hugePool;
			hugePool->free(ptr);
			// delete page;
			freeFromInternalPool(page);
			void* ptr = allocateFromInternalPool(sizeof(Page));
			page = new (ptr) Page(Page::PageType::HUGE, hugePool, pageNum);
			return;
		}
	}
	// not reachable
	assert(false);
}

void CustomMemoryManager::freeSmallPage(void* ptr)
{
	int pageHash = getPageHash(ptr);
	size_t pageNum = getPageNum(ptr);
	int smallPageNum = getSmallPageNum(ptr);
	for (auto page : pages[pageHash])
	{
		if (page->pageNum == pageNum)
		{
			SmallBlockPoolPage* sPage = (SmallBlockPoolPage*)page;
			assert(sPage->smallPools[smallPageNum] != nullptr);
			//sPage->smallPools[smallPageNum]->~MemoryBlockPool();
			sPage->smallPools[smallPageNum] = nullptr;
			freeFromBlockPool(ptr, &(sPage->dataPool), smallPagePoolMutex, freeSmallBlockPoolPages, false);
			return;
		}
	}
	// not reachable
	assert(false);
}

size_t CustomMemoryManager::reportFreeSpace()
{
	size_t ret = 0;

	//{
	//	std::unique_lock<std::shared_mutex> lock(internalPoolMutex);
	//	ret += internalPool.freeSpace;
	//}

	std::unique_lock<std::shared_mutex> lock(hugePoolsMutex);
	for (auto pool : hugePools)
		ret += pool->freeSpace;
	return ret;
}

size_t CustomMemoryManager::reportTotalSpace()
{
	size_t ret = 0;
	std::unique_lock<std::shared_mutex> lock(hugePoolsMutex);
	for (auto pool : hugePools)
		ret += pool->poolSize;
	return ret;
}