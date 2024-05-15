#pragma once
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
#include <type_traits>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory& other) = delete;
    RawMemory& operator=(const RawMemory& other) = delete;

    RawMemory(RawMemory&& rhs) noexcept
        : buffer_(rhs.buffer_)
        , capacity_(rhs.capacity_) {
        rhs.buffer_ = nullptr;
        rhs.capacity_ = 0;
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            this->Swap(rhs);
        }
        return *this;
    }

    ~RawMemory() {
        if (buffer_) {
            Deallocate(buffer_);
        }
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
}; 

template <typename T>
class Vector {
public:

    using iterator = T*;
    using const_iterator = const T*;
    
    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_ + size_;
    }

    const_iterator begin() const noexcept {
        return const_cast<Vector&>(*this).begin();
    }

    const_iterator end() const noexcept {
        return const_cast<Vector&>(*this).end();
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    const_iterator cend() const noexcept {
        return end();
    }

    explicit Vector() noexcept = default;

    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    void CopyWithOldCopacity(const Vector& rhs) {
        const size_t copy_size = (rhs.Size() < this->Size()) ? rhs.Size() : this->Size();
        (rhs.Size() < this->Size()) ? 
            std::destroy_n(data_ + rhs.Size(), size_ - rhs.Size()) :
            std::uninitialized_copy_n(rhs.data_ + this->Size(), rhs.Size() - size_, data_.GetAddress());
        std::copy(rhs.data_.GetAddress(), rhs.data_ + copy_size, data_.GetAddress());
        size_ = rhs.Size();  
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.Size() > this->Capacity()) {
                Vector<T> copy(rhs);
                this->Swap(copy);
            } else {
                CopyWithOldCopacity(rhs);
            }
        }
        return *this;
    }

    Vector(Vector &&other) noexcept 
        : data_(std::move(other.data_)), size_(other.size_)  {
            other.size_ = 0;
    }

    Vector& operator=(Vector&& rhs) noexcept  {
        if (this != &rhs) {
            data_.Swap(rhs.data_);
            std::swap(size_, rhs.size_);
        }
        return *this;
    }

    static void CopyOrMoveInNewData(T* from, size_t size, T* to) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(from, size, to);
        } else {
            std::uninitialized_copy_n(from, size, to);
        }
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        CopyOrMoveInNewData(data_.GetAddress(), size_, new_data.GetAddress());        
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size == size_) {
            return;
        }
        if (new_size < size_) {
            std::destroy_n(data_ + new_size, size_ - new_size);
        } else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
        }
        size_ = new_size;
    }

    template <typename... Types>
    T& EmplaceBack(Types&&... values) {
        return *Emplace(end(), std::forward<Types>(values)...);
    }

    void PushBack(const T& value) {
       EmplaceBack(value);
    }

    void PushBack(T&& value) {
       EmplaceBack(std::move(value));
    }

    void PopBack() noexcept {
        if (size_ == 0) {
            return;
        }
        --size_;
        std::destroy_at(data_ + size_);
    }
    
    iterator Insert(const_iterator cpos, const T& value) {
        return Emplace(cpos, value);
    }

    iterator Insert(const_iterator cpos, T&& value) {
        return Emplace(cpos, std::move(value));
    }

    void CopyData(T* from, size_t size, T* to, RawMemory<T>& new_data, T* new_pos) {
        try {
            CopyOrMoveInNewData(from, size, to);
        } catch(...) {
            new_pos->~T();
            std::destroy_n(new_data.GetAddress(), from - begin());
            throw;
        }
    }

    template <typename... Types>
    iterator Emplace(const_iterator cpos, Types&&... values) {
        assert(cpos >= cbegin() || cpos <= cend());
        iterator pos = const_cast<iterator>(cpos);
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : 2*Capacity());
            if (size_ == 0) {
                auto new_pos = new_data.GetAddress();
                new (new_pos) T(std::forward<Types>(values)...);
                data_.Swap(new_data);
                ++size_;
                return new_pos;
            }
            auto before{pos - begin()};
            auto after{end() - pos};
            T* new_pos = new_data + before;
            new (new_pos) T(std::forward<Types>(values)...);
            CopyData(begin(), before, new_data.GetAddress(), new_data, new_pos);
            CopyData(pos, after, new_pos + 1, new_data, new_pos);
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
            ++size_;
            return new_pos;
        }

        if (pos == end()) {
            new (pos) T(std::forward<Types>(values)...);
            ++size_;
            return pos;
        }
        T tmp(std::forward<Types>(values)...);
        new (end()) T(std::move(*(end() - 1)));
        std::move_backward(pos, end() - 1, end());
        *pos = std::move(tmp);
        ++size_;
        return pos;        
    }

    iterator Erase(const_iterator cpos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        assert(cpos >= cbegin() || cpos < cend());
        iterator pos = const_cast<iterator>(cpos);\
        std::move(pos + 1, end(), pos);
        PopBack();
        return pos;
    }


    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    ~Vector() {
        if (size_) {
            std::destroy_n(data_.GetAddress(), size_);
        };
    }

private:

    RawMemory<T> data_;
    size_t size_ = 0;
};
