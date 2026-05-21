#include "vultra/core/profiling/tracky.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map> // Would want a nicer hash table, e.g., flat_map of some kind
#include <vector>

#include <cassert>
#include <cstdio>

#ifdef TRACKY_OPENGL
#include <glad/glad.h>
#elifdef TRACKY_VULKAN
#include <vulkan/vulkan.hpp>
#else
#error "Undefined TRACKY GPU backend"
#endif

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif


// NOLINTBEGIN
// clang-format off
#if defined(__APPLE__)
    #define TRACKY_USE_THREAD_FALLBACK
#endif

#if defined(TRACKY_USE_THREAD_FALLBACK)
#include <thread>
#include <utility>

class JThread {
public:
    JThread() = default;

    template <typename Callable>
    explicit JThread(Callable&& func)
        : mThread(std::forward<Callable>(func)) {}

    // Move constructor
    JThread(JThread&& other) noexcept
        : mThread(std::move(other.mThread)) {}

    // Move assignment
    JThread& operator=(JThread&& other) noexcept {
        if (this != &other) {
            if (mThread.joinable())
                mThread.join();  // join old thread before overwriting
            mThread = std::move(other.mThread);
        }
        return *this;
    }

    // No copy
    JThread(const JThread&) = delete;
    JThread& operator=(const JThread&) = delete;

    ~JThread() {
        if (mThread.joinable())
            mThread.join();
    }

    void join() {
        if (mThread.joinable())
            mThread.join();
    }

private:
    std::thread mThread;
};
using JThread = JThread;
#else
#include <thread>
using JThread = std::jthread;
#endif

namespace
{
	using namespace tracky;

	using Clock_ = std::chrono::steady_clock;
	using Time_ = Clock_::time_point;

	using Usd_ = std::chrono::duration<double,std::micro>;

	constexpr std::size_t kInitialRecordBuffer_ = 256;
	constexpr std::size_t kNoLink_ = ~std::size_t(0);

#ifdef TRACKY_OPENGL
	constexpr GLsizei kInitialQueries_ = 128;
	constexpr GLsizei kQueryChunk_ = 32;
#endif

	// These are hardcoded for the moment
	constexpr char const* kOutputEvents_ = "tracky-events.csv";
	constexpr char const* kOutputAggregates_ = "tracky-agg.csv";
	constexpr char const* kOutputCounters_ = "tracky-counters.csv";

	enum class ERecord_
	{
		invalid,

		frameBegin,
		frameEnd,

		scopeEnter,
		scopeNext,
		scopeLeave,

		counter,
		counterPersistent,

		//teardown,
	};

	struct RecordFrame_
	{
		Time_ early, late;
		std::size_t number;
		uint32_t queryCount;
	};
	struct RecordScope_
	{
		char const* name;
		Time_ early, late;

		std::size_t partner, parent;

		union
		{
			uint32_t query;
			uint64_t result;
		} gpu;
	};
	struct RecordCount_
	{
		char const* name;
		long long value;
	};

	struct Record_
	{
		ERecord_ type;
		union
		{
			RecordFrame_ frame;
			RecordScope_ scope;
			RecordCount_ count;
		};

		constexpr
		Record_()
			: type(ERecord_::invalid)
		{}
	};

	struct Counter_
	{
		long long value;
		long long min, max;
	};

	class Tracky_
	{
		public:
#ifdef TRACKY_OPENGL
			Tracky_();
#elifdef TRACKY_VULKAN
	        Tracky_();
	        Tracky_(vk::Device aDevice, uint32_t aQueryCount, float aTimestampPeriodNs);
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
	        Tracky_(std::uintptr_t aInstanceHandle,
	                std::uintptr_t aDeviceHandle,
	                std::uintptr_t aQueueHandle,
	                uint32_t       aQueryCount,
	                bool           aSupportsTimestampQuery,
	                float          aTimestampPeriodNs);
#endif
#endif

	        ~Tracky_();

		public:
			void scope_enter( char const*, ExtraFlags );
			void scope_next( char const*, ExtraFlags );
			void scope_leave( ExtraFlags );

			void counter( char const*, long long, ExtraFlags );
			void persistent_counter( char const*, long long, ExtraFlags );

			void next_frame( bool aThisIsTheEnd = false );

			void flush();

			void set_frame_lag( std::size_t aLag = 5 );

#ifdef TRACKY_VULKAN
			void bind_cmd_buffer( std::uintptr_t aCmdBufferHandle,
			                     std::uintptr_t aRenderPassHandle,
			                     std::uintptr_t aComputePassHandle );
			void resolve_webgpu_queries();
#endif

		private:
			using Records_ = std::vector<Record_>;

		private:
			bool is_enabled_( ExtraFlags ) noexcept;

			uint32_t pull_query_();

#ifdef TRACKY_OPENGL
			void collect_gl_results_( Record_*, std::size_t );
#elifdef TRACKY_VULKAN
            void collect_vk_results_( Record_*, std::size_t );
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
	        void collect_webgpu_results_( Record_*, std::size_t );
#endif
#endif

			void scribe_();

			// Note: the following means that we iterate over the records
			// multiple times, which probably isn't ideal. (TODO)
			void fixup_( Record_*, std::size_t );
			void write_events_( FILE*, Record_ const*, std::size_t );
			void write_agg_( FILE*, Record_ const*, std::size_t );
			void write_counts_( FILE*, Record_ const*, std::size_t );

			double timestamp_( Time_ const& );

		private:
			// Settings
			unsigned long long mSoftLevel = (1llu<<16)-1;
			unsigned long long mSoftGroupMask = 0;

			std::size_t mFrameLag;
			std::size_t mFrameNumber = 0;

			Time_ mEpoch;

			// Current frame
			Records_* mActiveFrame = nullptr;
			std::vector<std::size_t> mRecordStack;

			// Any frame
			std::deque<Records_*> mPending;

