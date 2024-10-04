//
// Created by loria on 03/10/2024.
//

#pragma once

#include <type_traits>

namespace ler::rhi
{
// Mostly a copy of Microsoft::WRL::ComPtr<T>
template <typename T> class RefPtr
{
  public:
    typedef T InterfaceType;

  protected:
    InterfaceType* ptr_;
    template <class U> friend class ComPtr;

    void InternalAddRef() const noexcept
    {
        if (ptr_ != nullptr)
        {
            ptr_->AddRef();
        }
    }

    unsigned long InternalRelease() noexcept
    {
        unsigned long ref = 0;
        T* temp = ptr_;

        if (temp != nullptr)
        {
            ptr_ = nullptr;
            ref = temp->Release();
        }

        return ref;
    }

  public:
#pragma region constructors
    RefPtr() noexcept : ptr_(nullptr)
    {
    }

    explicit RefPtr(std::nullptr_t) noexcept : ptr_(nullptr)
    {
    }

    template <class U> explicit RefPtr(U* other) noexcept : ptr_(other)
    {
        InternalAddRef();
    }

    RefPtr(const RefPtr& other) noexcept : ptr_(other.ptr_)
    {
        InternalAddRef();
    }

    // copy constructor that allows to instantiate class when U* is convertible to T*
    template <class U>
    explicit RefPtr(const RefPtr<U>& other,
                    typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* = nullptr) noexcept
        : ptr_(other.ptr_)
    {
        InternalAddRef();
    }

    RefPtr(RefPtr&& other) noexcept : ptr_(nullptr)
    {
        if (this != reinterpret_cast<RefPtr*>(&reinterpret_cast<unsigned char&>(other)))
        {
            Swap(other);
        }
    }

    // Move constructor that allows instantiation of a class when U* is convertible to T*
    // typename std::enable_if<std::is_convertible<U*, T*>::value, void *>::type * = nullptr
    template <class U>
    explicit RefPtr(RefPtr<U>&& other,
                    typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* = nullptr) noexcept
        : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }
#pragma endregion

#pragma region destructor
    ~RefPtr() noexcept
    {
        InternalRelease();
    }
#pragma endregion

#pragma region assignment
    RefPtr& operator=(std::nullptr_t) noexcept
    {
        InternalRelease();
        return *this;
    }

    RefPtr& operator=(T* other) noexcept
    {
        if (ptr_ != other)
        {
            ComPtr(other).Swap(*this);
        }
        return *this;
    }

    template <typename U> RefPtr& operator=(U* other) noexcept
    {
        ComPtr(other).Swap(*this);
        return *this;
    }

    RefPtr& operator=(const RefPtr& other) noexcept // NOLINT(bugprone-unhandled-self-assignment)
    {
        if (ptr_ != other.ptr_)
        {
            ComPtr(other).Swap(*this);
        }
        return *this;
    }

    template <class U> RefPtr& operator=(const RefPtr<U>& other) noexcept
    {
        ComPtr(other).Swap(*this);
        return *this;
    }

    RefPtr& operator=(RefPtr&& other) noexcept
    {
        ComPtr(static_cast<RefPtr&&>(other)).Swap(*this);
        return *this;
    }

    template <class U> RefPtr& operator=(RefPtr<U>&& other) noexcept
    {
        ComPtr(static_cast<RefPtr<U>&&>(other)).Swap(*this);
        return *this;
    }
#pragma endregion

    void Swap(RefPtr&& r) noexcept
    {
        T* tmp = ptr_;
        ptr_ = r.ptr_;
        r.ptr_ = tmp;
    }

    void Swap(RefPtr& r) noexcept
    {
        T* tmp = ptr_;
        ptr_ = r.ptr_;
        r.ptr_ = tmp;
    }

    T** operator&() // NOLINT(google-runtime-operator)
    {
        return &ptr_;
    }

    explicit operator bool() const noexcept
    {
        return Get() != nullptr;
    }

    [[nodiscard]] T* Get() const noexcept
    {
        return ptr_;
    }

    explicit operator T*() const
    {
        return ptr_;
    }

    InterfaceType* operator->() const noexcept
    {
        return ptr_;
    }

    unsigned long Reset()
    {
        return InternalRelease();
    }
};
} // namespace ler::rhi
