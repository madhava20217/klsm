#include "lsm.h"

#include <algorithm>
#include <cassert>

namespace kpq
{

template <class T>
class LSMElem
{
public:
    LSMElem() :
        m_used(false)
    {
    }

    bool operator<(const LSMElem<T> &that) const
    {
        if (!this->m_used && that.m_used) {
            return false;
        }

        if (this->m_used && !that.m_used) {
            return true;
        }

        return this->m_elem < that.m_elem;
    }

    T peek() const
    {
        assert(m_used);
        return m_elem;
    }

    T pop()
    {
        assert(m_used);
        m_used = false;
        return m_elem;
    }

    bool used() const
    {
        return m_used;
    }

    void put(const T &v)
    {
        assert(!m_used);
        m_used = true;
        m_elem = v;
    }

private:
    T m_elem;
    bool m_used;
};

template <class T>
class LSMBlock
{
public:
    LSMBlock(const T v) :
        m_prev(nullptr),
        m_next(nullptr),
        m_elems(1),
        m_first(0),
        m_size(1),
        m_capacity(1)
    {
        m_elems[0].put(v);
    }

    LSMBlock(LSMBlock<T> *lhs,
             LSMBlock<T> *rhs) :
        m_prev(nullptr),
        m_next(nullptr),
        m_elems(lhs->capacity() * 2),
        m_first(0),
        m_size(lhs->size() + rhs->size()),
        m_capacity(lhs->capacity() * 2)
    {
        assert(lhs->capacity() == rhs->capacity());

        std::merge(lhs->m_elems.begin(), lhs->m_elems.end(),
                   rhs->m_elems.begin(), rhs->m_elems.end(),
                   m_elems.begin());
    }

    bool peek(T &v) const
    {
        if (m_size == 0) {
            return false;
        }

        v = m_elems[m_first].peek();
        return true;
    }

    bool pop(T &v)
    {
        if (m_size == 0) {
            return false;
        }

        v = m_elems[m_first].pop();
        m_size--;
        m_first++;
        return true;
    }

    void shrink()
    {
        /* TODO: What happens if the block is emptied in delete_min? */
        std::vector<LSMElem<T>> new_elems(m_capacity / 2);

        uint32_t j = 0;
        for (uint32_t i = m_first; i < m_capacity; i++) {
            if (m_elems[i].used()) {
                new_elems[j++] = m_elems[i];
            }
        }

        m_elems = new_elems;

        m_first = 0;
        m_capacity /= 2;
    }

    size_t size() const
    {
        return m_size;
    }

    size_t capacity() const
    {
        return m_capacity;
    }

public:
    LSMBlock<T> *m_prev, *m_next;

private:
    std::vector<LSMElem<T>> m_elems;
    uint32_t m_first, m_size, m_capacity;
};

template <class T>
LSM<T>::LSM() :
    m_head(nullptr)
{
}

template <class T>
LSM<T>::~LSM()
{
    while (m_head != nullptr) {
        LSMBlock<T> *next = m_head->m_next;
        delete m_head;
        m_head = next;
    }
}

template <class T>
void
LSM<T>::insert(const T v)
{
    auto new_block = new LSMBlock<T>(v);

    while (m_head != nullptr && m_head->capacity() == new_block->capacity()) {
        auto merged_block = new LSMBlock<T>(m_head, new_block);
        auto old_head = m_head;

        m_head = m_head->m_next;

        delete old_head;
        delete new_block;

        new_block = merged_block;
    }

    if (m_head != nullptr) {
        m_head->m_prev = new_block;
    }

    new_block->m_next = m_head;
    m_head = new_block;
}

template <class T>
bool
LSM<T>::delete_min(T &v)
{
    if (m_head == nullptr) {
        return false;
    }

    auto best = m_head;
    for (auto l = best->m_next; l != nullptr; l = l->m_next) {
        T lhs, rhs;
        assert(l->peek(lhs));
        assert(best->peek(rhs));

        if (lhs < rhs) {
            best = l;
        }
    }

    if (best == nullptr || best->size() == 0) {
        return false;
    }

    assert(best->pop(v));

    if (best->size() == 0 && m_head == best) {
        /* Unlink empty blocks of capacity 1 or 2. */

        assert(best->m_prev == nullptr);

        m_head = best->m_next;
        if (m_head != nullptr) {
            m_head->m_prev = nullptr;
        }

        delete best;
    } else if (best->size() < best->capacity() / 2) {
        /* Merge with previous block. */

        best->shrink();

        auto lhs = best->m_prev;
        auto rhs = best;

        if (lhs != nullptr && lhs->capacity() == rhs->capacity()) {
            auto merged_block = new LSMBlock<T>(lhs, rhs);

            /* Reconnect on the left. */

            if (lhs->m_prev != nullptr) {
                lhs->m_prev->m_next = merged_block;
            } else {
                m_head = merged_block;
            }

            /* Reconnect on the right. */

            if (rhs->m_next != nullptr) {
                rhs->m_next->m_prev = merged_block;
            }

            merged_block->m_prev = lhs->m_prev;
            merged_block->m_next = rhs->m_next;

            delete lhs;
            delete rhs;
        }
    }

    return true;
}

template class LSM<uint32_t>;

}
