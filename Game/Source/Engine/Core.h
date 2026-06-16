#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <xhash>
#define LOG(msg, ...) printf(msg, __VA_ARGS__); printf("\n")

#ifdef _MSVC_LANG
#define DEBUG_BREAK() __debugbreak()
#endif

#define ASSERT(x) if (!(x)) DEBUG_BREAK()

#include <memory>
template<class T>
using Ref = std::shared_ptr<T>;

template<class T>
using Scope = std::unique_ptr<T>;

[[nodiscard]] inline uint64_t HashString(const std::string& str) { return std::hash<std::string>{}(str); }

template<class T>
[[nodiscard]] inline T* CastChecked(const void* tU) {
	T* data = static_cast<T*>(tU);
	ASSERT(data && "Cast failed");
	return data;
}

#ifdef _WIN32 
#define _platform_ALLOC(size) malloc(size)
#define _platform_FREE(ptr) free(ptr)
#elif defined(__linux__)
//#define _platform_ALLOC(size) malloc(size)
//#define _platform_FREE(ptr) free(ptr)
#endif

class Allocator final {
public:
	struct AllocationHeader {
		size_t Size;
		size_t PaddingOffset;
	};

	struct AllocationMetrices {
		size_t TotalAllocated = 0;
		size_t TotalFreed = 0;
		size_t PeakUsage = 0;
		const size_t GetCurrentUsage() const { return TotalAllocated - TotalFreed; }
	} static inline s_allocationMetrices;

	static void* Allocate(size_t size, size_t alignment = 16) {
		if (size == 0) return nullptr;
		if (alignment < alignof(AllocationHeader)) {
			alignment = alignof(AllocationHeader);
		}

		size_t headerSpace = sizeof(AllocationHeader);
		size_t alignmentRemainder = headerSpace % alignment;
		size_t padding = (alignmentRemainder == 0) ? 0 : (alignment - alignmentRemainder);
		size_t totalHeaderSize = headerSpace + padding;

		void* rawPtr = _aligned_malloc(size + totalHeaderSize, alignment);
		if (!rawPtr) return nullptr;

		void* userPtr = (char*)rawPtr + totalHeaderSize;

		AllocationHeader* header = (AllocationHeader*)((char*)userPtr - sizeof(AllocationHeader));
		header->Size = size;
		header->PaddingOffset = totalHeaderSize;

		s_allocationMetrices.TotalAllocated += size;
		CheckPeakUsage();

		return userPtr;
	}

	static void Free(void* memory, size_t size = 0) {
		if (!memory) return;

		AllocationHeader* header = (AllocationHeader*)((char*)memory - sizeof(AllocationHeader));
		void* rawPtr = (char*)memory - header->PaddingOffset;
		s_allocationMetrices.TotalFreed += header->Size;

		_aligned_free(rawPtr);
	}

	static void* Reallocate(void* original, size_t newSize, size_t alignment = 16) {
		if (!original) return Allocate(newSize, alignment);
		if (newSize == 0) {
			Free(original, 0);
			return nullptr;
		}

		AllocationHeader* header = (AllocationHeader*)((char*)original - sizeof(AllocationHeader));
		size_t originalSize = header->Size;

		void* newPtr = Allocate(newSize, alignment);
		if (newPtr) {
			size_t copySize = (originalSize < newSize) ? originalSize : newSize;
			std::memcpy(newPtr, original, copySize);
			Free(original, 0);
		}
		return newPtr;
	}

	template<class T, typename... Args>
	[[nodiscard]] static Ref<T> AllocRef(Args&&... args) { return std::make_shared<T>(std::forward<Args>(args)...); }
	template<class T, typename... Args>
	[[nodiscard]] static Scope<T> AllocScope(Args&&... args) { return std::make_unique<T>(std::forward<Args>(args)...); }
private:
	static void CheckPeakUsage() {
		size_t currentUsage = s_allocationMetrices.GetCurrentUsage();
		if (currentUsage > s_allocationMetrices.PeakUsage) { s_allocationMetrices.PeakUsage = currentUsage; }
	}
};

class GCObject {
public:
	virtual ~GCObject() { Destroy(); };
	virtual void Destroy() { _pendingDestroy = true; }
protected:
	bool _pendingDestroy = false;
	friend class GarbageCollector;
};

class GarbageCollector final {
public:
	GarbageCollector() = default;
	void register_ObjectToGC(GCObject* obj) { _objects.push_back(obj); }
	void collect() {
		LOG("GC: Collecting...");
		std::erase_if(_objects, [](GCObject* obj) {
			if (obj && obj->_pendingDestroy) {
				LOG("GC: DELETED");
				delete obj;
				return true;
			}
			return false;
			});
	}
private:
	std::vector<GCObject*> _objects;
};