			JThread mScribe;

#ifdef TRACKY_OPENGL
			struct
			{
				std::vector<uint32_t> mQueryBuffer;
			} gpu;
#elifdef TRACKY_VULKAN
	        vk::Device mDevice;
	        vk::QueryPool mQueryPool;
	        vk::CommandBuffer mCmdBuf;
	        uint32_t mQueryIndex = 0;
	        uint32_t mMaxQueries = 0;
	        bool mVkInitialized = false;
	        float mTimestampPeriodNs = 1.0f;
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
	        struct WebGPUFrameSlot_
	        {
	            WGPUQuerySet querySet {nullptr};
	            WGPUBuffer   queryResolveBuffer {nullptr};
	            WGPUBuffer   queryReadbackBuffer {nullptr};
	        };
	        std::vector<WebGPUFrameSlot_> mWebGPUFrames;
	        WGPUInstance                 mWebGPUInstance {nullptr};
	        WGPUDevice                   mWebGPUDevice {nullptr};
	        WGPUQueue                    mWebGPUQueue {nullptr};
	        WGPUCommandEncoder           mWebGPUCommandEncoder {nullptr};
	        WGPURenderPassEncoder        mWebGPURenderPassEncoder {nullptr};
	        WGPUComputePassEncoder       mWebGPUComputePassEncoder {nullptr};
	        uint32_t                     mWebGPUQueryIndex {0};
	        uint32_t                     mWebGPUQueryCount {0};
	        std::size_t                  mWebGPUFrameSlot {0};
	        bool                         mWebGPUInitialized {false};
	        bool                         mWebGPUSupportsTimestampQuery {false};
	        float                        mWebGPUTimestampPeriodNs {1.0f};
#endif
#endif

			// Internal statistics
			// Keep track of profiling overhead!

			// Thread shared. Access through mMutex.
			std::mutex mMut;
			std::condition_variable mCV;

			std::deque<Records_*> mFinal, mAvailable;

			// Only for the scribe
			// unordered_map :-(
			struct
			{
				std::unordered_map<std::string_view,Counter_> mPersistentCounters;
			} scribe;
	};

	std::unique_ptr<Tracky_> gTracky;
}

namespace tracky
{
	void scope_enter( char const* aName, ExtraFlags aFlags )
	{
		assert( gTracky );
		gTracky->scope_enter( aName, aFlags );
	}
	void scope_next( char const* aName, ExtraFlags aFlags )
	{
		assert( gTracky );
		gTracky->scope_next( aName, aFlags );
	}
	void scope_leave( ExtraFlags aFlags )
	{
		assert( gTracky );
		gTracky->scope_leave( aFlags );
	}
	void counter( char const* aName, long long aValue, ExtraFlags aFlags )
	{
		assert( gTracky );
		gTracky->counter( aName, aValue, aFlags );
	}
	void persistent_counter( char const* aName, long long aValue, ExtraFlags aFlags )
	{
		assert( gTracky );
		gTracky->persistent_counter( aName, aValue, aFlags );
	}

	void next_frame()
	{
		assert( gTracky );
		gTracky->next_frame();
	}

#ifdef TRACKY_OPENGL
	void startup()
	{
		if( gTracky )
		{
			std::fprintf( stderr, "WARNING: tracky: already set up\n" );
			return;
		}

		gTracky = std::make_unique<Tracky_>();
	}
#elifdef TRACKY_VULKAN
	void startup(vk::Device aDevice, uint32_t aQueryCount, float aTimestampPeriodNs)
    {
        if( gTracky )
        {
            std::fprintf( stderr, "WARNING: tracky: already set up\n" );
            return;
        }

		gTracky = std::make_unique<Tracky_>(aDevice, aQueryCount, aTimestampPeriodNs);
    }
	void startup_webgpu(std::uintptr_t aInstanceHandle,
	                    std::uintptr_t aDeviceHandle,
	                    std::uintptr_t aQueueHandle,
	                    uint32_t       aQueryCount,
	                    bool           aSupportsTimestampQuery,
	                    float          aTimestampPeriodNs)
	{
		if( gTracky )
		{
			std::fprintf( stderr, "WARNING: tracky: already set up\n" );
			return;
		}

		gTracky = std::make_unique<Tracky_>(aInstanceHandle,
		                                   aDeviceHandle,
		                                   aQueueHandle,
		                                   aQueryCount,
		                                   aSupportsTimestampQuery,
		                                   aTimestampPeriodNs);
	}
	void bind_cmd_buffer( std::uintptr_t aCmdBufferHandle,
	                      std::uintptr_t aRenderPassHandle,
	                      std::uintptr_t aComputePassHandle )
    {
        assert( gTracky );
		gTracky->bind_cmd_buffer( aCmdBufferHandle, aRenderPassHandle, aComputePassHandle );
    }
	void resolve_webgpu_queries()
	{
		assert( gTracky );
		gTracky->resolve_webgpu_queries();
	}
#endif
	void teardown()
	{
		if( gTracky )
			gTracky.reset();
	}
}


namespace
{
#ifdef TRACKY_OPENGL
    Tracky_::Tracky_()
#elifdef TRACKY_VULKAN
	Tracky_::Tracky_()
		: mEpoch( Clock_::now() )
	{
		set_frame_lag();

		for( std::size_t i = 0; i < mFrameLag+1; ++i )
		{
			auto* recs = mAvailable.emplace_back( new Records_ );
			recs->reserve( kInitialRecordBuffer_ );
		}

		mScribe = JThread( [self=this] () {
			self->scribe_();
		} );

		mVkInitialized = false;
		mTimestampPeriodNs = 1.0f;
	}

