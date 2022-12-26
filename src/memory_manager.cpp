#include "memory_manager.h"

size_t CustomMemoryManagerConstants::getPageAddress(void* ptr) { return (size_t)ptr >> 21; }
int CustomMemoryManagerConstants::getPageNum(size_t ptr) { return (ptr >> 21) & (TOTAL_PAGE_NUM-1); }
int CustomMemoryManagerConstants::getPageNum(void* ptr) { return ((size_t)ptr >> 21) & (TOTAL_PAGE_NUM-1); }
int CustomMemoryManagerConstants::getSmallPageNum(size_t ptr) { return (ptr & ((1 << 21) - 1)) >> 12; }
int CustomMemoryManagerConstants::getSmallPageNum(void* ptr) { return ((size_t)ptr & ((1 << 21) - 1)) >> 12; }

using namespace CustomMemoryManagerConstants;

void* SmallPoolPage::allocate(CustomMemoryManager* manager, int blockSize, std::list<MemoryBlockPool*>& freeSmallPools)
{
	void* ptr = pool->allocate(0);
	if (ptr != nullptr)
	{
		int index = getSmallPageNum(ptr);
		smallPools[index] = new MemoryBlockPool(manager, ptr, SMALL_POOL_SIZE, blockSize, freeSmallPools);
	}
	return ptr;
}

size_t SmallPoolPage::free(void* ptr)
{
	int index = getSmallPageNum(ptr);
	delete smallPools[index];
	return pool->free(ptr);
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
	// not reachable
	assert(false);
	return nullptr;
}

void* CustomMemoryManager::allocateFromBlockPool(std::shared_mutex& mutex, std::list<MemoryBlockPool*>& pools, bool isSmallPool, int blockSize)
{
	std::shared_lock<std::shared_mutex> lock(mutex);
	if (!pools.empty())
	{
		auto pool = pools.front();
		void* ptr = pool->allocate(0);
		if (ptr != nullptr)
			return ptr;
	}
	lock.unlock();

	std::unique_lock<std::shared_mutex> lock2(mutex);
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
		return pool->allocate(0);
	}
	else
	{
		auto pool = allocateLargePage(blockSize, pools);
		return pool->allocate(0);
	}
}

void* CustomMemoryManager::allocateFromListPool(size_t size)
{
	std::unique_lock<std::shared_mutex> lock(hugePoolMutex);
	for (auto& pool : hugePools)
	{
		void* ptr = pool.allocate(size);
		if (ptr != nullptr)
			return ptr;
	}
	while (true)
	{
		grow();
		void* ptr = hugePools.back().allocate(size);
		if (ptr != nullptr)
			return ptr;
	}
}

