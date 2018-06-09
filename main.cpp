#include <iostream>
#include <assert.h>
#include <vector>

#include <alya.h>
#include <climits>

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

template<size_t SIZE> struct Log { static constexpr size_t VALUE = 1 + Log<SIZE / 2>::VALUE; };
template<> struct Log<1> { static constexpr size_t VALUE = 0; };

template<size_t BASE, size_t POWER> struct Pow { static constexpr size_t VALUE = BASE * Pow<BASE, POWER-1>::VALUE; };
template<size_t BASE> struct Pow<BASE, 0> { static constexpr size_t VALUE = 1; };

struct NetBufferTraitsBase
{
    /* Size (in bytes) of dynamically allocated tree nodes */
    static constexpr size_t CHUNK_SIZE = 8192;
    /* Size (count) of top level node of the tree */
    static constexpr size_t L0_SIZE = 8;
    /* Height of the tree */
    static constexpr size_t HEIGHT = 3;
    /* Allocator for dynamically allocated tree nodes */
    typedef StupidAllocator<CHUNK_SIZE> Allocator;
};

template<class T = NetBufferTraitsBase>
class NetBuffer
{
public:
    struct Link {
        union {
            Link* child;
            char* data;
        };
        uintptr_t size;
    };
    static constexpr size_t MIDDLE_SIZE = T::CHUNK_SIZE / sizeof(Link);
    static constexpr size_t SUBTREE_CARDINALITY = T::CHUNK_SIZE * Pow<MIDDLE_SIZE, T::HEIGHT - 2>::VALUE;
    static constexpr size_t CARDINALITY = T::L0_SIZE * SUBTREE_CARDINALITY;

    static_assert(0 == (T::CHUNK_SIZE & (T::CHUNK_SIZE - 1)), "CHUNK_SIZE must be power of 2");
    static_assert(0 == (T::L0_SIZE & (T::L0_SIZE - 1)), "L0_SIZE must be power of 2");
    static_assert(T::CHUNK_SIZE >= sizeof(Link) * 2, "CHUNK_SIZE must be large enough");
    static_assert(T::L0_SIZE >= 2, "L0_SIZE must be >= 2");
    static_assert(T::HEIGHT >= 2, "HEIGHT must be >= 2");
    static_assert(T::Allocator::ALLOC_SIZE == T::CHUNK_SIZE, "ALLOC_SIZE must be equal to CHUNK_SIZE");

    NetBuffer() : m_Begin(0), m_End(0) {}
	~NetBuffer();

    size_t begin() const { return m_Begin; }
    size_t end() const { return m_End; }
    inline size_t alloc(size_t aSize);
    inline void unalloc(size_t aSize);
    inline void erase(size_t aPos, size_t aSize);
    inline void insert(size_t aPos, size_t aSize);
    inline void free(size_t aFrom, size_t aSize);
    inline void begin(size_t aPos) { m_Begin = aPos; }

    inline char& operator[](size_t i);
    inline const char& operator[](size_t i) const;
    inline void set(size_t aPos, const char *aData, size_t aSize);
    template <class U> inline void set(size_t aPos, const U& t);
    inline void get(size_t aPos, char *aData, size_t aSize) const;
	template <class U> inline void get(size_t aPos, U& t) const;

private:
    size_t m_Begin;
    size_t m_End;
    Link m_Root[T::L0_SIZE];

    Link* allocMiddle() { return reinterpret_cast<Link*>(T::Allocator::alloc()); }
    void freeMiddle(Link* aLink) { return T::Allocator::free(reinterpret_cast<char*>(aLink)); }
    static constexpr size_t root_offset(size_t aPos)
    {
        const size_t sShift = Log<T::CHUNK_SIZE>::VALUE + (T::HEIGHT - 2) * Log<MIDDLE_SIZE>::VALUE;
        return (aPos >> sShift) & (T::L0_SIZE - 1);
    }
    static constexpr size_t mid_offset(size_t aPos, size_t aLvl)
    {
        assert(aLvl > 0 && aLvl < T::HEIGHT - 1);
        const size_t sShift = Log<T::CHUNK_SIZE>::VALUE + (T::HEIGHT - 2 - aLvl) * Log<MIDDLE_SIZE>::VALUE;
        return (aPos >> sShift) & (MIDDLE_SIZE - 1);
    }
    static constexpr size_t data_offset(size_t aPos)
    {
        return aPos & (T::CHUNK_SIZE - 1);
    }
};


template<class T>
NetBuffer<T>::~NetBuffer()
{
    if (m_Begin == m_End)
        return;
}

