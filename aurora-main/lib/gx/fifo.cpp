#include "fifo.hpp"
#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXCommandList.h>
#if defined(__SWITCH__)
#include <switch.h>
#endif
#include "command_processor.hpp"
#include "../internal.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdlib>
#include <cstring>

#include "tracy/Tracy.hpp"

namespace aurora::gx::fifo {
static Module Log("aurora::gx::fifo");

namespace detail {
uint8_t* sBufferData = nullptr;
uint32_t sBufferSize = 0;
uint32_t sBufferCapacity = 0;
bool sInDisplayList = false;
uint8_t* sDlBuffer = nullptr;
uint32_t sDlSize = 0;
uint32_t sDlWritePos = 0;
} // namespace detail

void init() {
  constexpr uint32_t initialCapacity = 64 * 1024;
  reset_cp_register_cache();
  free(detail::sBufferData);
  detail::sBufferData = static_cast<uint8_t*>(malloc(initialCapacity));
  detail::sBufferSize = 0;
  detail::sBufferCapacity = initialCapacity;
  detail::sInDisplayList = false;
  detail::sDlBuffer = nullptr;
  detail::sDlSize = 0;
  detail::sDlWritePos = 0;
}

void write_data_grow(const void* data, uint32_t length) {
  uint32_t needed = detail::sBufferSize + length;
  uint32_t newCap = std::max(detail::sBufferCapacity * 2, needed);
  detail::sBufferData = static_cast<uint8_t*>(realloc(detail::sBufferData, newCap));
  std::memcpy(detail::sBufferData + detail::sBufferSize, data, length);
  detail::sBufferSize = needed;
  detail::sBufferCapacity = newCap;
}

void begin_display_list(uint8_t* buf, uint32_t size) {
  detail::sInDisplayList = true;
  detail::sDlBuffer = buf;
  detail::sDlSize = size;
  detail::sDlWritePos = 0;
}

uint32_t end_display_list() {
  detail::sInDisplayList = false;
  uint32_t bytesWritten = detail::sDlWritePos;
  uint32_t padded = (bytesWritten + 31) & ~31u;
  while (detail::sDlWritePos < padded && detail::sDlWritePos < detail::sDlSize) {
    detail::sDlBuffer[detail::sDlWritePos++] = 0;
  }
  detail::sDlBuffer = nullptr;
  detail::sDlSize = 0;
  detail::sDlWritePos = 0;
  return padded;
}

bool in_display_list() { return detail::sInDisplayList; }

// How much of the producer's frame is spent blocked before it may decode the next batch of GX commands.
static void note_drain_wait(uint64_t nanos) noexcept {
  ZoneScopedN("FIFO drain wait");
  TracyPlot("aurora: fifoDrainWaitUs", static_cast<int64_t>(nanos / 1000));
}

} // namespace aurora::gx::fifo
extern "C" {
extern std::atomic<const char*> g_gxDecodePhase;
extern std::atomic<uint32_t> g_gxDecodeQueued;
extern std::atomic<uint32_t> g_gxDecodeThreadHandle;
}
namespace aurora::gx::fifo {
namespace {
// One filled FIFO buffer waiting for (or being decoded by) the worker.
struct Batch {
  uint8_t* data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
  std::vector<std::function<void()>> deferred;  // indexed by GX_LOAD_AURORA_DEFERRED
};

// Steps deferred into the producer's current buffer; they travel with it.
std::vector<std::function<void()>> g_pendingDeferred;
// The steps of the batch the worker is decoding.
std::vector<std::function<void()>>* g_decodingDeferred = nullptr;

constexpr uint32_t kBatchFlushBytes = 32 * 1024;
constexpr size_t kMaxQueuedBatches = 8;
constexpr uint32_t kBatchCapacity = 64 * 1024;

struct DecodeWorker {
  std::mutex mutex;
  std::condition_variable workCv;
  std::condition_variable idleCv;
  std::deque<Batch> queue;
  std::vector<Batch> freeList;
  bool busy = false;
  bool stop = false;
  std::thread thread;
};
// Never destroyed: a joinable std::thread in a static destructor at process
// exit would call std::terminate.
DecodeWorker& g_decode = *new DecodeWorker;

std::atomic_bool g_threaded{false};

void decode_worker_main() noexcept {
#if defined(__SWITCH__)
  g_gxDecodeThreadHandle.store(threadGetCurHandle(), std::memory_order_relaxed);
#endif
  for (;;) {
    Batch batch;
    g_gxDecodePhase.store("idle", std::memory_order_relaxed);
    {
      std::unique_lock lock(g_decode.mutex);
      g_decode.workCv.wait(lock, [] { return g_decode.stop || !g_decode.queue.empty(); });
      if (g_decode.queue.empty()) {
        return;  // stop requested and nothing left
      }
      batch = g_decode.queue.front();
      g_decode.queue.pop_front();
      g_decode.busy = true;
      g_gxDecodeQueued.store(static_cast<uint32_t>(g_decode.queue.size()), std::memory_order_relaxed);
    }
    g_gxDecodePhase.store("wait sealed", std::memory_order_relaxed);
    // The same ordering the inline drain keeps: the frame worker must have
    // sealed the previous frame before this one's passes are touched.
    const auto waited = aurora::wait_for_frame_worker_sealed();
    if (waited.count() > 0) UNLIKELY {
      note_drain_wait(static_cast<uint64_t>(waited.count()));
    }
    g_gxDecodePhase.store("process", std::memory_order_relaxed);
    g_decodingDeferred = &batch.deferred;
    process(batch.data, batch.size, true);
    g_decodingDeferred = nullptr;
    batch.deferred.clear();
    g_gxDecodePhase.store("done", std::memory_order_relaxed);
    {
      std::lock_guard lock(g_decode.mutex);
      batch.size = 0;
      g_decode.freeList.push_back(batch);
      g_decode.busy = false;
    }
    g_decode.idleCv.notify_all();
  }
}
} // namespace

} // namespace aurora::gx::fifo

