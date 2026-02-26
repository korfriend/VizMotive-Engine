#pragma once
#include "../CommonInclude.h"
#include "Spinlock.h"

#include <mutex>
#include <atomic>
#include <memory>
#include <cassert>
#include <algorithm>
#include <vector>

namespace vz::allocator
{
	// Allocation and freeing of single elements of the same size
	template<typename T, size_t block_size = 256>
	struct BlockAllocator
	{
		struct Block
		{
			struct alignas(alignof(T)) RawStruct
			{
				uint8_t data[sizeof(T)];
			};
			std::vector<RawStruct> mem;
		};
		std::vector<Block> blocks;
		std::vector<T*> free_list;

		template<typename... ARG>
		inline T* allocate(ARG&&... args)
		{
			if (free_list.empty())
			{
				free_list.reserve(block_size);
				Block& block = blocks.emplace_back();
				block.mem.resize(block_size);
				T* ptr = (T*)block.mem.data();
				for (size_t i = 0; i < block_size; ++i)
				{
					free_list.push_back(ptr + i);
				}
			}
			T* ptr = free_list.back();
			free_list.pop_back();
			return new (ptr) T(std::forward<ARG>(args)...);
		}
		inline void free(T* ptr)
		{
			ptr->~T();
			free_list.push_back(ptr);
		}

		inline bool is_empty() const
		{
			return (blocks.size() * block_size) == free_list.size();
		}
	};


	// Interface for allocating pooled shared_ptr
	struct SharedBlockAllocator
	{
		virtual void init_refcount(void* ptr) = 0;
		virtual uint32_t get_refcount(void* ptr) = 0;
		virtual uint32_t inc_refcount(void* ptr) = 0;
		virtual uint32_t dec_refcount(void* ptr) = 0;
		virtual uint32_t get_refcount_weak(void* ptr) = 0;
		virtual uint32_t inc_refcount_weak(void* ptr) = 0;
		virtual uint32_t dec_refcount_weak(void* ptr) = 0;
		virtual bool try_inc_refcount(void* ptr) = 0;
	};

	// The per-type block allocators can be indexed with bottom 8 bits of the shared_ptr's handle:
	inline SharedBlockAllocator* block_allocators[256] = {};
	inline std::atomic<uint8_t> next_allocator_id{ 0 };
	inline uint8_t register_shared_block_allocator(SharedBlockAllocator* allocator)
	{
		uint8_t id = next_allocator_id.fetch_add(1);
		assert(id < 256);
		block_allocators[id] = allocator;
		return id;
	}
	inline uint8_t get_shared_block_allocator_count() { return next_allocator_id.load(); }

	// Shared ptr using a block allocation strategy, refcounted, thread-safe
	//	This makes it easy to swap-out std::shared_ptr, but not feature complete, only has minimal feature set
	//	Use this if you require many object of the same type, their memory allocation will be pooled
	//	If you require just a single object, it will be better to use std::shared_ptr instead
	//	Note: allocator pointer is stored directly to support DLL boundaries
	template<typename T>
	struct shared_ptr
	{
		T* ptr = nullptr;
		SharedBlockAllocator* allocator = nullptr;

		constexpr bool IsValid() const { return ptr != nullptr && allocator != nullptr; }

		constexpr T* get_ptr() const { return ptr; }
		constexpr SharedBlockAllocator* get_allocator() const { return allocator; }

		constexpr T* operator->() const { return ptr; }
		constexpr operator T* () const { return ptr; }
		constexpr T* get() const { return ptr; }

		template<typename U>
		operator shared_ptr<U>& () const { return *(shared_ptr<U>*)this; }

		shared_ptr() = default;
		shared_ptr(const shared_ptr& other) { copy(other); }
		shared_ptr(shared_ptr&& other) noexcept { move(other); }
		~shared_ptr() noexcept { reset(); }
		shared_ptr& operator=(const shared_ptr& other) { copy(other); return *this; }
		shared_ptr& operator=(shared_ptr&& other) noexcept { move(other); return *this; }

		void reset() noexcept
		{
			if (IsValid())
			{
				allocator->dec_refcount(ptr);
			}
			ptr = nullptr;
			allocator = nullptr;
		}
		void copy(const shared_ptr& other)
		{
			reset();
			ptr = other.ptr;
			allocator = other.allocator;
			if (IsValid())
			{
				allocator->inc_refcount(ptr);
			}
		}
		void move(shared_ptr& other) noexcept
		{
			if (this == &other)
				return;
			reset();
			ptr = other.ptr;
			allocator = other.allocator;
			other.ptr = nullptr;
			other.allocator = nullptr;
		}
		uint32_t use_count() const { return IsValid() ? allocator->get_refcount(ptr) : 0; }
	};

	// Similar to std::weak_ptr but works with the shared block allocator, and reduced feature set
	//	Note: allocator pointer is stored directly to support DLL boundaries
	template<typename T>
	struct weak_ptr
	{
		T* ptr = nullptr;
		SharedBlockAllocator* allocator = nullptr;