	Tracky_::Tracky_(vk::Device aDevice, uint32_t aQueryCount, float aTimestampPeriodNs)
#endif
		: mEpoch( Clock_::now() )
	{
		set_frame_lag();

#if defined(__APPLE__) && defined(TRACKY_VULKAN)
		set_frame_lag(0);
#endif
#ifdef TRACKY_VULKAN
		set_frame_lag(0);
#endif

		for( std::size_t i = 0; i < mFrameLag+1; ++i )
		{
			auto* recs = mAvailable.emplace_back( new Records_ );
			recs->reserve( kInitialRecordBuffer_ );
		}

		mScribe = JThread( [self=this] () {
			self->scribe_();
		} );

#ifdef TRACKY_OPENGL
		// OpenGL resources
		gpu.mQueryBuffer.resize( kInitialQueries_ );
		glGenQueries( kInitialQueries_, gpu.mQueryBuffer.data() );
#elifdef TRACKY_VULKAN
	    // Vulkan resources
	    mDevice = aDevice;
	    mMaxQueries = aQueryCount;
	    mQueryIndex = 1;
	    mVkInitialized = true;
	    mTimestampPeriodNs = aTimestampPeriodNs > 0.0f ? aTimestampPeriodNs : 1.0f;

	    vk::QueryPoolCreateInfo qinfo{};
	    qinfo.queryType = vk::QueryType::eTimestamp;
	    qinfo.queryCount = aQueryCount;
	    mQueryPool = aDevice.createQueryPool(qinfo);
#endif
	}

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
	Tracky_::Tracky_(std::uintptr_t aInstanceHandle,
	                std::uintptr_t aDeviceHandle,
	                std::uintptr_t aQueueHandle,
	                uint32_t       aQueryCount,
	                bool           aSupportsTimestampQuery,
	                float          aTimestampPeriodNs)
		: mEpoch( Clock_::now() )
	{
		set_frame_lag();

		for( std::size_t i = 0; i < mFrameLag+1; ++i )
		{
			auto* recs = mAvailable.emplace_back( new Records_ );
			recs->reserve( kInitialRecordBuffer_ );
		}

		mScribe = JThread( [self=this] () {
			self->scribe_();
		} );

		mWebGPUInstance                 = reinterpret_cast<WGPUInstance>(aInstanceHandle);
		mWebGPUDevice                   = reinterpret_cast<WGPUDevice>(aDeviceHandle);
		mWebGPUQueue                    = reinterpret_cast<WGPUQueue>(aQueueHandle);
		mWebGPUSupportsTimestampQuery    = aSupportsTimestampQuery;
		mWebGPUTimestampPeriodNs         = aTimestampPeriodNs > 0.0f ? aTimestampPeriodNs : 1.0f;
		mWebGPUQueryCount                = aQueryCount;
		mWebGPUQueryIndex                = 0;
		mWebGPUFrameSlot                 = 0;
		mWebGPUInitialized               = mWebGPUDevice != nullptr && mWebGPUQueue != nullptr;
		mWebGPUCommandEncoder            = nullptr;

		if( !mWebGPUInitialized || !mWebGPUSupportsTimestampQuery || mWebGPUQueryCount == 0 )
		{
			return;
		}

		mWebGPUFrames.resize( mFrameLag + 1 );
		for( auto& frame : mWebGPUFrames )
		{
			WGPUQuerySetDescriptor queryDesc {};
			queryDesc.type  = WGPUQueryType_Timestamp;
			queryDesc.count = mWebGPUQueryCount;
			frame.querySet  = wgpuDeviceCreateQuerySet( mWebGPUDevice, &queryDesc );

			WGPUBufferDescriptor resolveDesc {};
			resolveDesc.usage            = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
			resolveDesc.size             = static_cast<uint64_t>(mWebGPUQueryCount) * sizeof(uint64_t);
			resolveDesc.mappedAtCreation = false;
			frame.queryResolveBuffer     = wgpuDeviceCreateBuffer( mWebGPUDevice, &resolveDesc );

			WGPUBufferDescriptor readbackDesc {};
			readbackDesc.usage            = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
			readbackDesc.size             = static_cast<uint64_t>(mWebGPUQueryCount) * sizeof(uint64_t);
			readbackDesc.mappedAtCreation = false;
			frame.queryReadbackBuffer     = wgpuDeviceCreateBuffer( mWebGPUDevice, &readbackDesc );
		}
	}
#endif

	Tracky_::~Tracky_()
	{
#ifdef TRACKY_VULKAN
        if (mVkInitialized)
            mDevice.waitIdle();
#endif

		flush();

		// Send an empty set of records to the scribe. This will end the
		// scribe.
		Records_ empty;
		{
			std::unique_lock l{mMut};
			mFinal.emplace_back( &empty );
		}

		mCV.notify_one();

		// Wait.
		mScribe.join();

		// We can now free resources
		std::size_t frames = 0;
		std::size_t records = 0;
		for( auto* ptr : mAvailable )
		{
			if( ptr != &empty )
			{
				++frames;
				records += ptr->capacity();
				delete ptr;
			}
		}

#ifdef TRACKY_OPENGL
		// OpenGL resources
        std::size_t queries = gpu.mQueryBuffer.size();
		glDeleteQueries( gpu.mQueryBuffer.size(), gpu.mQueryBuffer.data() );
#elifdef TRACKY_VULKAN
	    // Vulkan resources
        std::size_t queries = mMaxQueries;
	    if (mVkInitialized && mDevice && mQueryPool)
	    {
	        mDevice.destroyQueryPool(mQueryPool);
	    }
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
	    // WebGPU resources
	    queries = mWebGPUQueryCount;
	    for( auto& frame : mWebGPUFrames )
	    {
	    	if( frame.querySet != nullptr )
	    	{
	    		wgpuQuerySetRelease( frame.querySet );
	    		frame.querySet = nullptr;
	    	}
	    	if( frame.queryResolveBuffer != nullptr )
	    	{
	    		wgpuBufferRelease( frame.queryResolveBuffer );
	    		frame.queryResolveBuffer = nullptr;
	    	}
	    	if( frame.queryReadbackBuffer != nullptr )
	    	{
	    		wgpuBufferRelease( frame.queryReadbackBuffer );
	    		frame.queryReadbackBuffer = nullptr;
	    	}
	    }
#endif
#endif

		// Final stats
		std::printf( "Note: tracky: allocations: %zu frames, %zu records, %zu queries\n", frames, records, queries );
	}

	inline
	bool Tracky_::is_enabled_( ExtraFlags aFlags ) noexcept
	{
		return detail::level(aFlags) <= mSoftLevel
			&& !(detail::groups(aFlags) & mSoftGroupMask)
		;
	}

