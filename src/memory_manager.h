#pragma once

// constexpr int MAX_THREAD = 8;
// at least MAX_THREAD free blocks per pool
// up to 512 bytes --- allocate 4KiB memory pool, block-size
// up to 256 KiB -- allocate 2MiB memory pool, block-size
// over 256 KiB -- allocate >= 128MiB dynamic-sized memory pool, free-list

#include "memory_pool.h"

#include <map>
#include <list>
#include <array>
#include <vector>
#include <atomic>
#include <shared_mutex>
#include <mutex>
#include <cassert>

#include <Windows.h>

class MemoryManager
{
public:
	virtual void* allocate(size_t size) = 0;
	virtual void free(void* ptr) = 0;
};

namespace CustomMemoryManagerConstants
{
	constexpr size_t MAX_MEMORY = 64LL * (1 << 30);
	constexpr size_t PAGE_SIZE = 2 * (1 << 20); // = 1 << PAGE_BIT
	constexpr int TOTAL_PAGE_NUM = MAX_MEMORY >> 21;
	constexpr size_t SMALL_THRESHOLD = 512;
	constexpr size_t LARGE_THRESHOLD = 256 * (1 << 10);
	constexpr size_t SMALL_POOL_SIZE = 4 * (1 << 10);
	constexpr size_t LARGE_POOL_SIZE = 2 * (1 << 20);
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
	size_t getPageAddress(void* ptr);
	int getPageNum(size_t ptr);
	int getPageNum(void* ptr);
	int getSmallPageNum(size_t ptr);
	int getSmallPageNum(void* ptr);
}

class Page
{
public:
	enum class PageType {
		HUGE, LARGE, SMALL,
	};
	const PageType t;
	const size_t baseAddress;
	MemoryListPool* const hugePool;
	Page(MemoryListPool* hugePool, size_t baseAddress) :
		t(PageType::HUGE), hugePool(hugePool), baseAddress(baseAddress) {}
protected:
	Page(PageType t, MemoryListPool* hugePool, size_t baseAddress) :
		t(t), hugePool(hugePool), baseAddress(baseAddress) {}
};

class LargePoolPage : public Page
{
public:
	MemoryBlockPool* const pool;
	LargePoolPage(MemoryListPool* hugePool, size_t baseAddress, MemoryBlockPool* pool) :
		Page(PageType::LARGE, hugePool, baseAddress), pool(pool) {}
};

class SmallPoolPage : public Page
{
	static constexpr size_t LARGE_PAGE_SIZE = 2 * (1 << 20);
	static constexpr size_t SMALL_PAGE_SIZE = 4 * (1 << 10);
public:
	MemoryBlockPool* const pool;
	std::array<MemoryBlockPool*, LARGE_PAGE_SIZE / SMALL_PAGE_SIZE> smallPools{};
	SmallPoolPage(MemoryListPool* hugePool, size_t baseAddress, MemoryBlockPool* pool) :
		Page(PageType::SMALL, hugePool, baseAddress), pool(pool) {}

	void* allocate(CustomMemoryManager* manager, int blockSize, std::list<MemoryBlockPool*>& freeSmallPools);
	size_t free(void* ptr);
};

class CustomMemoryManager : public MemoryManager
{
public:
	void* allocate(size_t size) override final;
	void free(void* ptr) override final;
	CustomMemoryManager();

private:
	std::array<std::list<MemoryBlockPool*>, 23> freeSmallPools{};
	std::array<std::list<MemoryBlockPool*>, 53> freeLargePools{};
	std::array<std::shared_mutex, 23> smallMutexes{};
	std::array<std::shared_mutex, 53> largeMutexes{};
	
	std::shared_mutex smallPagePoolMutex;
	std::list<MemoryBlockPool*> freeLargePoolsForSmallPages;

	std::shared_mutex hugePoolMutex;
	std::vector<MemoryListPool> hugePools;
	size_t nextHugePoolSize = 128 * (1 << 20);
	std::array<std::vector<Page*>, CustomMemoryManagerConstants::TOTAL_PAGE_NUM> pages{};

private:
	void* allocateFromBlockPool(std::shared_mutex& mutex, std::list<MemoryBlockPool*>& pools, bool isSmallPool, int blockSize);
	void freeFromBlockPool(void* ptr, MemoryBlockPool* pool, std::shared_mutex& mutex, std::list<MemoryBlockPool*>& pools, bool isSmallPool);
	
	void* allocateFromListPool(size_t size);
	void freeFromListPool(void* ptr, MemoryListPool* pool);

	MemoryBlockPool* allocateLargePage(int blockSize, std::list<MemoryBlockPool*>& pools);
	MemoryBlockPool* allocatePageForSmallPools();
	MemoryBlockPool* allocatePage(bool forSmallPages, int blockSize, std::list<MemoryBlockPool*>& pools);
	void freePage(void* ptr);
	
	MemoryBlockPool* allocateSmallPage(int blockSize, std::list<MemoryBlockPool*>& pools);
	void freeSmallPage(void* ptr);
	
	void grow();
};