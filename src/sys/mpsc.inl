//
// Created by Loulfy on 25/09/2021.
//

template <typename T> MpscQueue<T>::MpscQueue() : _head(new Node), _tail(_head.load(std::memory_order_relaxed))
{
    // Linked queue is initialized with a fake node as a head node
    Node* front = _head.load(std::memory_order_relaxed);
    front->next.store(nullptr, std::memory_order_relaxed);
}

template <typename T> MpscQueue<T>::~MpscQueue()
{
    // Remove all nodes from the linked queue
    T item;
    while (dequeue(item))
    {
    }

    // Remove the last fake node
    Node* front = _head.load(std::memory_order_relaxed);
    delete front;
}

template <typename T> bool MpscQueue<T>::enqueue(const T& item)
{
    T temp = item;
    return enqueue(std::forward<T>(temp));
}

template <typename T> bool MpscQueue<T>::enqueue(T&& item)
{
    // Create new head node
    Node* node = new Node;
    if (node == nullptr)
        return false;

    // Fill new head node with the given value
    node->value = std::move(item);
    node->next.store(nullptr, std::memory_order_relaxed);

    // Insert new head node into the queue and linked it with the previous one
    Node* prev_head = _head.exchange(node, std::memory_order_acq_rel);
    prev_head->next.store(node, std::memory_order_release);

    return true;
}

template <typename T> bool MpscQueue<T>::dequeue(T& item)
{
    Node* tail = _tail.load(std::memory_order_relaxed);
    Node* next = tail->next.load(std::memory_order_acquire);

    // Check if the linked queue is empty
    if (next == nullptr)
        return false;

    // Get the item value
    item = std::move(next->value);

    // Update tail node with a next one
    _tail.store(next, std::memory_order_release);

    // Remove the previous tail node
    delete tail;

    return true;
}