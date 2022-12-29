#include "memory_pool.h"

#include "memory_manager.h"

#include <cassert>
#include <iostream>

constexpr size_t multipleGeq(size_t size, size_t multiple) {
	return (size + multiple - 1) / multiple * multiple;
}

MemoryBlockPool::MemoryBlockPool(CustomMemoryManager* manager, void* baseAddress, int poolSize, int blockSize, std::list<MemoryBlockPool*>& freePools) :
	MemoryPool(manager, true), baseAddress(baseAddress), poolSize(poolSize), blockSize(blockSize),
	numBlock((poolSize - multipleGeq(sizeof(SLIST_HEADER), MEMORY_ALLOCATION_ALIGNMENT))
		/ (blockSize + multipleGeq(sizeof(SLIST_ENTRY), MEMORY_ALLOCATION_ALIGNMENT))),
	freeHead((PSLIST_HEADER)baseAddress),
	slistAddress((size_t)baseAddress + multipleGeq(sizeof(SLIST_HEADER), MEMORY_ALLOCATION_ALIGNMENT)),
	dataAddress((size_t)baseAddress + poolSize - blockSize * numBlock),
	freeSpace(poolSize)
{
	InitializeSListHead(freeHead);
	size_t metaAddress = slistAddress;
	for (int i = 0; i < numBlock; i++) {
		PSLIST_ENTRY listEntry = (PSLIST_ENTRY)metaAddress;
		InterlockedPushEntrySList(freeHead, listEntry);
		metaAddress += multipleGeq(sizeof(SLIST_ENTRY), MEMORY_ALLOCATION_ALIGNMENT);
	}
	isOnQueue = true;
	freePools.push_back(this);
	it = --freePools.end();
}

void* MemoryBlockPool::allocate(size_t size)
{
	PSLIST_ENTRY listEntry = InterlockedPopEntrySList(freeHead);
	if (listEntry == nullptr)
		return nullptr;
	int index = ((size_t)listEntry - slistAddress) / multipleGeq(sizeof(SLIST_ENTRY), MEMORY_ALLOCATION_ALIGNMENT);
	freeSpace.fetch_sub(blockSize);
	return (void*)(dataAddress + blockSize * index);
}

size_t MemoryBlockPool::free(void* ptr)
{
	int index = ((size_t)ptr - dataAddress) / blockSize;
	InterlockedPushEntrySList(freeHead, (PSLIST_ENTRY)(slistAddress + multipleGeq(sizeof(SLIST_ENTRY), MEMORY_ALLOCATION_ALIGNMENT) * index));
	size_t prevFreeSpace = freeSpace.fetch_add(blockSize);
	return prevFreeSpace + blockSize;
}

MemoryListPool::MemoryListPool(CustomMemoryManager* manager, size_t poolSize):
	MemoryPool(manager, false), poolSize(poolSize), freeSpace(poolSize),
	baseAddress(_aligned_malloc(poolSize, CustomMemoryManagerConstants::PAGE_SIZE))
{
	entryList.emplace_front(baseAddress, poolSize, true);
	freeList.push_front(entryList.begin());
	entryList.front().freeListIt = freeList.begin();
}

MemoryListPool::~MemoryListPool() {
	_aligned_free(baseAddress);
}

void* MemoryListPool::allocate(size_t size)
{
	// std::cout << "enter allocate: " << entryList.size() << "=" << freeList.size() << "+" << usedMap.size() << std::endl;
	assert(entryList.size() == freeList.size() + usedMap.size());

	for(auto freeListIt = freeList.begin(); freeListIt != freeList.end(); freeListIt++)
	{
		if ((*freeListIt)->size >= size)
		{
			auto it = *freeListIt;
			freeSpace -= size;
			size_t leftover = it->size - size;
			if (leftover > 0)
			{
				auto it2 = entryList.insert(it, Entry(it->address, size, false));
				usedMap[it2->address] = it2;
				it->address = (void*)((size_t)it->address + size);
				it->size = leftover;
				return it2->address;
			}
			else
			{
				freeList.erase(freeListIt);
				it->isOnFreeList = false;
				usedMap[it->address] = it;
				return it->address;
			}
		}
	}
	return nullptr;
}

size_t MemoryListPool::free(void* ptr)
{
	// std::cout << "enter free: " << entryList.size() << "=" << freeList.size() << "+" << usedMap.size() << std::endl;
	assert(entryList.size() == freeList.size() + usedMap.size());

	assert(usedMap.count(ptr));
	auto mapIt = usedMap.find(ptr);
	auto it = mapIt->second;
	usedMap.erase(mapIt);
	freeSpace += it->size;

	if (it != entryList.begin()) {
		auto prev = it;
		prev--;
		if (prev->isOnFreeList) {
			prev->size += it->size;
			entryList.erase(it);
			it = prev;
		}
	}
	{
		auto next = it;
		next++;
		if (next != entryList.end() && next->isOnFreeList) {
			next->size += it->size;
			next->address = it->address;
			if (it->isOnFreeList)
				freeList.erase(it->freeListIt);
			entryList.erase(it);
			it = next;
		}
	}
	if (!it->isOnFreeList)
	{
		freeList.push_front(it);
		it->isOnFreeList = true;
		it->freeListIt = freeList.begin();
	}

	assert(entryList.size() == freeList.size() + usedMap.size());
	return freeSpace;
}

void* MemoryListPool::allocateAligned(size_t size)
{
	// aligned by size
	for (auto freeListIt = freeList.begin(); freeListIt != freeList.end(); freeListIt++) {
		auto it = *freeListIt;
		size_t startAddress = ((size_t)it->address + size - 1) / size * size;
		size_t endAddress = (size_t)it->address + it->size;
		if(startAddress + size <= endAddress)
		{
			freeSpace -= size;
			size_t left = startAddress - (size_t)it->address;
			size_t right = endAddress - startAddress - size;
			if (left > 0)
			{
				auto it2 = entryList.insert(it, Entry(it->address, left, true));
				freeList.push_front(it2);
				it2->freeListIt = freeList.begin();
			}
			if (right > 0)
			{
				it->address = (void*)(startAddress + size);
				it->size = right;
				auto it2 = entryList.insert(it, Entry((void*)startAddress, size, false));
				usedMap[it2->address] = it2;
				return it2->address;
			}
			else
			{
				it->address = (void*)startAddress;
				freeList.erase(freeListIt);
				it->isOnFreeList = false;
				usedMap[it->address] = it;
				return it->address;
			}
		}
	}
	return nullptr;
}