	void Tracky_::scope_enter( char const* aName, ExtraFlags aFlags )
	{
		// Soft enablement check first to minimize costs
		if( !is_enabled_(aFlags) )
			return;

		// Measure time as early as possible
		auto const early = Clock_::now();

		// Enter record
		assert( mActiveFrame );

		auto parent = kNoLink_;
		if( !mRecordStack.empty() )
			parent = mRecordStack.back();

		mRecordStack.emplace_back( mActiveFrame->size() );
		auto& record = mActiveFrame->emplace_back();
		record.type = ERecord_::scopeEnter;
		record.scope.name = aName;
		record.scope.early = early;
		record.scope.partner = kNoLink_;
		record.scope.parent = parent;

		// Record OpenGL?
		// Do this relatively late.
		if( !!(EFlags::GPU & aFlags) )
		{
			record.scope.gpu.query = 0u;
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
			if( mWebGPUInitialized && mWebGPUSupportsTimestampQuery )
			{
				if( mWebGPUCommandEncoder != nullptr && mWebGPUComputePassEncoder == nullptr && mWebGPURenderPassEncoder == nullptr )
				{
					record.scope.gpu.query = pull_query_();
					if( mWebGPUQueryCount > 0 )
					{
						const auto& frame = mWebGPUFrames[mWebGPUFrameSlot];
						if( frame.querySet != nullptr && record.scope.gpu.query < mWebGPUQueryCount )
						{
							wgpuCommandEncoderWriteTimestamp( mWebGPUCommandEncoder, frame.querySet, record.scope.gpu.query );
						}
					}
				}
			}
			else
#endif
#ifdef TRACKY_VULKAN
			if( mVkInitialized && mCmdBuf != nullptr )
			{
				record.scope.gpu.query = pull_query_();
#ifdef TRACKY_OPENGL
				glQueryCounter( record.scope.gpu.query, GL_TIMESTAMP );
#elifdef TRACKY_VULKAN
				mCmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, mQueryPool, record.scope.gpu.query);
#endif
			}
			else
#endif
			{
			}
		}
		else
			record.scope.gpu.query = 0;

		// Late time
		record.scope.late = Clock_::now();
	}
	void Tracky_::scope_next( char const* aName, ExtraFlags aFlags )
	{
		if( !is_enabled_(aFlags) )
			return;

		// Measure time as early as possible
		auto const early = Clock_::now();

		// Enter record
		assert( mActiveFrame );
		assert( !mRecordStack.empty() );

		auto const partner = mRecordStack.back();

		mRecordStack.back() = mActiveFrame->size();
		auto& record = mActiveFrame->emplace_back();
		record.type = ERecord_::scopeNext;
		record.scope.name = aName;
		record.scope.early = early;
		record.scope.partner = partner;
		record.scope.parent = kNoLink_;

		// Record OpenGL?
		// We would like to do this in the middle of the function...
		if( !!(EFlags::GPU & aFlags) )
		{
			record.scope.gpu.query = 0u;
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
			if( mWebGPUInitialized && mWebGPUSupportsTimestampQuery )
			{
				if( mWebGPUCommandEncoder != nullptr && mWebGPUComputePassEncoder == nullptr && mWebGPURenderPassEncoder == nullptr )
				{
					record.scope.gpu.query = pull_query_();
					if( mWebGPUQueryCount > 0 )
					{
						const auto& frame = mWebGPUFrames[mWebGPUFrameSlot];
						if( frame.querySet != nullptr && record.scope.gpu.query < mWebGPUQueryCount )
						{
							wgpuCommandEncoderWriteTimestamp( mWebGPUCommandEncoder, frame.querySet, record.scope.gpu.query );
						}
					}
				}
			}
			else
#endif
#ifdef TRACKY_VULKAN
			if( mVkInitialized && mCmdBuf != nullptr )
			{
				record.scope.gpu.query = pull_query_();
#ifdef TRACKY_OPENGL
				glQueryCounter( record.scope.gpu.query, GL_TIMESTAMP );
#elifdef TRACKY_VULKAN
				mCmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, mQueryPool, record.scope.gpu.query);
#endif
			}
			else
#endif
			{
			}
		}
		else
			record.scope.gpu.query = 0;

		// Late time
		record.scope.late = Clock_::now();
	}
	void Tracky_::scope_leave( ExtraFlags aFlags )
	{
		if( !is_enabled_(aFlags) )
			return;

		// Measure time as early as possible
		auto const early = Clock_::now();

		// Record OpenGL?
		// Also early
		uint32_t query = 0;
		if( !!(EFlags::GPU & aFlags) )
		{
			query = 0u;
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
			if( mWebGPUInitialized && mWebGPUSupportsTimestampQuery )
			{
				if( mWebGPUCommandEncoder != nullptr && mWebGPUComputePassEncoder == nullptr && mWebGPURenderPassEncoder == nullptr )
				{
					query = pull_query_();
					if( mWebGPUQueryCount > 0 )
					{
						const auto& frame = mWebGPUFrames[mWebGPUFrameSlot];
						if( frame.querySet != nullptr && query < mWebGPUQueryCount )
						{
							wgpuCommandEncoderWriteTimestamp( mWebGPUCommandEncoder, frame.querySet, query );
						}
					}
				}
			}
			else
#endif
#ifdef TRACKY_VULKAN
			if( mVkInitialized && mCmdBuf != nullptr )
			{
				query = pull_query_();
#ifdef TRACKY_OPENGL
				glQueryCounter( query, GL_TIMESTAMP );
#elifdef TRACKY_VULKAN
				mCmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, mQueryPool, query);
#endif
			}
			else
#endif
			{
				query = 0u;
			}
		}

		// Enter record
		assert( mActiveFrame );
		assert( !mRecordStack.empty() );

		auto const partner = mRecordStack.back();

		mRecordStack.pop_back();
		auto& record = mActiveFrame->emplace_back();
		record.type = ERecord_::scopeLeave;
		record.scope.name = nullptr;
		record.scope.early = early;
		record.scope.partner = partner;
		record.scope.parent = kNoLink_;
		record.scope.gpu.query = query;

		// Late time
		record.scope.late = Clock_::now();
	}

	void Tracky_::counter( char const* aName, long long aValue, ExtraFlags aFlags )
	{
		if( !is_enabled_(aFlags) )
			return;
		//
		// Enter record
		assert( mActiveFrame );

		auto& record = mActiveFrame->emplace_back();
		record.type = ERecord_::counter;
		record.count.name = aName;
		record.count.value = aValue;
	}
	void Tracky_::persistent_counter( char const* aName, long long aValue, ExtraFlags aFlags )
	{
		if( !is_enabled_(aFlags) )
			return;
		//
		// Enter record
		assert( mActiveFrame );

		auto& record = mActiveFrame->emplace_back();
		record.type = ERecord_::counterPersistent;
		record.count.name = aName;
		record.count.value = aValue;
	}

	void Tracky_::next_frame( bool aThisIsTheEnd )
	{
		// Measure time as early as possible
		auto const early = Clock_::now();

		// Finalize last frame
		if( mActiveFrame && !mActiveFrame->empty() )
		{
			// Enter final record
			auto& last = mActiveFrame->emplace_back();
			last.type = ERecord_::frameEnd;
			last.frame.early = last.frame.late = early;
			last.frame.number = mFrameNumber;
			last.frame.queryCount = 0u;
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
			if( mWebGPUInitialized && mWebGPUSupportsTimestampQuery )
			{
				last.frame.queryCount = mWebGPUQueryIndex;
			}
#endif
#ifdef TRACKY_VULKAN
			if( mVkInitialized )
			{
				last.frame.queryCount = mQueryIndex;
			}
#endif

			// Set as pending
			mPending.emplace_back( mActiveFrame );
			mActiveFrame = nullptr;
		}

		// Process pending
		// This needs to occur on the main thread for OpenGL.
        while( !mPending.empty() )
        {
            auto& frame = *mPending.front();
            assert( !frame.empty() && ERecord_::frameBegin == frame[0].type );

			if( !aThisIsTheEnd && mFrameNumber - frame[0].frame.number < mFrameLag )
                break;

#ifdef TRACKY_OPENGL
            collect_gl_results_( frame.data(), frame.size() );
#elifdef TRACKY_VULKAN
            if (aThisIsTheEnd)
            {
                mCmdBuf = nullptr;
            }
            collect_vk_results_( frame.data(), frame.size() );
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
			if( mWebGPUInitialized )
			{
				collect_webgpu_results_( frame.data(), frame.size() );
			}
#endif
#endif

            // Send the records to the scribe
            mPending.pop_front();

            {
                std::unique_lock l{mMut};
                mFinal.emplace_back( &frame );
            }

            mCV.notify_one();
        }

		// Exit if this was the last frame
		if( aThisIsTheEnd )
			return;

		// Begin new frame
		++mFrameNumber;

#ifdef TRACKY_VULKAN
	if (mVkInitialized && mCmdBuf != nullptr)
	{
	    mCmdBuf.resetQueryPool(mQueryPool, 0, mMaxQueries);
	}
        mQueryIndex = 1;
#endif

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
		if( mWebGPUInitialized )
		{
			mWebGPUQueryIndex = 0;
			if( !mWebGPUFrames.empty() )
			{
				mWebGPUFrameSlot = mFrameNumber % mWebGPUFrames.size();
			}
		}
#endif

		{
			std::unique_lock l{mMut};
			if( !mAvailable.empty() )
			{
				mActiveFrame = mAvailable.front();
				mAvailable.pop_front();
			}
		}

		if( !mActiveFrame )
		{
			mActiveFrame = new Records_;
			mActiveFrame->reserve( kInitialRecordBuffer_ );
		}

		assert( mActiveFrame && mActiveFrame->empty() );
		auto& record = mActiveFrame->emplace_back();
		record.type = ERecord_::frameBegin;
		record.frame.early = early;
		record.frame.number = mFrameNumber;

		// Late time
		record.frame.late = Clock_::now();
	}

	void Tracky_::flush()
	{
		next_frame( true );
	}

	void Tracky_::set_frame_lag( std::size_t aLag )
	{
		mFrameLag = aLag;
	}

