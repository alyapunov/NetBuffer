#pragma once

template <size_t SIZE>
struct StupidAllocator
{
    static constexpr size_t ALLOC_SIZE = SIZE;
    union Block
    {
        char data[SIZE];
        Block* next;
    };

    Block* m_Next;
    size_t m_Count;

    StupidAllocator() : m_Next(), m_Count() {}

    void fill()
    {
        const size_t COUNT = 16;
        char *buf = new char[ALLOC_SIZE * COUNT];
        for (size_t i = 0; i < COUNT; i++)
        {
            Block* b = reinterpret_cast<Block*>(&buf[i * SIZE]);
            b->next = m_Next;
            m_Next = b;
        }
    }

    char* internal_alloc()
    {
        if (nullptr == m_Next)
            fill();
        char* r = m_Next->data;
        m_Next = m_Next->next;
        ++m_Count;
        return r;
    }

    void internal_free(char* aPtr)
    {
        Block* b = reinterpret_cast<Block*>(aPtr);
        b->next = m_Next;
        m_Next = b;
        --m_Count;
    }

    static StupidAllocator& instance() { static StupidAllocator inst; return inst; }

    static char* alloc()
    {
        return instance().internal_alloc();
    }

    static void free(char* aPtr)
    {
        instance().internal_free(aPtr);
    }

};