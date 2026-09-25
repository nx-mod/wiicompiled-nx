#pragma once

#include "../internal.hpp"

#include <cstring>
#include <functional>

namespace aurora::gx::fifo {

namespace detail {
extern uint8_t* sBufferData;
extern uint32_t sBufferSize;
extern uint32_t sBufferCapacity;
extern bool sInDisplayList;
extern uint8_t* sDlBuffer;
extern uint32_t sDlSize;
extern uint32_t sDlWritePos;
} // namespace detail

void init();

// Out-of-line slow path: grows internal buffer then appends data
void write_data_grow(const void* data, uint32_t length);

inline void write_data(const void* data, const uint32_t length) {
  if (!detail::sInDisplayList)
    LIKELY {
      if (detail::sBufferSize + length <= detail::sBufferCapacity)
        LIKELY {
          std::memcpy(detail::sBufferData + detail::sBufferSize, data, length);
          detail::sBufferSize += length;
          return;
        }
      write_data_grow(data, length);
    }
  else if (detail::sDlWritePos + length <= detail::sDlSize) {
    std::memcpy(detail::sDlBuffer + detail::sDlWritePos, data, length);
    detail::sDlWritePos += length;
  }
}

inline void write_u8(const uint8_t val) {
  if (!detail::sInDisplayList)
    LIKELY {
      if (detail::sBufferSize < detail::sBufferCapacity)
        LIKELY {
          detail::sBufferData[detail::sBufferSize++] = val;
          return;
        }
      write_data_grow(&val, 1);
    }
  else if (detail::sDlWritePos < detail::sDlSize) {
    detail::sDlBuffer[detail::sDlWritePos++] = val;
  }
}

inline void write_u16(const uint16_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_u32(const uint32_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_u64(const uint64_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_f32(const float val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

// Display list recording
void begin_display_list(uint8_t* buf, uint32_t size);
uint32_t end_display_list();
bool in_display_list();

// Drain the internal FIFO buffer through the command processor. In threaded
// mode this hands the buffer to the decode worker and waits until everything
// queued so far has been decoded, so on return the decoder state is current.
void drain(const char* caller = __builtin_FUNCTION());

// Threaded decode: the producer only appends FIFO bytes and a worker thread
// runs the command processor. Everything that reads or writes decoder state
// (g_gxState, render passes) from the producer must call sync() first; hot
// paths that only append bytes call flush_async() instead.
bool threaded() noexcept;
bool on_decode_worker() noexcept;
void set_threaded(bool enabled) noexcept;
// `caller` names who waited, for the [gxthread] report of where syncs come from.
void sync(const char* caller = __builtin_FUNCTION()) noexcept;
// Waits for batches already handed to the worker, leaving the producer's
// current buffer in place (it belongs after whatever the caller does next).
void wait_idle() noexcept;
void flush_async() noexcept;
// Hands the buffer to the worker once it holds a worthwhile batch.
void maybe_flush_async() noexcept;
// Runs `step` in stream order: at once when decoding inline or while
// recording a display list, otherwise on the decode worker when it reaches
// this point. For producer-side code that changes decoder state (g_gxState,
// render passes) - copies, copy and z/scissor setters - so it needs no sync.
void defer(std::function<void()> step);
// Worker side: runs deferred step `index` of the batch being decoded.
void run_deferred(uint32_t index);

inline void sync_for_state_access(const char* caller = __builtin_FUNCTION()) noexcept {
  if (threaded()) UNLIKELY {
    sync(caller);
  }
}

// Internal buffer inspection
const uint8_t* get_buffer_data();
uint32_t get_buffer_size();
void clear_buffer();

} // namespace aurora::gx::fifo
