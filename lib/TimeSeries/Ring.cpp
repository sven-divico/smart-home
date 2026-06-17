#include "Ring.h"

Ring::Ring(Sample* buf, uint16_t capacity) : buf_(buf), cap_(capacity) {}

void Ring::push(const Sample& s) {
  buf_[head_] = s;
  head_ = (uint16_t)((head_ + 1) % cap_);
  if (count_ < cap_) count_++;
}

uint16_t Ring::size() const { return count_ < cap_ ? count_ : cap_; }

const Sample& Ring::at(uint16_t i) const {
  // oldest lives at (head_ - size + i) modulo cap_
  uint16_t start = (uint16_t)((head_ + cap_ - size()) % cap_);
  return buf_[(uint16_t)((start + i) % cap_)];
}

const Sample& Ring::newest() const { return at((uint16_t)(size() - 1)); }

void Ring::load(uint16_t head, uint16_t count) {
  head_ = head % cap_;
  count_ = count > cap_ ? cap_ : count;
}
