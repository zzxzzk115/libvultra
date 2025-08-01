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
	        Tracky_(vk::Device aDevice, uint32_t aQueryCount);
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
            void bind_cmd_buffer( vk::CommandBuffer aCmdBuffer );
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
    void startup(vk::Device aDevice, uint32_t aQueryCount)
    {
        if( gTracky )
        {
            std::fprintf( stderr, "WARNING: tracky: already set up\n" );
            return;
        }

        gTracky = std::make_unique<Tracky_>(aDevice, aQueryCount);
    }
    void bind_cmd_buffer( vk::CommandBuffer aCmdBuffer )
    {
        assert( gTracky );
        gTracky->bind_cmd_buffer( aCmdBuffer );
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
	Tracky_::Tracky_(vk::Device aDevice, uint32_t aQueryCount)
#endif
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

#ifdef TRACKY_OPENGL
		// OpenGL resources
		gpu.mQueryBuffer.resize( kInitialQueries_ );
		glGenQueries( kInitialQueries_, gpu.mQueryBuffer.data() );
#elifdef TRACKY_VULKAN
        // Vulkan resources
	    mDevice = aDevice;
	    mMaxQueries = aQueryCount;
	    mQueryIndex = 0;
	    mVkInitialized = true;

	    vk::QueryPoolCreateInfo qinfo{};
	    qinfo.queryType = vk::QueryType::eTimestamp;
	    qinfo.queryCount = aQueryCount;
	    mQueryPool = aDevice.createQueryPool(qinfo);
#endif
	}

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
	    mDevice.destroyQueryPool(mQueryPool);
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
			record.scope.gpu.query = pull_query_();

#ifdef TRACKY_OPENGL
			glQueryCounter( record.scope.gpu.query, GL_TIMESTAMP );
#elifdef TRACKY_VULKAN
		    if (mVkInitialized)
		    {
		        mCmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, mQueryPool, record.scope.gpu.query);
		    }
#endif
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
			record.scope.gpu.query = pull_query_();
#ifdef TRACKY_OPENGL
		    glQueryCounter( record.scope.gpu.query, GL_TIMESTAMP );
#elifdef TRACKY_VULKAN
		    if (mVkInitialized)
		    {
		        mCmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, mQueryPool, record.scope.gpu.query);
		    }
#endif
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
			query = pull_query_();
#ifdef TRACKY_OPENGL
		    glQueryCounter( query, GL_TIMESTAMP );
#elifdef TRACKY_VULKAN
		    if (mVkInitialized)
		    {
		        mCmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, mQueryPool, query);
		    }
#endif
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

            if( !aThisIsTheEnd && mFrameNumber - frame[0].frame.number <= mFrameLag )
                break;

#ifdef TRACKY_OPENGL
            collect_gl_results_( frame.data(), frame.size() );
#elifdef TRACKY_VULKAN
            if (aThisIsTheEnd)
            {
                mCmdBuf = nullptr;
            }
            collect_vk_results_( frame.data(), frame.size() );
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
        mCmdBuf.resetQueryPool(mQueryPool, 0, mMaxQueries);
        mQueryIndex = 0;
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
    void Tracky_::bind_cmd_buffer(vk::CommandBuffer aCmdBuffer)
    {
        mCmdBuf = aCmdBuffer;
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
        auto ret = mQueryIndex++;
#endif
		return ret;
	}

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

        if (!mVkInitialized || mQueryIndex == 0) return;
        if (!aRecords || aCount == 0) return;

        std::vector<uint64_t> results(mQueryIndex);

        vk::Result result = mDevice.getQueryPoolResults(
            mQueryPool,
            0,
            mQueryIndex,
            sizeof(uint64_t) * mQueryIndex,
            results.data(),
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
                    if (rec.scope.gpu.query < mQueryIndex) {
                        rec.scope.gpu.result = results[rec.scope.gpu.query];
                    } else {
                        rec.scope.gpu.result = 0;
                    }
                } break;
                default:
                    break;
            }
        }

        if (mCmdBuf != nullptr)
        {
            mCmdBuf.resetQueryPool(mQueryPool, 0, mMaxQueries);
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

					auto const scopeGL = partner->scope.gpu.result - rec.scope.gpu.result;

					std::fprintf( aFof, "\"%s\", %f, %f, %f, \"%s\"\n", rec.scope.name, timestamp_(rec.scope.late), duration_(scopeDuration), scopeGL/1000.0, parent );

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

					auto const scopeGL = partner->scope.gpu.result - rec.scope.gpu.result;

					aggregatesCPU[rec.scope.name] += duration_(scopeDuration);
					aggregatesGL[rec.scope.name] += scopeGL / 1000.0;

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