// How long the producer waits on the decode worker (syncs and a full queue),
// read once a second by the runtime's [vi] report, plus where the worker is
// and its handle so the runtime's PC sampler can watch it too.
extern "C" {
uint64_t g_gxSyncWaitNs = 0;
uint32_t g_gxSyncCount = 0;
std::atomic<const char*> g_gxDecodePhase{"not started"};
std::atomic<uint32_t> g_gxDecodeQueued{0};
std::atomic<uint32_t> g_gxDecodeThreadHandle{0};
}

namespace aurora::gx::fifo {
namespace {
struct SyncWaitTimer {
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  ~SyncWaitTimer() {
    g_gxSyncWaitNs += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
  }
};
} // namespace

bool threaded() noexcept { return g_threaded.load(std::memory_order_relaxed); }

bool on_decode_worker() noexcept { return std::this_thread::get_id() == g_decode.thread.get_id(); }

void flush_async() noexcept {
  if (!threaded() || detail::sBufferSize == 0 || detail::sInDisplayList || on_decode_worker()) {
    return;
  }
  // Between aurora_end_frame and the next begin the frame worker has not
  // prepared a frame to decode into, and it only does once the producer
  // begins one. Handing a batch over now would park the decoder on that and,
  // through any sync(), the producer with it. Inline decode leaves these bytes
  // buffered until after begin; so does this.
  if (!aurora::frame_worker_accepting_gx()) {
    return;
  }
  Batch filled{detail::sBufferData, detail::sBufferSize, detail::sBufferCapacity, std::move(g_pendingDeferred)};
  g_pendingDeferred = {};
  Batch next;
  {
    SyncWaitTimer waitTimer;
    std::unique_lock lock(g_decode.mutex);
    // Bounded: a producer far ahead of the decoder waits here rather than
    // growing an unbounded backlog of frames' worth of commands.
    g_decode.idleCv.wait(lock, [] { return g_decode.queue.size() < kMaxQueuedBatches; });
    if (!g_decode.freeList.empty()) {
      next = g_decode.freeList.back();
      g_decode.freeList.pop_back();
    }
    g_decode.queue.push_back(filled);
  }
  g_decode.workCv.notify_one();
  if (next.data == nullptr) {
    next.data = static_cast<uint8_t*>(malloc(kBatchCapacity));
    next.capacity = kBatchCapacity;
  }
  detail::sBufferData = next.data;
  detail::sBufferSize = 0;
  detail::sBufferCapacity = next.capacity;
}

void defer(std::function<void()> step) {
  if (!threaded() || detail::sInDisplayList || on_decode_worker()) {
    step();
    return;
  }
  const uint32_t index = static_cast<uint32_t>(g_pendingDeferred.size());
  g_pendingDeferred.push_back(std::move(step));
  write_u8(GX_LOAD_AURORA);
  write_u16(GX_LOAD_AURORA_DEFERRED);
  write_u32(index);
}

void run_deferred(uint32_t index) {
  if (g_decodingDeferred != nullptr && index < g_decodingDeferred->size()) {
    (*g_decodingDeferred)[index]();
  }
}

void maybe_flush_async() noexcept {
  if (detail::sBufferSize >= kBatchFlushBytes) {
    flush_async();
  }
}

// Which callers synced, and how often: a handful of distinct names at most.
struct SyncSite {
  const char* caller;
  uint32_t count;
};
std::array<SyncSite, 24> g_syncSites{};

void note_sync_site(const char* caller) noexcept {
  for (auto& site : g_syncSites) {
    if (site.caller == caller) {
      ++site.count;
      return;
    }
    if (site.caller == nullptr) {
      site = {caller, 1};
      return;
    }
  }
}

void sync(const char* caller) noexcept {
  // From the worker itself (a fault handler reached from process()) there is
  // nothing to wait for: everything before this point is what it is decoding.
  if (!threaded() || on_decode_worker()) {
    return;
  }
  ++g_gxSyncCount;
  note_sync_site(caller);
  SyncWaitTimer waitTimer;
  if (!aurora::producer_frame_begun() && !aurora::frame_worker_accepting_gx()) {
    // Between frame end and the next begin: frame end already synced, so the
    // worker is idle and stays so (nothing is handed over until a frame is
    // prepared). Waiting for the frame worker here would wait for a begin
    // only this thread can make - the first threaded build deadlocked so.
    std::unique_lock lock(g_decode.mutex);
    g_decode.idleCv.wait(lock, [] { return g_decode.queue.empty() && !g_decode.busy; });
    return;
  }
  // What the inline drain waits for before it decodes: the frame worker has
  // sealed the last frame and prepared this one. Code that syncs then touches
  // render passes itself, and must not race the frame worker preparing them.
  aurora::wait_for_frame_worker_sealed();
  flush_async();
  std::unique_lock lock(g_decode.mutex);
  g_decode.idleCv.wait(lock, [] { return g_decode.queue.empty() && !g_decode.busy; });
}

void wait_idle() noexcept {
  if (!threaded() || on_decode_worker()) {
    return;
  }
  std::unique_lock lock(g_decode.mutex);
  g_decode.idleCv.wait(lock, [] { return g_decode.queue.empty() && !g_decode.busy; });
}

void set_threaded(bool enabled) noexcept {
  if (enabled == threaded()) {
    return;
  }
  if (enabled) {
    // Everything already buffered is decoded inline first, so the worker
    // starts from a clean boundary.
    drain();
    {
      std::lock_guard lock(g_decode.mutex);
      g_decode.stop = false;
    }
    g_decode.thread = std::thread(decode_worker_main);
    g_threaded.store(true, std::memory_order_relaxed);
    Log.info("Threaded GX decode enabled");
  } else {
    sync();
    g_threaded.store(false, std::memory_order_relaxed);
    {
      std::lock_guard lock(g_decode.mutex);
      g_decode.stop = true;
    }
    g_decode.workCv.notify_all();
    if (g_decode.thread.joinable()) {
      g_decode.thread.join();
    }
    Log.info("Threaded GX decode disabled");
  }
}

void drain(const char* caller) {
  if (threaded()) {
    sync(caller);
    return;
  }
  // SEALED, not DONE.
  const auto waited = aurora::wait_for_frame_worker_sealed();
  if (waited.count() > 0) UNLIKELY {
    note_drain_wait(static_cast<uint64_t>(waited.count()));
  }
  if (detail::sBufferSize == 0) {
    return;
  }
  process(detail::sBufferData, detail::sBufferSize, true);
  detail::sBufferSize = 0;
}

const uint8_t* get_buffer_data() { return detail::sBufferData; }
uint32_t get_buffer_size() { return detail::sBufferSize; }
void clear_buffer() {
  detail::sBufferSize = 0;
}

} // namespace aurora::gx::fifo

// For the runtime's [gxthread] report: the busiest sync callers since the last
// call, written as "name xN" into `out`, and the counts reset.
extern "C" void aurora_gx_sync_sites(char* out, size_t size) {
  using aurora::gx::fifo::g_syncSites;
  size_t used = 0;
  if (size != 0) {
    out[0] = '\0';
  }
  for (int pick = 0; pick < 5; ++pick) {
    aurora::gx::fifo::SyncSite* best = nullptr;
    for (auto& site : g_syncSites) {
      if (site.caller != nullptr && site.count != 0 && (best == nullptr || site.count > best->count)) {
        best = &site;
      }
    }
    if (best == nullptr || used + 1 >= size) {
      break;
    }
    const int n = std::snprintf(out + used, size - used, "%s%s x%u", used ? ", " : "", best->caller, best->count);
    used += n > 0 ? static_cast<size_t>(n) : 0;
    best->count = 0;
  }
  for (auto& site : g_syncSites) {
    site.count = 0;
  }
}