#ifdef TRACKY_VULKAN
	void Tracky_::bind_cmd_buffer(std::uintptr_t aCmdBufferHandle,
	                              std::uintptr_t aRenderPassHandle,
	                              std::uintptr_t aComputePassHandle)
    {
		if (mVkInitialized)
		{
			mCmdBuf = vk::CommandBuffer {reinterpret_cast<VkCommandBuffer>(aCmdBufferHandle)};
		}
		else
		{
			mCmdBuf = nullptr;
		}
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
		mWebGPUCommandEncoder = reinterpret_cast<WGPUCommandEncoder>(aCmdBufferHandle);
		mWebGPURenderPassEncoder  = reinterpret_cast<WGPURenderPassEncoder>(aRenderPassHandle);
		mWebGPUComputePassEncoder = reinterpret_cast<WGPUComputePassEncoder>(aComputePassHandle);
#endif
    }
#endif

	uint32_t Tracky_::pull_query_()
	{
#ifdef TRACKY_OPENGL
		if( gpu.mQueryBuffer.empty() )
		{
			gpu.mQueryBuffer.resize( kQueryChunk_ );
			glGenQueries( kQueryChunk_, gpu.mQueryBuffer.data() );
		}

		auto ret = gpu.mQueryBuffer.back();
		gpu.mQueryBuffer.pop_back();
#elifdef TRACKY_VULKAN
        auto ret = (mVkInitialized && mQueryIndex < mMaxQueries) ? mQueryIndex++ : 0u;
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        if( mWebGPUInitialized && mWebGPUSupportsTimestampQuery )
        {
			if( mWebGPUQueryIndex < mWebGPUQueryCount )
			{
				ret = mWebGPUQueryIndex++;
			}
			else
			{
				ret = 0u;
			}
        }
#endif
#endif
		return ret;
	}

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
	void Tracky_::resolve_webgpu_queries()
	{
		if( !mWebGPUInitialized || !mWebGPUSupportsTimestampQuery || mWebGPUCommandEncoder == nullptr )
		{
			return;
		}

		if( mWebGPUFrames.empty() || mWebGPUQueryIndex == 0 )
		{
			return;
		}

		const auto& frame = mWebGPUFrames[mWebGPUFrameSlot];
		if( frame.querySet == nullptr || frame.queryResolveBuffer == nullptr || frame.queryReadbackBuffer == nullptr )
		{
			return;
		}

		wgpuCommandEncoderResolveQuerySet( mWebGPUCommandEncoder,
		                                   frame.querySet,
		                                   0u,
		                                   mWebGPUQueryIndex,
		                                   frame.queryResolveBuffer,
		                                   0u );
		wgpuCommandEncoderCopyBufferToBuffer( mWebGPUCommandEncoder,
		                                     frame.queryResolveBuffer,
		                                     0u,
		                                     frame.queryReadbackBuffer,
		                                     0u,
		                                     static_cast<uint64_t>(mWebGPUQueryIndex) * sizeof(uint64_t) );
	}

	void Tracky_::collect_webgpu_results_( Record_* aRecords, std::size_t aCount )
	{
		assert( aRecords );

		if( !mWebGPUInitialized || !mWebGPUSupportsTimestampQuery || mWebGPUFrames.empty() )
		{
			for( std::size_t i = 0; i < aCount; ++i )
			{
				auto& rec = aRecords[i];
				switch( rec.type )
				{
					case ERecord_::scopeEnter:
					case ERecord_::scopeNext:
					case ERecord_::scopeLeave:
						rec.scope.gpu.result = 0;
						break;
					default:
						break;
				}
			}
			return;
		}

		const std::size_t slotIndex = aRecords[0].frame.number % mWebGPUFrames.size();
		const auto&       frame     = mWebGPUFrames[slotIndex];
		if( frame.querySet == nullptr || frame.queryResolveBuffer == nullptr || frame.queryReadbackBuffer == nullptr )
		{
			return;
		}

		const uint32_t queryCount = aRecords[aCount - 1].frame.queryCount;
		const uint64_t resultSize = static_cast<uint64_t>(queryCount) * sizeof(uint64_t);
		if( resultSize == 0u )
		{
			for( std::size_t i = 0; i < aCount; ++i )
			{
				auto& rec = aRecords[i];
				if( rec.type == ERecord_::scopeEnter || rec.type == ERecord_::scopeNext || rec.type == ERecord_::scopeLeave )
				{
					rec.scope.gpu.result = 0;
				}
			}
			return;
		}

		struct MapState_
		{
			bool completed {false};
			WGPUMapAsyncStatus status {WGPUMapAsyncStatus_Error};
		};

		auto onMap = [](WGPUMapAsyncStatus status,
		               WGPUStringView,
		               void* userdata1,
		               void*) {
			auto* state   = static_cast<MapState_*>(userdata1);
			state->status = status;
			state->completed = true;
		};

		MapState_ state {};
		WGPUBufferMapCallbackInfo mapInfo {};
		mapInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
		mapInfo.callback  = onMap;
		mapInfo.userdata1 = &state;
		mapInfo.userdata2 = nullptr;

		wgpuBufferMapAsync(frame.queryReadbackBuffer, WGPUMapMode_Read, 0u, resultSize, mapInfo);
		while( !state.completed )
		{
			if( mWebGPUInstance != nullptr )
			{
				wgpuInstanceProcessEvents( mWebGPUInstance );
			}
			else
			{
				std::this_thread::yield();
			}
		}

		if( state.status != WGPUMapAsyncStatus_Success )
		{
			std::fprintf( stderr, "Tracky WebGPU: buffer map failed: %d\n", static_cast<int>(state.status) );
			for( std::size_t i = 0; i < aCount; ++i )
			{
				auto& rec = aRecords[i];
				if( rec.type == ERecord_::scopeEnter || rec.type == ERecord_::scopeNext || rec.type == ERecord_::scopeLeave )
				{
					rec.scope.gpu.result = 0;
				}
			}
			wgpuBufferUnmap( frame.queryReadbackBuffer );
			return;
		}

		auto const* results = static_cast<uint64_t const*>(wgpuBufferGetConstMappedRange(frame.queryReadbackBuffer, 0u, resultSize));
		if( results == nullptr )
		{
			std::fprintf( stderr, "Tracky WebGPU: mapped results are null\n" );
			for( std::size_t i = 0; i < aCount; ++i )
			{
				auto& rec = aRecords[i];
				if( rec.type == ERecord_::scopeEnter || rec.type == ERecord_::scopeNext || rec.type == ERecord_::scopeLeave )
				{
					rec.scope.gpu.result = 0;
				}
			}
			wgpuBufferUnmap( frame.queryReadbackBuffer );
			return;
		}

		for( std::size_t i = 0; i < aCount; ++i )
		{
			auto& rec = aRecords[i];
			switch( rec.type )
			{
				case ERecord_::scopeEnter:
				case ERecord_::scopeNext:
				case ERecord_::scopeLeave:
					if( rec.scope.gpu.query < queryCount )
					{
						rec.scope.gpu.result = results[rec.scope.gpu.query];
					}
					else
					{
						rec.scope.gpu.result = 0;
					}
					break;
				default:
					break;
			}
		}

		wgpuBufferUnmap( frame.queryReadbackBuffer );
	}
