#include "memory_pool.h"

#include <cassert>

MemoryBlockPool::MemoryBlockPool(CustomMemoryManager* manager, void* baseAddress, int poolSize, int blockSize, std::list<MemoryBlockPool*>& freePools) :
	MemoryPool(manager, true), baseAddress(baseAddress), poolSize(poolSize), blockSize(blockSize),
	freeHead((PSLIST_HEADER)_aligned_malloc(sizeof(SLIST_HEADER), MEMORY_ALLOCATION_ALIGNMENT)),
	freeSpace(poolSize)
{
	InitializeSListHead(freeHead);
	for (size_t address = (size_t)baseAddress; address < (size_t)baseAddress + poolSize; address += blockSize)
	{
		Entry* item = (Entry*)_aligned_malloc(sizeof(Entry), MEMORY_ALLOCATION_ALIGNMENT);
		item->address = (void*)address;
		InterlockedPushEntrySList(freeHead, &(item->listEntry));
	}

	isOnQueue = true;
	freePools.push_front(this);
	it = freePools.begin();
}

void* MemoryBlockPool::allocate(size_t size)
{
	PSLIST_ENTRY listEntry = InterlockedPopEntrySList(freeHead);
	if (listEntry == nullptr)
		return nullptr;
	Entry* entry = (Entry*)listEntry;
	freeSpace.fetch_sub(blockSize);
	return entry->address;
}

size_t MemoryBlockPool::free(void* ptr)
{
	Entry* item = (Entry*)_aligned_malloc(sizeof(Entry), MEMORY_ALLOCATION_ALIGNMENT);
	item->address = ptr;
	InterlockedPushEntrySList(freeHead, &(item->listEntry));
	return freeSpace.fetch_add(blockSize);
}

MemoryListPool::MemoryListPool(CustomMemoryManager* manager, void* baseAddress, size_t poolSize):
	MemoryPool(manager, false), baseAddress(baseAddress), poolSize(poolSize), freeSpace(0)
{
	new Entry(nullptr, nullptr, baseAddress, poolSize, freeList);
}

void* MemoryListPool::allocate(size_t size)
{
	for(auto pEntry: freeList)
	{
		if (pEntry->size >= size)
		{
			freeSpace -= size;
			size_t leftover = pEntry->size - size;
			if (leftover > 0)
			{
				Entry* pEntry2 = new Entry(pEntry->prev, pEntry, pEntry->address, size);
				pEntry->address = (void*)((size_t)pEntry->address + leftover);
				pEntry->size -= leftover;
				pEntry->prev = pEntry2;
				usedList[pEntry2->address] = pEntry2;
				return pEntry2->address;
			}
			else
			{
				freeList.erase(pEntry->it);
				pEntry->isOnList = false;
				usedList[pEntry->address] = pEntry;
				return pEntry->address;
			}
		}
	}
	return nullptr;
}

size_t MemoryListPool::free(void* ptr)
{
	assert(usedList.count(ptr));
	auto pEntry = usedList[ptr];
	usedList.erase(ptr);
	freeSpace += pEntry->size;

	if (pEntry->prev != nullptr && pEntry->prev->isOnList)
	{
		pEntry->prev->next = pEntry->next;
		pEntry->prev->size += pEntry->size;
		pEntry = pEntry->prev;
	}
	if (pEntry->next != nullptr && pEntry->next->isOnList)
	{
		pEntry->next->prev = pEntry;
		pEntry->next->address = pEntry->address;
		pEntry->next->size += pEntry->size;
		pEntry = pEntry->next;
	}
	if (!pEntry->isOnList)
	{
		freeList.push_front(pEntry);
		pEntry->isOnList = true;
		pEntry->it = freeList.begin();
	}

	return 0;
}

void* MemoryListPool::allocateAligned(size_t size)
{
	// aligned by size
	for (auto pEntry : freeList)
	{
		size_t startAddress = ((size_t)pEntry->address + size - 1) / size * size;
		size_t endAddress = (size_t)pEntry->address + pEntry->size;
		if(startAddress + size <= endAddress)
		{
			freeSpace -= size;
			size_t left = startAddress - (size_t)pEntry->address;
			size_t right = endAddress - startAddress - size;
			if (left > 0)
			{
				pEntry->prev = new Entry(pEntry->prev, pEntry, pEntry->address, left, freeList);
			}
			if (right > 0)
			{
				pEntry->next = new Entry(pEntry, pEntry->next, (void*)(startAddress + size), right, freeList);
			}
			pEntry->address = (void*)startAddress;
			freeList.erase(pEntry->it);
			pEntry->isOnList = false;
			usedList[pEntry->address] = pEntry;
			return pEntry->address;
		}
	}
	return nullptr;
}