#include "memory_manager.h"

#include <iostream>
#include <random>
#include <thread>
#include <vector>

// todo: debug the single-threaded run
// todo: debug the multi-threaded run
// todo: write the destructor of memory manager

using ll = long long;

using namespace CustomMemoryManagerConstants;

class BasicMemoryManager : public MemoryManager
{
	void* allocate(size_t size) override final { return std::malloc(size); }
	void free(void* ptr) override final { std::free(ptr); }
};

void integrityTest(MemoryManager* manager, const int N, int seed, int max)
{
	std::mt19937 generator(seed);
	std::uniform_int_distribution<int> distribution(1, max / sizeof(int));
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
			isAllocated[i] = true;
			for (int j = 0; j < elements[i].size(); j++)
				*(address[i] + j) = elements[i][j];
		}
	}
	// random free
	for (int i = 0; i < N; i++) {
		if (flag(generator)) {
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
}

void integrityTestSmall(MemoryManager* manager, const int N, int seed)
{
	integrityTest(manager, N, seed, SMALL_THRESHOLD);
}

void integrityTestLarge(MemoryManager* manager, const int N, int seed)
{
	integrityTest(manager, N, seed, LARGE_THRESHOLD);
}

void integrityTestHuge(MemoryManager* manager, const int N, int seed)
{
	integrityTest(manager, N, seed, 10 * (1 << 20));
}

void integrityTestMixed(MemoryManager* manager, const int N, int seed)
{

}

void measure(CustomMemoryManager* customManager, BasicMemoryManager* basicManager, int N, void (*f)(MemoryManager*, int, int))
{
	for (int n = 1; n <= 1; n *= 2)
	{
		std::chrono::steady_clock::time_point start;
		ll elapsed;

		start = std::chrono::steady_clock::now();
		f(basicManager, N, 0);
		elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		std::cout << "BasicManager - thread #" << n << ": " << elapsed << "ms" << std::endl;

		start = std::chrono::steady_clock::now();
		f(customManager, N, 0);
		elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		std::cout << "CustomManager - thread #" << n << ": " << elapsed << "ms" << std::endl;
	}

	//for (int n = 1; n <= 32; n *= 2)
	//{
	//	std::vector<std::thread> threads(n);
	//	std::chrono::steady_clock::time_point start;
	//  ll elapsed;
	// 
	//	start = std::chrono::steady_clock::now();
	//	for (int i = 0; i < n; i++)
	//		threads[i] = std::thread(f, (MemoryManager*)basicManager, N/n, i);
	//	for (int i = 0; i < n; i++)
	//		threads[i].join();
	//	elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	//	std::cout << "BasicManager - " << n << "thread: " << elapsed << "ms" << std::endl;
	// 
	//	start= std::chrono::steady_clock::now();
	//	for (int i = 0; i < n; i++)
	//		threads[i] = std::thread(f, (MemoryManager*)customManager, N/n, i);
	//	for (int i = 0; i < n; i++)
	//		threads[i].join();
	//	elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	//	std::cout << "CustomManager - " << n << "thread: " << elapsed << "ms" << std::endl;
	//}
}

int main()
{
	std::cout << "hello world" << std::endl;

	CustomMemoryManager* customManager = new CustomMemoryManager();
	BasicMemoryManager* basicManager = new BasicMemoryManager();

	std::cout << "IntegrityTestSmall" << std::endl; // up to 512B  // 1'000,000 * 512 / 2 = 256
	measure(customManager, basicManager, 1'000'000, integrityTestSmall);

	std::cout << "IntegrityTestLarge" << std::endl; // up to 256KiB // 4,000 * 0.25 / 2 = 500
	measure(customManager, basicManager, 4'000, integrityTestLarge);

	std::cout << "IntegrityTestHuge" << std::endl; // up to 10MiB
	measure(customManager, basicManager, 100, integrityTestHuge); // 100 * 10 / 2 = 500

	std::cout << "IntegrityTestMixed" << std::endl;
	measure(customManager, basicManager, 1'000'000, integrityTestMixed);
}