#endif

#ifdef TRACKY_OPENGL
	void Tracky_::collect_gl_results_( Record_* aRecords, std::size_t aCount )
	{
		assert( aRecords );

		for( std::size_t i = 0; i < aCount; ++i )
		{
			auto& rec = aRecords[i];
			switch( rec.type )
			{
				using enum ERecord_;

				case scopeEnter: [[fallthrough]];
				case scopeNext: [[fallthrough]];
				case scopeLeave: {
					if( auto const q = rec.scope.gpu.query )
					{
						uint64_t res{};
						glGetQueryObjectui64v( q, GL_QUERY_RESULT, &res );
						rec.scope.gpu.result = res;

						gpu.mQueryBuffer.emplace_back( q );
					}
					else
						rec.scope.gpu.result = 0;
				} break;

				default:
					break;
			}
		}
	}
#elifdef TRACKY_VULKAN
    void Tracky_::collect_vk_results_( Record_* aRecords, std::size_t aCount )
    {
        assert( aRecords );

        if (!aRecords || aCount == 0) return;

        const uint32_t queryCount = aRecords[aCount - 1].frame.queryCount;
        if (!mVkInitialized || queryCount <= 1) {
            for (std::size_t i = 0; i < aCount; ++i) {
                auto& rec = aRecords[i];
                switch (rec.type) {
                    case ERecord_::scopeEnter:
                    case ERecord_::scopeNext:
                    case ERecord_::scopeLeave:
                        rec.scope.gpu.result = 0;
                        break;
                    default:
                        break;
                }
            }
            return;
        }

        std::vector<uint64_t> results(queryCount, 0);

        vk::Result result = mDevice.getQueryPoolResults(
            mQueryPool,
            1,
            queryCount - 1,
            sizeof(uint64_t) * (queryCount - 1),
            results.data() + 1,
            sizeof(uint64_t),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait
        );

        if (result != vk::Result::eSuccess) {
            std::fprintf(stderr, "Tracky Vulkan: getQueryPoolResults failed: %d\n", static_cast<int>(result));
            return;
        }

        for (std::size_t i = 0; i < aCount; ++i) {
            auto& rec = aRecords[i];
            switch (rec.type) {
                case ERecord_::scopeEnter:
                case ERecord_::scopeNext:
                case ERecord_::scopeLeave: {
                    if (rec.scope.gpu.query < queryCount) {
                        rec.scope.gpu.result = results[rec.scope.gpu.query];
                    } else {
                        rec.scope.gpu.result = 0;
                    }
                } break;
                default:
                    break;
            }
        }

    }
