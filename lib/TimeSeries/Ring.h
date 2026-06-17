#pragma once
#include <stdint.h>
#include "Sample.h"

// Fixed-capacity round-robin ring over a buffer the caller owns (Canvas1 pattern).
// at(0) is the oldest live sample, at(size()-1) the newest.
class Ring {
public:
  Ring(Sample* buf, uint16_t capacity);
  void push(const Sample& s);
  uint16_t size() const;            // min(count, capacity)
  const Sample& at(uint16_t i) const;  // precondition: i < size()
  const Sample& newest() const;        // precondition: !empty()
  bool empty() const { return count_ == 0; }

  // Persistence helpers: head/count are the only mutable state beyond the buffer.
  uint16_t head() const { return head_; }
  uint16_t rawCount() const { return count_; }
  uint16_t capacity() const { return cap_; }
  void load(uint16_t head, uint16_t count);

private:
  Sample*  buf_;
  uint16_t cap_;
  uint16_t head_  = 0;  // next write index
  uint16_t count_ = 0;  // live sample count, saturates at cap_
};
