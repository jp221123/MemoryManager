#include "memory_manager.h"

#include <iostream>
#include <random>
#include <thread>
#include <vector>
#include <set>

// todo: debug the multi-threaded run
// todo: change memory list pool to remove std::list, use the allocated space instead (n.b. alignment issue)
// todo: memoryListPool -- make several freeList by size range

// not robust against bad free calls and bad memory manipulations

using ll = long long;

using namespace CustomMemoryManagerConstants;

class BasicMemoryManager : public MemoryManager
{
	void* allocate(size_t size) override final { return std::malloc(size); }
	void free(void* ptr) override final { std::free(ptr); }
	size_t reportFreeSpace() override final { return 0; }
	size_t reportTotalSpace() override final { return 0; }
};

void integrityTest(MemoryManager* manager, const size_t maxSize, int seed, const int maxElementSize)
{
	const int N = maxSize / maxElementSize;
	std::mt19937 generator(seed);
	std::uniform_int_distribution<int> distribution(1, maxElementSize / sizeof(int));
	std::uniform_int_distribution<int> flag(0, 1);
	std::vector<std::vector<int>> elements(N);
	for (int i = 0; i < N; i++) {
		int size = distribution(generator);
		for (int j = 0; j < size; j++)
			elements[i].push_back(distribution(generator));
	}
	std::vector<int*> address(N);
	std::vector<int> isAllocated(N);

	// random alloc
	for (int i = 0; i < N; i++) {
		if (flag(generator)) {
			address[i] = (int*)manager->allocate(elements[i].size() * sizeof(int));
			//std::cout << address[i] << ' ' << address[i] + elements[i].size() - 1 << std::endl;
			isAllocated[i] = true;
			for (int j = 0; j < elements[i].size(); j++) {
				*(address[i] + j) = elements[i][j];
			}
		}
	}
	for (int i = 0; i < N; i++) {
		if (isAllocated[i]) {
			for (int j = 0; j < elements[i].size(); j++) {
				if (*(address[i] + j) != elements[i][j]) {
					std::cerr << "wrong" << std::endl;
				}
			}
		}
	}
	// random free
	for (int i = 0; i < N; i++) {
		if (isAllocated[i] && flag(generator)) {
			manager->free(address[i]);
			isAllocated[i] = false;
		}
	}
	for (int i = 0; i < N; i++) {
		if (isAllocated[i]) {
			for (int j = 0; j < elements[i].size(); j++) {
				if (*(address[i] + j) != elements[i][j]) {
					std::cerr << "wrong" << std::endl;
				}
			}
		}
	}
	// random alloc, free
	for (int i = 0; i < N; i++) {
		if (flag(generator)) {
			if (isAllocated[i]) {
				manager->free(address[i]);
				isAllocated[i] = false;
			}
			else {
				address[i] = (int*)manager->allocate(elements[i].size() * sizeof(int));
				isAllocated[i] = true;
				for (int j = 0; j < elements[i].size(); j++)
					*(address[i] + j) = elements[i][j];
			}
		}
	}
	for (int i = 0; i < N; i++) {
		if (isAllocated[i]) {
			for (int j = 0; j < elements[i].size(); j++) {
				if (*(address[i] + j) != elements[i][j]) {
					std::cerr << "wrong" << std::endl;
				}
			}
		}
	}

	// free
	for (int i = 0; i < N; i++) {
		if (isAllocated[i]) {
			manager->free(address[i]);
			isAllocated[i] = false;
		}
	}
}

void integrityTestSmall(MemoryManager* manager, const size_t maxSize, int seed)
{
	integrityTest(manager, maxSize, seed, SMALL_THRESHOLD);
}

void integrityTestLarge(MemoryManager* manager, const size_t maxSize, int seed)
{
	integrityTest(manager, maxSize, seed, LARGE_THRESHOLD);
}

void integrityTestHuge(MemoryManager* manager, const size_t maxSize, int seed)
{
	integrityTest(manager, maxSize, seed, 10 * (1 << 20));
}

void integrityTestMixed(MemoryManager* manager, const size_t maxSize, int seed)
{

}

void performanceTest(MemoryManager* manager, const size_t maxSize, int seed, const int maxElementSize) {
	const int N = maxSize / maxElementSize;
	std::mt19937 generator(seed);
	std::uniform_int_distribution<int> distribution(1, maxElementSize);
	std::uniform_int_distribution<int> flag(0, 1);
	std::vector<int> size(N);
	for (int i = 0; i < N; i++)
		size[i] = distribution(generator);
	std::vector<int*> address(N);
	std::vector<int> isAllocated(N);

	for (int iter = 0; iter < 10; iter++)
	{
		// random alloc
		for (int i = 0; i < N; i++) {
			if (flag(generator)) {
				address[i] = (int*)manager->allocate(size[i]);
				isAllocated[i] = true;
			}
		}
		// random free
		for (int i = 0; i < N; i++) {
			if (isAllocated[i] && flag(generator)) {
				manager->free(address[i]);
				isAllocated[i] = false;
			}
		}
		// random alloc, free
		for (int i = 0; i < N; i++) {
			if (flag(generator)) {
				if (isAllocated[i]) {
					manager->free(address[i]);
					isAllocated[i] = false;
				}
				else {
					address[i] = (int*)manager->allocate(size[i]);
					isAllocated[i] = true;
				}
			}
		}
		// free
		for (int i = 0; i < N; i++) {
			if (isAllocated[i]) {
				manager->free(address[i]);
				isAllocated[i] = false;
			}
		}
	}
}