template<class T>
size_t NetBuffer<T>::alloc(size_t aSize)
{
    size_t sReservedBegin = m_Begin & ~(SUBTREE_CARDINALITY - 1);
    if (m_End + aSize - sReservedBegin > CARDINALITY)
        throw std::bad_alloc();

    size_t sNewEnd = m_End + aSize;
    try {
        size_t sAllocatedEnd = (m_End + T::CHUNK_SIZE - 1) & ~(T::CHUNK_SIZE - 1);
        while (sAllocatedEnd < sNewEnd)
        {
            size_t sSubtreeCardinality = SUBTREE_CARDINALITY;
            Link* sLink = &m_Root[sAllocatedEnd / sSubtreeCardinality % T::L0_SIZE];
            size_t sNextOffsetVector = sAllocatedEnd % sSubtreeCardinality;
            for (size_t h = 0; h < T::HEIGHT - 2; h++)
            {
                if (0 == sNextOffsetVector)
                {
                    sLink->child = nullptr;
                    sLink->child = allocMiddle();
                    sLink->size = MIDDLE_SIZE;
                }
                sSubtreeCardinality /= MIDDLE_SIZE;
                sLink = &sLink->child[sNextOffsetVector / sSubtreeCardinality];
                sNextOffsetVector = sNextOffsetVector % sSubtreeCardinality;
            }
            sLink->data = nullptr;
            sLink->data = T::Allocator::alloc();
            sLink->size = T::CHUNK_SIZE;
            sAllocatedEnd += T::CHUNK_SIZE;
        }
    } catch (...) {
        size_t sAllocatedEnd = (m_End + T::CHUNK_SIZE - 1) & ~(T::CHUNK_SIZE - 1);
        while (sAllocatedEnd < sNewEnd)
        {
            size_t sSubtreeCardinality = SUBTREE_CARDINALITY;
            Link* sLink = &m_Root[sAllocatedEnd / sSubtreeCardinality % T::L0_SIZE];
            size_t sNextOffsetVector = sAllocatedEnd % sSubtreeCardinality;
            for (size_t h = 0; h < T::HEIGHT - 2; h++)
            {
                if (0 == sNextOffsetVector)
                {
                    if (nullptr == sLink->child)
                        throw;
                    freeMiddle(sLink->child);
                }
                sSubtreeCardinality /= MIDDLE_SIZE;
                sLink = &sLink->child[sNextOffsetVector / sSubtreeCardinality];
                sNextOffsetVector = sNextOffsetVector % sSubtreeCardinality;
            }
            if (nullptr == sLink->child)
                throw;
            T::Allocator::free(sLink->data);
            sAllocatedEnd += T::CHUNK_SIZE;
        }
        // unreachable
        assert(false);
        throw;
    }

    size_t sOldEnd = m_End;
    m_End = sNewEnd;
    return sOldEnd;
}

template<class T>
void NetBuffer<T>::unalloc(size_t aSize)
{
    size_t sNewEnd = m_End - aSize;
    size_t sAllocatedEnd = (m_End + T::CHUNK_SIZE - 1) & ~(T::CHUNK_SIZE - 1);
    size_t sNewAllocatedEnd = (sNewEnd + T::CHUNK_SIZE - 1) & ~(T::CHUNK_SIZE - 1);
    while (sNewAllocatedEnd != sAllocatedEnd)
    {
        size_t sSubtreeCardinality = SUBTREE_CARDINALITY;
        Link* sLink = &m_Root[(sAllocatedEnd-1) / sSubtreeCardinality % T::L0_SIZE];
        size_t sNextOffsetVector = (sAllocatedEnd-1) % sSubtreeCardinality;
        for (size_t h = 0; h < T::HEIGHT - 2; h++)
        {
            if (0 == sNextOffsetVector)
            {
                if (nullptr == sLink->child)
                    throw;
                freeMiddle(sLink->child);
            }
            sSubtreeCardinality /= MIDDLE_SIZE;
            sLink = &sLink->child[sNextOffsetVector / sSubtreeCardinality];
            sNextOffsetVector = sNextOffsetVector % sSubtreeCardinality;
        }
        T::Allocator::free(sLink->data);
        sAllocatedEnd -= T::CHUNK_SIZE;
    }
    m_End = sNewEnd;
}

