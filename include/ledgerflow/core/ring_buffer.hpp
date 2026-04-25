//
// Created by jtwears on 4/25/26.
//

#pragma once
#include <cstddef>
#include <vector>

namespace ledgerflow::core {

    template<typename T>
    class RingBuffer {
    public:
        explicit RingBuffer(std::size_t capacity) : capacity_(capacity), buffer_(capacity), head_(0), tail_(0), size_(0) {}

        bool push(T& item) {
            if (size_ == capacity_) {
                return false; // Buffer is full
            }
            buffer_[tail_] = item;
            tail_ = (tail_ + 1) % capacity_;
            ++size_;
            return true;
        }

        bool pop(T& item) {
            if (size_ == 0) {
                return false; // Buffer is empty
            }
            item = buffer_[head_];
            head_ = (head_ + 1) % capacity_;
            --size_;
            return true;
        }

        bool drain(std::vector<T>& items) {
            if (size_ == 0) {
                return false; // Buffer is empty
            }
            items.clear();
            while (size_ > 0) {
                items.push_back(buffer_[head_]);
                head_ = (head_ + 1) % capacity_;
                --size_;
            }
            return true;
        }

        std::size_t size() const {
            return size_;
        }

        std::size_t capacity() const {
            return capacity_;
        }


    private:
        std::size_t capacity_;
        std::vector<T> buffer_;
        std::size_t head_;
        std::size_t tail_;
        std::size_t size_;
    };
}