		constexpr bool IsValid() const { return ptr != nullptr && allocator != nullptr; }

		constexpr T* get_ptr() const { return ptr; }
		constexpr SharedBlockAllocator* get_allocator() const { return allocator; }

		template<typename U>
		operator weak_ptr<U>& () const { return *(weak_ptr<U>*)this; }

		weak_ptr() = default;
		weak_ptr(const weak_ptr& other) { copy(other); }
		weak_ptr(weak_ptr&& other) noexcept { move(other); }
		~weak_ptr() noexcept { reset(); }
		weak_ptr& operator=(const weak_ptr& other) { copy(other); return *this; }
		weak_ptr& operator=(weak_ptr&& other) noexcept { move(other); return *this; }

		weak_ptr(const shared_ptr<T>& other)
		{
			reset();
			ptr = other.ptr;
			allocator = other.allocator;
			if (IsValid())
			{
				allocator->inc_refcount_weak(ptr);
			}
		}

		shared_ptr<T> lock()
		{
			if (!IsValid())
				return {};

			if (allocator->try_inc_refcount(ptr))
			{
				shared_ptr<T> ret;
				ret.ptr = ptr;
				ret.allocator = allocator;
				return ret;
			}
			return {};
		}

		void reset() noexcept
		{
			if (IsValid())
			{
				allocator->dec_refcount_weak(ptr);
			}
			ptr = nullptr;
			allocator = nullptr;
		}
		void copy(const weak_ptr& other)
		{
			reset();
			ptr = other.ptr;
			allocator = other.allocator;
			if (IsValid())
			{
				allocator->inc_refcount_weak(ptr);
			}
		}
		void move(weak_ptr& other) noexcept
		{
			if (this == &other)
				return;
			reset();
			ptr = other.ptr;
			allocator = other.allocator;
			other.ptr = nullptr;
			other.allocator = nullptr;
		}
		uint32_t use_count() const { return IsValid() ? allocator->get_refcount(ptr) : 0; }
		bool expired() const noexcept
		{
			return !IsValid() || use_count() == 0;
		}
	};

	// Implementation of a thread-safe refcounted block allocator
	template<typename T, size_t block_size = 256>
	struct SharedBlockAllocatorImpl final : public SharedBlockAllocator
	{
		const uint8_t allocator_id = register_shared_block_allocator(this);

		struct alignas(std::max(size_t(256), alignof(T))) RawStruct
		{
			uint8_t data[sizeof(T)];
			std::atomic<uint32_t> refcount;
			std::atomic<uint32_t> refcount_weak;
		};
		static_assert(offsetof(RawStruct, data) == 0); // we assume that data is located at 0 when casting ptr to T*, this avoids having to do a function call that would return T* like the refcounts

		struct Block
		{
			std::unique_ptr<RawStruct[]> mem;
		};
		std::vector<Block> blocks;
		std::vector<RawStruct*> free_list;
		vz::SpinLock locker;

		template<typename... ARG>
		inline shared_ptr<T> allocate(ARG&&... args)
		{
			locker.lock();
			if (free_list.empty())
			{
				Block& block = blocks.emplace_back();
				block.mem.reset(new RawStruct[block_size]);
				RawStruct* ptr = block.mem.get();
				free_list.reserve(block_size);
				for (size_t i = 0; i < block_size; ++i)
				{
					free_list.push_back(ptr + i);
				}
			}
			RawStruct* ptr = free_list.back();
			assert((uint64_t)ptr == ((uint64_t)ptr & (~0ull << 8ull))); // The pointer lower 8 bits must be 0, it will be used as allocator index
			free_list.pop_back();
			locker.unlock();

			// Construction can be outside of lock, this structure wasn't shared yet:
			new (ptr) T(std::forward<ARG>(args)...);
			init_refcount(ptr);
			shared_ptr<T> allocation;
			allocation.ptr = reinterpret_cast<T*>(ptr);
			allocation.allocator = this;
			return allocation;
		}

		void reclaim(void* ptr)
		{
			std::scoped_lock lck(locker);
			free_list.push_back((RawStruct*)ptr);
		}

		void init_refcount(void* ptr) override
		{
			static_cast<RawStruct*>(ptr)->refcount.store(1, std::memory_order_relaxed);
			static_cast<RawStruct*>(ptr)->refcount_weak.store(1, std::memory_order_relaxed);
		}
		uint32_t get_refcount(void* ptr) override
		{
			return static_cast<RawStruct*>(ptr)->refcount.load(std::memory_order_acquire);
		}
		uint32_t inc_refcount(void* ptr) override
		{
			return static_cast<RawStruct*>(ptr)->refcount.fetch_add(1, std::memory_order_relaxed);
		}
		uint32_t dec_refcount(void* ptr) override
		{
			uint32_t old = static_cast<RawStruct*>(ptr)->refcount.fetch_sub(1, std::memory_order_acq_rel);
			if (old == 1)
			{
				static_cast<T*>(ptr)->~T();
				dec_refcount_weak(ptr);
			}
			return old;
		}
		uint32_t get_refcount_weak(void* ptr) override
		{
			return static_cast<RawStruct*>(ptr)->refcount_weak.load(std::memory_order_acquire);
		}
		uint32_t inc_refcount_weak(void* ptr) override
		{
			return static_cast<RawStruct*>(ptr)->refcount_weak.fetch_add(1, std::memory_order_relaxed);
		}
		uint32_t dec_refcount_weak(void* ptr) override
		{
			uint32_t old = static_cast<RawStruct*>(ptr)->refcount_weak.fetch_sub(1, std::memory_order_acq_rel);
			if (old == 1)
			{
				reclaim(ptr);
			}
			return old;
		}
		bool try_inc_refcount(void* ptr) override
		{
			auto& ref = static_cast<RawStruct*>(ptr)->refcount;
			uint32_t expected = ref.load(std::memory_order_acquire);
			do {
				if (expected == 0) {
					return false;
				}
			} while (!ref.compare_exchange_weak(expected, expected + 1, std::memory_order_acq_rel, std::memory_order_acquire));
			return true;
		}
	};

