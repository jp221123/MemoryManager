#pragma once

#include <atomic>
#include <shared_mutex>
#include <list>
#include <map>

#include <Windows.h>

class CustomMemoryManager;
class MemoryPool
{
public:
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
	//struct Entry
	//{
	//	SLIST_ENTRY listEntry;
	//	void* address;
	//	Entry(void* address) :
	//		address(address) {}
	//};
public:
	void* const baseAddress;
	std::atomic<int> freeSpace;
	std::list<MemoryBlockPool*>::iterator it;
	bool isOnQueue;
	const int poolSize;
	const int blockSize;
private:
	//const int entrySize;
	const int numBlock;
	PSLIST_HEADER freeHead;
	const size_t slistAddress;
	const size_t dataAddress;
public:
	MemoryBlockPool(CustomMemoryManager* manager, void* baseAddress, int poolSize, int blockSize, std::list<MemoryBlockPool*>& freePools);
	void* allocate(size_t size) override final;
	size_t free(void* ptr) override final;
};

class MemoryListPool : public MemoryPool
{
	struct Entry
	{
		void* address;
		size_t size;
		bool isOnFreeList;
		std::list<std::list<Entry>::iterator>::iterator freeListIt;
		Entry(void* address, size_t size, bool isOnFreeList) :
			address(address), size(size), isOnFreeList(isOnFreeList) {}
	};
public:
	void* const baseAddress;
	const size_t poolSize;
	size_t freeSpace;
private:
	std::list<Entry> entryList;
	std::list<std::list<Entry>::iterator> freeList;
	std::map<void*, std::list<Entry>::iterator> usedMap;
	//Entry* entryListHead;
	//Entry* freeListHead;
public:
	MemoryListPool(CustomMemoryManager* manager, size_t poolSize);
	~MemoryListPool();
	void* allocate(size_t size) override final;
	size_t free(void* ptr) override final;
	void* allocateAligned(size_t size);
};