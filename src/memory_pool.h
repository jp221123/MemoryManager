#pragma once

#include <atomic>
#include <shared_mutex>
#include <list>
#include <map>

#include <Windows.h>

class CustomMemoryManager;
class MemoryPool
{
protected:
	CustomMemoryManager* const manager;
	const bool isBlockPool;
public:
	virtual void* allocate(size_t size) = 0;
	virtual size_t free(void* ptr) = 0;
protected:
	MemoryPool(CustomMemoryManager* manager, bool isBlockPool) :
		manager(manager),
		isBlockPool(isBlockPool)
	{}

	friend CustomMemoryManager;
};

class MemoryBlockPool : public MemoryPool
{
	struct Entry
	{
		SLIST_ENTRY listEntry;
		void* address;
	};
public:
	void* const baseAddress;
	std::atomic<int> freeSpace;
	std::list<MemoryBlockPool*>::iterator it;
	bool isOnQueue;
	const int poolSize;
	const int blockSize;
private:
	PSLIST_HEADER freeHead;

public:
	MemoryBlockPool(CustomMemoryManager* manager, void* baseAddress, int poolSize, int blockSize, std::list<MemoryBlockPool*>& freePools);
	void* allocate(size_t size) override final;
	size_t free(void* ptr) override final;
};

class MemoryListPool : public MemoryPool
{
	struct Entry
	{
		Entry *prev, *next;
		void* address;
		size_t size;
		bool isOnList;
		std::list<Entry*>::iterator it;
		Entry(Entry* prev, Entry* next, void* address, size_t size) :
			prev(prev), next(next), address(address), size(size), isOnList(false) {}
		Entry(Entry* prev, Entry* next, void* address, size_t size, std::list<Entry*>& freeList):
			prev(prev), next(next), address(address), size(size), isOnList(true)
		{
			freeList.push_front(this);
			it = freeList.begin();
		}
	};
public:
	void* const baseAddress;
	const size_t poolSize;
	size_t freeSpace;
private:
	std::list<Entry*> freeList;
	std::map<void*, Entry*> usedList;
public:
	MemoryListPool(CustomMemoryManager* manager, void* baseAddress, size_t poolSize);
	void* allocate(size_t size) override final;
	size_t free(void* ptr) override final;
	void* allocateAligned(size_t size);
};