void performanceTestSmall(MemoryManager* manager, const size_t maxSize, int seed)
{
	performanceTest(manager, maxSize, seed, SMALL_THRESHOLD);
}

void performanceTestLarge(MemoryManager* manager, const size_t maxSize, int seed)
{
	performanceTest(manager, maxSize, seed, LARGE_THRESHOLD);
}

void performanceTestHuge(MemoryManager* manager, const size_t maxSize, int seed)
{
	performanceTest(manager, maxSize, seed, 10 * (1 << 20));
}

void performanceTestMixed(MemoryManager* manager, const size_t maxSize, int seed)
{

}

void measure(CustomMemoryManager* customManager, BasicMemoryManager* basicManager, const int maxSize, void (*f)(MemoryManager*, size_t, int))
{
	for (int n = 1; n <= 32; n *= 2)
	{
		std::chrono::steady_clock::time_point start;
		ll elapsed;
		std::vector<std::thread> threads(n);

		std::cout << "CustomManager - " << n << " threads started" << std::endl;
		start = std::chrono::steady_clock::now();
		// f(customManager, maxSize, 0);
		for (int i = 0; i < n; i++)
			threads[i] = std::thread(f, (MemoryManager*)customManager, maxSize/n, i);
		for (int i = 0; i < n; i++)
			threads[i].join();
		elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		std::cout << "CustomManager - " << n << " threads ended: " << elapsed << "ms" << std::endl;
		std::cout << "free space = " << customManager->reportFreeSpace() / (1 << 20) << "MiB" << std::endl;
		std::cout << "total space = " << customManager->reportTotalSpace() / (1 << 20) << "MiB" << std::endl;

		std::cout << "BasicManager - " << n << " threads started" << std::endl;
		start = std::chrono::steady_clock::now();
		// f(basicManager, maxSize, 0);
		for (int i = 0; i < n; i++)
			threads[i] = std::thread(f, (MemoryManager*)basicManager, maxSize/n, i);
		for (int i = 0; i < n; i++)
			threads[i].join();
		elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		std::cout << "BasicManager - " << n << " threads ended: " << elapsed << "ms" << std::endl;
	}
}

int main()
{
	CustomMemoryManager* customManager = new CustomMemoryManager();
	BasicMemoryManager* basicManager = new BasicMemoryManager();
	constexpr size_t maxSize = 1000LL << 20;

	std::cout << "Integrity Test Start" << std::endl;

	std::cout << "Single Thread Test" << std::endl;
	integrityTestSmall(customManager, maxSize/10, 999'999'999);
	integrityTestSmall(customManager, maxSize , 999'999'999);
	integrityTestLarge(customManager, maxSize/10, 999'999'999);
	integrityTestLarge(customManager, maxSize, 999'999'999);
	integrityTestHuge(customManager, maxSize/10, 999'999'999);
	integrityTestHuge(customManager, maxSize, 999'999'999);

	std::cout << "IntegrityTestSmall" << std::endl;
	measure(customManager, basicManager, maxSize, integrityTestSmall);

	std::cout << "IntegrityTestLarge" << std::endl;
	measure(customManager, basicManager, maxSize, integrityTestLarge);

	std::cout << "IntegrityTestHuge" << std::endl;
	measure(customManager, basicManager, maxSize, integrityTestHuge);

	std::cout << "IntegrityTestMixed" << std::endl;
	measure(customManager, basicManager, maxSize, integrityTestMixed);

	std::cout << "Integrity Test End" << std::endl;

	std::cout << "Performance Test Start" << std::endl;

	std::cout << "PerformanceTestSmall" << std::endl;
	measure(customManager, basicManager, maxSize, performanceTestSmall);

	std::cout << "PerformanceTestLarge" << std::endl;
	measure(customManager, basicManager, maxSize, performanceTestLarge);

	std::cout << "PerformanceTestHuge" << std::endl;
	measure(customManager, basicManager, maxSize, performanceTestHuge);

	std::cout << "PerformanceTestMixed" << std::endl;
	measure(customManager, basicManager, maxSize, performanceTestMixed);

	std::cout << "Performance Test End" << std::endl;
}