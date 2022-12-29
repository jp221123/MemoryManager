#pragma once

// constexpr int MAX_THREAD = 8;
// at least MAX_THREAD free blocks per pool
// up to 512 bytes --- allocate 4KiB memory pool, block-size
// up to 256 KiB -- allocate 2MiB memory pool, block-size
// over 256 KiB -- allocate >= 128MiB dynamic-sized memory pool, free-list
// 32 MiB for internal data structures

// lock order is always
// block pool -> (page pool ->) list pool -> internal pool

#include "memory_pool.h"

#include <map>
#include <list>
#include <array>
#include <vector>
#include <atomic>
#include <shared_mutex>
#include <mutex>

#include <Windows.h>

class MemoryManager
{
public:
	virtual void* allocate(size_t size) = 0;
	virtual void free(void* ptr) = 0;
	virtual size_t reportFreeSpace() = 0;
	virtual size_t reportTotalSpace() = 0;
};

namespace CustomMemoryManagerConstants
{
	constexpr size_t MAX_MEMORY = 64LL * (1 << 30);
	constexpr size_t INTERNAL_POOL_SIZE = 32 * (1 << 20);
	constexpr size_t INITIAL_HUGE_POOL_SIZE = 128 * (1 << 20);
	constexpr int TOTAL_PAGE_NUM = MAX_MEMORY >> 21;
	constexpr size_t SMALL_THRESHOLD = 512;
	constexpr size_t LARGE_THRESHOLD = 256 * (1 << 10);
	constexpr size_t SMALL_POOL_SIZE = 4 * (1 << 10);
	constexpr size_t LARGE_POOL_SIZE = 2 * (1 << 20);
	constexpr size_t SMALL_PAGE_NUM_PER_LARGE_PAGE = LARGE_POOL_SIZE / SMALL_POOL_SIZE;
	const std::array<size_t, 23> SMALL_BLOCK_SIZES
		= {
			8, 16, 24, 32, 40, 48, 56, 64, 72, 88, 104, 120, 136,
			160, 184, 208, 240, 272, 312, 352, 400, 456, 512
	};
	const std::array<size_t, 53> LARGE_BLOCK_SIZES
		= {
			576, 648, 736, 832, 936, 1056, 1192, 1344, 1512, 1704, 1920,
			2160, 2432, 2736, 3080, 3472, 3912, 4408, 4960, 5584, 6288,
			7080, 7968, 8968, 10096, 11360, 12784, 14384, 16184, 18208,
			20488, 23056, 25944, 29192, 32848, 36960, 41584, 46784, 52632,
			59216, 66624, 74952, 84328, 94872, 106736, 120080, 135096,
			151984, 170984, 192360, 216408, 243464, 262144
	};
	constexpr size_t PAGE_SIZE = LARGE_POOL_SIZE;
	//std::vector<size_t> CustomMemoryManager::makeBlockSizes(int min, int max)
	//{
	//	std::vector<size_t> ret;
	//	int curr = min;
	//	while (true) {
	//		curr = curr * 9 / 8;
	//		curr = (curr + 7) / 8 * 8;
	//		if (curr >= max) {
	//			ret.push_back(max);
	//			break;
	//		}
	//		ret.push_back(curr);
	//	}
	//	return ret;
	//}
	size_t getPageNum(size_t ptr);
	size_t getPageNum(void* ptr);
	int getPageHash(size_t ptr);
	int getPageHash(void* ptr);
	int getSmallPageNum(size_t ptr);
	int getSmallPageNum(void* ptr);
}

class Page
{
public:
	enum class PageType {
		INTERNAL, HUGE, LARGE, SMALL,
	};
	const PageType t;
	const size_t pageNum;
	MemoryListPool* const hugePool;
	Page(PageType t, MemoryListPool* hugePool, size_t pageNum) :
		t(t), hugePool(hugePool), pageNum(pageNum) {}
};

class LargeBlockPoolPage : public Page
{
public:
	MemoryBlockPool dataPool;
	LargeBlockPoolPage(MemoryListPool* hugePool, size_t pageNum, CustomMemoryManager* manager, void* baseAddress, int poolSize, int blockSize, std::list<MemoryBlockPool*>& freePools) :
		Page(PageType::LARGE, hugePool, pageNum), dataPool{ manager, baseAddress, poolSize, blockSize, freePools } {}
};

class SmallBlockPoolPage : public Page
{
public:
	MemoryBlockPool dataPool;
	std::array<MemoryBlockPool*, CustomMemoryManagerConstants::SMALL_PAGE_NUM_PER_LARGE_PAGE> smallPools{};
	SmallBlockPoolPage(MemoryListPool* hugePool, size_t pageNum, CustomMemoryManager* manager, void* baseAddress, int poolSize, int blockSize, std::list<MemoryBlockPool*>& freePools) :
		Page(PageType::SMALL, hugePool, pageNum), dataPool{ manager, baseAddress, poolSize, blockSize, freePools } {}
};

class CustomMemoryManager : public MemoryManager
{
public:
	void* allocate(size_t size) override final;
	void free(void* ptr) override final;
	size_t reportFreeSpace() override final;
	size_t reportTotalSpace() override final;
	CustomMemoryManager();
	~CustomMemoryManager();

private:
	std::array<std::list<MemoryBlockPool*>, 23> freeSmallPools{};
	std::array<std::list<MemoryBlockPool*>, 53> freeLargePools{};
	std::array<std::shared_mutex, 23> smallMutexes{};
	std::array<std::shared_mutex, 53> largeMutexes{};
	
	std::shared_mutex smallPagePoolMutex;
	std::list<MemoryBlockPool*> freeSmallBlockPoolPages;

	std::shared_mutex hugePoolsMutex;
	std::vector<MemoryListPool*> hugePools;
	std::array<std::vector<Page*>, CustomMemoryManagerConstants::TOTAL_PAGE_NUM> pages{};
	size_t nextHugePoolSize = CustomMemoryManagerConstants::INITIAL_HUGE_POOL_SIZE;

	std::shared_mutex internalPoolMutex;
	MemoryListPool internalPool;

private:
	void grow();

	void* allocateFromBlockPool(std::shared_mutex& mutex, std::list<MemoryBlockPool*>& pools, bool isSmallPool, int blockSize);
	void* allocateFromListPool(size_t size);
	void* allocateFromInternalPool(size_t size);
	MemoryBlockPool* allocateSmallPage(int blockSize, std::list<MemoryBlockPool*>& pools);
	MemoryBlockPool* allocateLargeBlockPoolPage(int blockSize, std::list<MemoryBlockPool*>& pools);
	MemoryBlockPool* allocateSmallBlockPoolPage();
	MemoryBlockPool* allocatePage(bool forSmallPages, int blockSize, std::list<MemoryBlockPool*>& pools);

	void freeFromBlockPool(void* ptr, MemoryBlockPool* pool, std::shared_mutex& mutex, std::list<MemoryBlockPool*>& pools, bool isSmallPool);
	void freeFromListPool(void* ptr, MemoryListPool* pool);
	void freeFromInternalPool(void* ptr);
	void freeSmallPage(void* ptr);
	void freePage(void* ptr);
};