	// The allocators are global intentionally, this avoids runtime construction, guard check
	template<typename T, size_t block_size = 256>
	inline static SharedBlockAllocatorImpl<T, block_size>* shared_block_allocator = new SharedBlockAllocatorImpl<T, block_size>; // only destroyed after program exit, never earlier

	// Create a new shared pooled object:
	template<typename T, size_t block_size = 256, typename... ARG>
	inline shared_ptr<T> make_shared(ARG&&... args)
	{
		return shared_block_allocator<T, block_size>->allocate(std::forward<ARG>(args)...);
	}


	// Implementation of a thread-safe refcounted heap allocator for single objects
	//	Use this when you only need one or a few objects of the same type (not pooled)
	template<typename T>
	struct SharedHeapAllocator final : public SharedBlockAllocator
	{
		struct RawStruct
		{
			uint8_t data[sizeof(T)];
			std::atomic<uint32_t> refcount;
			std::atomic<uint32_t> refcount_weak;
		};
		static_assert(offsetof(RawStruct, data) == 0);

		template<typename... ARG>
		inline shared_ptr<T> allocate(ARG&&... args)
		{
			RawStruct* ptr = new RawStruct;
			new (ptr) T(std::forward<ARG>(args)...);
			init_refcount(ptr);
			shared_ptr<T> allocation;
			allocation.ptr = reinterpret_cast<T*>(ptr);
			allocation.allocator = this;
			return allocation;
		}

		void reclaim(void* ptr)
		{
			delete static_cast<RawStruct*>(ptr);
		}

		void init_refcount(void* ptr) override
		{
			static_cast<RawStruct*>(ptr)->refcount.store(1, std::memory_order_relaxed);
			static_cast<RawStruct*>(ptr)->refcount_weak.store(1, std::memory_order_relaxed);
		}
		uint32_t get_refcount(void* ptr) override
		{
			return static_cast<RawStruct*>(ptr)->refcount.load(std::memory_order_acquire);
		}
		uint32_t inc_refcount(void* ptr) override
		{
			return static_cast<RawStruct*>(ptr)->refcount.fetch_add(1, std::memory_order_relaxed);
		}
		uint32_t dec_refcount(void* ptr) override
		{
			uint32_t old = static_cast<RawStruct*>(ptr)->refcount.fetch_sub(1, std::memory_order_acq_rel);
			if (old == 1)
			{
				static_cast<T*>(ptr)->~T();
				dec_refcount_weak(ptr);
			}
			return old;
		}
		uint32_t get_refcount_weak(void* ptr) override
		{
			return static_cast<RawStruct*>(ptr)->refcount_weak.load(std::memory_order_acquire);
		}
		uint32_t inc_refcount_weak(void* ptr) override
		{
			return static_cast<RawStruct*>(ptr)->refcount_weak.fetch_add(1, std::memory_order_relaxed);
		}
		uint32_t dec_refcount_weak(void* ptr) override
		{
			uint32_t old = static_cast<RawStruct*>(ptr)->refcount_weak.fetch_sub(1, std::memory_order_acq_rel);
			if (old == 1)
			{
				reclaim(ptr);
			}
			return old;
		}
		bool try_inc_refcount(void* ptr) override
		{
			auto& ref = static_cast<RawStruct*>(ptr)->refcount;
			uint32_t expected = ref.load(std::memory_order_acquire);
			do {
				if (expected == 0) {
					return false;
				}
			} while (!ref.compare_exchange_weak(expected, expected + 1, std::memory_order_acq_rel, std::memory_order_acquire));
			return true;
		}
	};

	// Create a new shared individually allocated object (not pooled):
	template<typename T, typename... ARG>
	inline shared_ptr<T> make_shared_single(ARG&&... args)
	{
		// Allocator is created here, it's safer for global construction. The heap allocator is not used in high-performance code
		static SharedHeapAllocator<T>* shared_heap_allocator = new SharedHeapAllocator<T>; // only destroyed after program exit, never earlier
		return shared_heap_allocator->allocate(std::forward<ARG>(args)...);
	}

}