void CustomMemoryManager::free(void* ptr)
{
	int pageNum = getPageNum(ptr);
	size_t pageAddress = getPageAddress(ptr);
	for (auto page : pages[pageNum])
	{
		if (page->baseAddress == pageAddress)
		{
			switch (page->t)
			{
				case Page::PageType::HUGE:
				{
					auto pool = page->hugePool;
					freeFromListPool(ptr, (MemoryListPool*)pool);
					return;
				}
				case Page::PageType::LARGE:
				{
					LargePoolPage* lPage = (LargePoolPage*)page;
					auto pool = lPage->pool;
					freeFromBlockPool(ptr, (MemoryBlockPool*)pool, largeMutexes[pageNum], freeLargePools[pageNum], false);
					return;
				}
				case Page::PageType::SMALL:
				{
					SmallPoolPage* sPage = (SmallPoolPage*)page;
					int smallPageNum = getSmallPageNum(ptr);
					auto pool = sPage->smallPools[smallPageNum];
					freeFromBlockPool(ptr, pool, smallMutexes[pageNum], freeSmallPools[pageNum], true);
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

	if (!pool->isOnQueue)
	{
		std::unique_lock<std::shared_mutex> lock(mutex);
		if (!pool->isOnQueue)
		{
			pools.push_front(pool);
			pool->it = pools.begin();
			pool->isOnQueue = true;
		}
	}
	else if (freeSpace == poolSize)
	{
		std::unique_lock<std::shared_mutex> lock(mutex);
		if (pool->it != pools.begin())
		{
			pools.erase(pool->it);
			if (isSmallPool)
			{
				freeSmallPage(pool->baseAddress);
			}
			else
			{
				freePage(pool->baseAddress);
			}
			delete pool;
		}
	}
}

void CustomMemoryManager::freeFromListPool(void* ptr, MemoryListPool* pool)
{
	pool->free(ptr);
}

CustomMemoryManager::CustomMemoryManager()
{
}

MemoryBlockPool* CustomMemoryManager::allocateLargePage(int blockSize, std::list<MemoryBlockPool*>& pools)
{
	return allocatePage(false, blockSize, pools);
}

MemoryBlockPool* CustomMemoryManager::allocatePageForSmallPools()
{
	return allocatePage(true, SMALL_POOL_SIZE, freeLargePoolsForSmallPages);
}

MemoryBlockPool* CustomMemoryManager::allocatePage(bool forSmallPages, int blockSize, std::list<MemoryBlockPool*>& pools)
{
	std::unique_lock<std::shared_mutex> lock(hugePoolMutex);
	MemoryListPool* hugePool = nullptr;
	void* address = nullptr;
	for (auto& pool : hugePools)
	{
		address = pool.allocateAligned(LARGE_POOL_SIZE);
		if (address != nullptr) {
			hugePool = &pool;
			break;
		}
	}
	if (address == nullptr)
	{
		grow();
		hugePool = &hugePools.back();
		address = hugePool->allocateAligned(LARGE_POOL_SIZE);
	}
	assert(address != nullptr);
	int pageNum = getPageNum(address);
	size_t pageAddress = getPageAddress(address);
	if (forSmallPages)
	{
		auto pool = new MemoryBlockPool(this, address, LARGE_POOL_SIZE, blockSize, pools);
		for (auto& page : pages[pageNum])
		{
			if (page->baseAddress == pageAddress)
			{
				delete page;
				page = new SmallPoolPage(hugePool, pageAddress, pool);
				return pool;
			}
		}
	}
	else
	{
		auto pool = new MemoryBlockPool(this, address, LARGE_POOL_SIZE, blockSize, pools);
		for (auto& page : pages[pageNum])
		{
			if (page->baseAddress == pageAddress)
			{
				delete page;
				page = new LargePoolPage(hugePool, pageAddress, pool);
				return pool;
			}
		}
	}
}

MemoryBlockPool* CustomMemoryManager::allocateSmallPage(int blockSize, std::list<MemoryBlockPool*>& pools)
{
	void* ptr = allocateFromBlockPool(smallPagePoolMutex, freeLargePoolsForSmallPages, false, SMALL_POOL_SIZE);
	int pageNum = getPageNum(ptr);
	size_t pageAddress = getPageAddress(ptr);
	int smallPageNum = getSmallPageNum(ptr);
	for (auto page : pages[pageNum])
	{
		if (page->baseAddress == pageAddress)
		{
			SmallPoolPage* sPage = (SmallPoolPage*)page;
			auto pool = new MemoryBlockPool(this, ptr, SMALL_POOL_SIZE, blockSize, pools);
			sPage->smallPools[smallPageNum] = pool;
			return pool;
		}
	}
}

void CustomMemoryManager::freePage(void* ptr)
{
	std::unique_lock<std::shared_mutex> lock(hugePoolMutex);
	int pageNum = getPageNum(ptr);
	size_t pageAddress = getPageAddress(ptr);
	for (auto& page : pages[pageNum])
	{
		if (page->baseAddress == pageAddress)
		{
			auto hugePool = page->hugePool;
			hugePool->free(ptr);
			delete page;
			page = new Page(hugePool, pageAddress);
		}
	}
}

void CustomMemoryManager::freeSmallPage(void* ptr)
{
	int pageNum = getPageNum(ptr);
	size_t pageAddress = getPageAddress(ptr);
	for (auto page : pages[pageNum])
	{
		if (page->baseAddress == pageAddress)
		{
			auto lPage = (LargePoolPage*)page;
			auto pool = lPage->pool;
			freeFromBlockPool(ptr, pool, smallPagePoolMutex, freeLargePoolsForSmallPages, false);
		}
	}
}

void CustomMemoryManager::grow()
{
	void* address = _aligned_malloc(nextHugePoolSize, PAGE_SIZE);
	hugePools.emplace_back(this, address, nextHugePoolSize);
	size_t pageAddress = (size_t)address;
	for (int i = 0; i < getPageNum(nextHugePoolSize); i++)
	{
		int pageNum = getPageNum(pageAddress);
		pages[pageNum].push_back(new Page(&hugePools.back(), pageAddress));
		pageAddress += PAGE_SIZE;
	}
	nextHugePoolSize <<= 1;
}