template<class T>
void NetBuffer<T>::free(size_t aPos, size_t aSize)
{
    do {
        size_t sSizeInBlock = std::min(aSize, T::CHUNK_SIZE - (aPos & (T::CHUNK_SIZE - 1)));

        Link* sPath[T::HEIGHT - 1];
        sPath[0] = &m_Root[root_offset(aPos)];
        for (size_t h = 1; h < T::HEIGHT - 1; h++)
            sPath[h] = &sPath[h - 1]->child[mid_offset(aPos, h)];

        sPath[T::HEIGHT - 1]->size -= sSizeInBlock; // Make interlocked for multithread
        size_t sLeft = sPath[T::HEIGHT - 1]->size; // Make interlocked for multithread
        if (0 == sLeft)
        {
            T::Allocator::free(sPath[T::HEIGHT - 1]->data);
        }
        /*
        for (size_t h = 0; 0 == sLeft && h < T::HEIGHT - 1; h++)
        {
            if (0 == sPath[T::HEIGHT - 1]->size)
        }
        if (0 == sLink->size)
        {
            T::Allocator::free(sLink->data);
        }
         */

        aPos += sSizeInBlock;
        aSize -= sSizeInBlock;
    } while (0 != aSize);
}

struct NetBufferTraitsTest : NetBufferTraitsBase
{
    static constexpr size_t CHUNK_SIZE = 128;
    typedef StupidAllocator<CHUNK_SIZE> Allocator;
};




struct Link
{
    Link* next;
};

struct LevelState
{
    Link* m_Level;
    size_t m_Pos;
    size_t m_PosMask; // const
    size_t m_OverShift; // const

    size_t m_Rise; // const
    size_t m_Falls; // const
    size_t m_OverFall;

    size_t m_AllInc; // const
};

template <class T>
struct AllState
{
    size_t m_Level;
    LevelState m_Levels[T::HEIGHT];

    AllState() : m_Level(0)
    {
        static Link tmp[T::L0_SIZE];

        m_Levels[0].m_Level = tmp;
        m_Levels[0].m_Pos = 0;
        m_Levels[0].m_PosMask = T::L0_SIZE - 1;
        m_Levels[0].m_OverShift = Log<T::L0_SIZE>::VALUE;

        m_Levels[0].m_Rise = 1;
        m_Levels[0].m_Falls = 0;
        m_Levels[0].m_OverFall = 0;

        m_Levels[0].m_AllInc = 0;

        for (size_t i = 1; i < T::HEIGHT - 1; i++)
        {
            m_Levels[i].m_Pos = 0;
            m_Levels[i].m_PosMask = T::CHUNK_SIZE / sizeof(Link) - 1;
            m_Levels[i].m_OverShift = Log<T::CHUNK_SIZE / sizeof(Link)>::VALUE;

            m_Levels[i].m_Rise = 1;
            m_Levels[i].m_Falls = 0;

            m_Levels[i].m_AllInc = 0;
        }

        const size_t i = T::HEIGHT - 2;
        m_Levels[i].m_Rise = 0;
        m_Levels[i].m_Falls = SIZE_MAX;
        m_Levels[i].m_AllInc = T::CHUNK_SIZE;

    }

    void step(size_t& aAllEnd)
    {
        //Link* m_New = static_cast<Link*>(T::Allocator::alloc());

        LevelState& sLevel = m_Levels[m_Level];
        LevelState& sNextLevel = m_Levels[m_Level + 1];

        aAllEnd += sLevel.m_AllInc;

        //sLevel.m_Level[sLevel.m_Pos].next = m_New;
        //sNextLevel.m_Level = m_New;

        ++sLevel.m_Pos;
        size_t sOver = sLevel.m_Pos >> sLevel.m_OverShift;
        sLevel.m_Pos &= sLevel.m_PosMask;
        sNextLevel.m_OverFall = 1 + sOver * sLevel.m_OverFall;

        m_Level += sLevel.m_Rise - ((sOver * sLevel.m_OverFall) & sLevel.m_Falls);

    }
};

struct Test
{
    static constexpr size_t L0_SIZE = 8;
    static constexpr size_t CHUNK_SIZE = 8 * 1024;
    static constexpr size_t HEIGHT = 3;
    typedef StupidAllocator<CHUNK_SIZE> Allocator;
};

int main()
{
    #if 1
    {
        alya::CTimer t;
        t.Start();
        AllState<Test> st;
        size_t side = 0;
        const size_t N = 10000000;
        size_t s = 0;
        for (size_t i = 0; i < N; i++)
        {
            size_t s_old = s;
            size_t steps = 0;
            while (s < s_old + 8 * Test::CHUNK_SIZE)
            {
                st.step(s);
                steps++;
            }
            side ^= s;
        }
        t.Stop();
        COUTF(t.Mrps(N), side);
        return 0;
    }
    #endif

    alya::CTimer t;
    NetBuffer<> n;
    t.Start();
    size_t r = 0;
    const size_t N = 1000000;
    for (size_t i = 2; i < N; i++)
    {
        r += n.alloc(64 * 1024);
        n.unalloc(64 * 1024 - 1);
    }
    t.Stop();
    COUTF(t.Mrps(N), r);


    return 0;
}