#endif


	void Tracky_::scribe_()
	{
		// Outputs XXX-FIXME
		FILE* event = std::fopen( kOutputEvents_, "wb" );
		if( !event )
			std::fprintf( stderr, "ERROR: tracky: unable to open '%s' for writing\n", kOutputEvents_ );
		else
			std::fprintf( event, "\"Event Name\",\"TimeStamp (µs)\",\"Duration (µs)\",\"GPU (µs)\",\"Parent Name\"\n" );

		FILE* agg = std::fopen( kOutputAggregates_, "wb" );
		if( !agg )
			std::fprintf( stderr, "ERROR: tracky: unable to open '%s' for writing\n", kOutputAggregates_ );
		else
			std::fprintf( agg, "\"Frame\",\"Name\",\"Duration (µs)\"\n" );

		FILE* count = std::fopen( kOutputCounters_, "wb" );
		if( !count )
			std::fprintf( stderr, "ERROR: tracky: unable to open '%s' for writing\n", kOutputCounters_ );
		else
			std::fprintf( count, "\"Frame\",\"Name\",\"Value\",\"Min\",\"Max\"\n" );


		// Process events
		while( 1 )
		{
			// Get next set of records.
			Records_* records = nullptr;
			{
				std::unique_lock l{mMut};
				while( mFinal.empty() )
					mCV.wait( l );

				assert( !mFinal.empty() );
				records = mFinal.front();
				mFinal.pop_front();
			}

			assert( records );

			// Empty records? Exit.
			// Note: we don't have to return the empty record set, since it's
			// allocated on the stack.
			if( records->empty() )
				break;

			// Process records
			fixup_( records->data(), records->size() );

			if( event )
				write_events_( event, records->data(), records->size() );
			if( agg )
				write_agg_( agg, records->data(), records->size() );
			if( count )
				write_counts_( count, records->data(), records->size() );

			///XXX-DEBUG
			//write_events_( stdout, records->data(), records->size() );
			//write_agg_( stdout, records->data(), records->size() );
			//write_counts_( stdout, records->data(), records->size() );

			// Return records buffer
			records->clear();

			{
				std::unique_lock l{mMut};
				mAvailable.emplace_back( records );
			}
		}

		// Cleanup
		if( agg )
			std::fclose( agg );

		if( event )
			std::fclose( event );
	}

	void Tracky_::fixup_( Record_* aRecords, std::size_t aCount )
	{
		assert( aRecords );

		for( std::size_t i = 0; i < aCount; ++i )
		{
			auto& rec = aRecords[i];
			switch( rec.type )
			{
				using enum ERecord_;

				case scopeEnter: [[fallthrough]];
				case scopeNext: [[fallthrough]];
				case scopeLeave: {
					Record_* partner = nullptr;
					if( kNoLink_ != rec.scope.partner )
					{
						assert( rec.scope.partner < aCount );
						partner = aRecords + rec.scope.partner;

						//TODO : verify that partner is a scope!
					}

					if( partner && kNoLink_ == rec.scope.parent )
						rec.scope.parent = partner->scope.parent;

					// Flip partners so that they point forward instead
					if( partner )
						partner->scope.partner = i;
				} break;

				default:
					break;
			}
		}
	}

	double duration_( auto aX )
	{
		return std::chrono::duration_cast<Usd_>(aX).count();
	}

	void Tracky_::write_events_( FILE* aFof, Record_ const* aRecords, std::size_t aCount )
	{
		assert( aRecords );

		double totalOverhead = 0.0;

		// Figure out frame durations and output
		assert( ERecord_::frameBegin == aRecords[0].type );
		assert( ERecord_::frameEnd == aRecords[aCount-1].type );

		auto const frameDuration = aRecords[aCount-1].frame.early - aRecords[0].frame.early;
		auto const frameOverhead = aRecords[0].frame.late - aRecords[0].frame.early;


		std::fprintf( aFof, "\"frame#%zu\", %f, %f, 0.0, \"\"\n", aRecords[0].frame.number, timestamp_(aRecords[0].frame.early), duration_(frameDuration) );

		totalOverhead += duration_(frameOverhead);

		// Output events
		auto const timestampPeriodNs = [&]() {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
			if( mWebGPUInitialized )
			{
				return static_cast<double>(mWebGPUTimestampPeriodNs);
			}
#endif
			return static_cast<double>(mTimestampPeriodNs);
		}();

		for( std::size_t i = 0; i < aCount; ++i )
		{
			auto const& rec = aRecords[i];
			switch( rec.type )
			{
				using enum ERecord_;

				case scopeEnter: [[fallthrough]];
				case scopeNext: {
					assert( kNoLink_ != rec.scope.partner );
					auto const* partner = aRecords + rec.scope.partner;

					char const* parent = "";
					if( kNoLink_ != rec.scope.parent )
						parent = aRecords[rec.scope.parent].scope.name;

					auto const scopeDuration = partner->scope.early - rec.scope.late;
					auto const scopeOverhead = rec.scope.late - rec.scope.early;

					double scopeGpuUs = 0.0;
					if( partner->scope.gpu.result >= rec.scope.gpu.result && rec.scope.gpu.result != 0 && partner->scope.gpu.result != 0 )
					{
						auto const scopeTicks = static_cast<double>(partner->scope.gpu.result - rec.scope.gpu.result);
						scopeGpuUs = (scopeTicks * timestampPeriodNs) / 1000.0;
					}

					std::fprintf( aFof, "\"%s\", %f, %f, %f, \"%s\"\n", rec.scope.name, timestamp_(rec.scope.late), duration_(scopeDuration), scopeGpuUs, parent );

					totalOverhead += duration_(scopeOverhead);
				} break;
				case scopeLeave: {
					auto const scopeOverhead = rec.scope.late - rec.scope.early;
					totalOverhead += duration_(scopeOverhead);
				} break;
				case ERecord_::invalid:
				case ERecord_::frameBegin:
				case ERecord_::frameEnd:
				case ERecord_::counter:
				case ERecord_::counterPersistent:
					break;
			}
		}

		//TODO: accumulate all direct descendants to frame
		//  CPU => output unaccounted for items.

		std::fprintf( aFof, "\"tracky-overhead\", 0.0, %f, 0.0, \"\"\n", totalOverhead );
	}
	void Tracky_::write_agg_( FILE* aFof, Record_ const* aRecords, std::size_t aCount )
	{
		assert( aRecords );

		double totalOverhead = 0.0;

		// Figure out frame durations and output
		assert( ERecord_::frameBegin == aRecords[0].type );
		assert( ERecord_::frameEnd == aRecords[aCount-1].type );

		auto const frameNumber = aRecords[0].frame.number;
		auto const frameDuration = aRecords[aCount-1].frame.early - aRecords[0].frame.early;
		auto const frameOverhead = aRecords[0].frame.late - aRecords[0].frame.early;

		std::fprintf( aFof, "%zu,\"frame\", %f\n", frameNumber, duration_(frameDuration) );

		totalOverhead += duration_(frameOverhead);

		// Accumulate events
		// Unordered_map sadness.
		std::unordered_map<std::string_view,double> aggregatesCPU, aggregatesGL;
		auto const timestampPeriodNs = [&]() {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
			if( mWebGPUInitialized )
			{
				return static_cast<double>(mWebGPUTimestampPeriodNs);
			}
#endif
			return static_cast<double>(mTimestampPeriodNs);
		}();

		for( std::size_t i = 0; i < aCount; ++i )
		{
			auto const& rec = aRecords[i];
			switch( rec.type )
			{
				using enum ERecord_;

				case scopeEnter: [[fallthrough]];
				case scopeNext: {
					assert( kNoLink_ != rec.scope.partner );
					auto const* partner = aRecords + rec.scope.partner;

					auto const scopeDuration = partner->scope.early - rec.scope.late;
					auto const scopeOverhead = rec.scope.late - rec.scope.early;

					double scopeGpuUs = 0.0;
					if( partner->scope.gpu.result >= rec.scope.gpu.result && rec.scope.gpu.result != 0 && partner->scope.gpu.result != 0 )
					{
						auto const scopeTicks = static_cast<double>(partner->scope.gpu.result - rec.scope.gpu.result);
						scopeGpuUs = (scopeTicks * timestampPeriodNs) / 1000.0;
					}

					aggregatesCPU[rec.scope.name] += duration_(scopeDuration);
					aggregatesGL[rec.scope.name] += scopeGpuUs;

					totalOverhead += duration_(scopeOverhead);
				} break;
				case scopeLeave: {
					auto const scopeOverhead = rec.scope.late - rec.scope.early;
					totalOverhead += duration_(scopeOverhead);
				} break;
				case ERecord_::invalid:
				case ERecord_::frameBegin:
				case ERecord_::frameEnd:
				case ERecord_::counter:
				case ERecord_::counterPersistent:
					break;
			}
		}

		// Output aggs
		for( auto const& agg : aggregatesCPU )
		{
			std::fprintf( aFof, "%zu,\"%s\", %f\n", frameNumber, agg.first.data(), agg.second );
		}
		for( auto const& agg : aggregatesGL )
		{
			std::fprintf( aFof, "%zu,\"%s::GPU\", %f\n", frameNumber, agg.first.data(), agg.second );
		}

		std::fprintf( aFof, "%zu,\"tracky-overhead\", %f\n", frameNumber, totalOverhead );
	}
	void Tracky_::write_counts_( FILE* aFof, Record_ const* aRecords, std::size_t aCount )
	{
		assert( aRecords );

		// Frame number
		auto const frameNumber = aRecords[0].frame.number;

		// Accumulate counters
		// Unordered_map sadness.
		std::unordered_map<std::string_view,Counter_> counters;

		for( std::size_t i = 0; i < aCount; ++i )
		{
			auto const& rec = aRecords[i];
			switch( rec.type )
			{
				using enum ERecord_;

				case counter: {
					auto [it,fresh] = counters.emplace( rec.count.name, Counter_{} );

					auto& cc = it->second;
					if( fresh )
					{
						cc.value = cc.min = cc.max = rec.count.value;
					}
					else
					{
						cc.value += rec.count.value;
						cc.min = std::min( cc.min, cc.value );
						cc.max = std::max( cc.max, cc.value );
					}
				} break;
				case counterPersistent: {
					auto [it,fresh] = scribe.mPersistentCounters.emplace( rec.count.name, Counter_{} );

					auto& cc = it->second;
					if( fresh )
					{
						cc.value = cc.min = cc.max = rec.count.value;
					}
					else
					{
						cc.value += rec.count.value;
						cc.min = std::min( cc.min, cc.value );
						cc.max = std::max( cc.max, cc.value );
					}
				} break;
				case ERecord_::invalid:
				case ERecord_::frameBegin:
				case ERecord_::frameEnd:
				case ERecord_::scopeEnter:
				case ERecord_::scopeNext:
				case ERecord_::scopeLeave:
					break;
			}
		}

		// Output counters
		for( auto const& cc : counters )
		{
			std::fprintf( aFof, "%zu,\"%s\", %lld, %lld, %lld\n", frameNumber, cc.first.data(), cc.second.value, cc.second.min, cc.second.max );
		}
		for( auto const& cc : scribe.mPersistentCounters )
		{
			std::fprintf( aFof, "%zu,\"%s\", %lld, %lld, %lld\n", frameNumber, cc.first.data(), cc.second.value, cc.second.min, cc.second.max );
		}
	}

	inline
	double Tracky_::timestamp_( Time_ const& aTime )
	{
		return duration_( aTime-mEpoch );
	}
}
// clang-format on
// NOLINTEND
