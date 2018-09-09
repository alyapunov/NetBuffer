#include <iostream>
#include <assert.h>
#include <vector>

#include <climits>
#include <LocalMemPool.hpp>
#include <unistd.h>

template<size_t SIZE> struct Log { static constexpr size_t VALUE = 1 + Log<SIZE / 2>::VALUE; };
template<> struct Log<1> { static constexpr size_t VALUE = 0; };

template<size_t BASE, size_t POWER> struct Pow { static constexpr size_t VALUE = BASE * Pow<BASE, POWER-1>::VALUE; };
template<size_t BASE> struct Pow<BASE, 0> { static constexpr size_t VALUE = 1; };

template <class T>
struct AllocProxy
{
    static char* alloc()
    {
        char* sRes = T::alloc();
        std::cout << "Alloc " << (void*)sRes << std::endl;
        sleep(1);
        return sRes;
    }
    static void free(char* aPtr)
    {
        std::cout << "Free  " << (void*)aPtr << std::endl;
        sleep(1);
        T::free(aPtr);
    }
};

struct NetBufferTraitsBase
{
    /* Size (in bytes) of dynamically allocated tree nodes */
    static constexpr size_t CHUNK_SIZE = 8192;
    /* Size (count) of top level node of the tree */
    static constexpr size_t L0_SIZE = 8;
    /* Height of the tree */
    static constexpr size_t HEIGHT = 3;
    /* Allocator for dynamically allocated tree nodes */
    typedef LocalMemPool<CHUNK_SIZE> Allocator;
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

    NetBuffer() : m_Alloc(T::Allocator::instance()), m_Begin(0), m_End(0) {}
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
    typename T::Allocator& m_Alloc;
    size_t m_Begin;
    size_t m_End;
    Link m_Root[T::L0_SIZE];

    Link* allocMiddle() { return reinterpret_cast<Link*>(m_Alloc.alloc()); }
    void freeMiddle(Link* aLink) { return m_Alloc.free(reinterpret_cast<char*>(aLink)); }
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
            sLink->data = m_Alloc.alloc();
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
            m_Alloc.free(sLink->data);
            sAllocatedEnd -= T::CHUNK_SIZE;
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
        sAllocatedEnd -= T::CHUNK_SIZE;
        size_t sSubtreeCardinality = SUBTREE_CARDINALITY;
        Link* sLink = m_Root[sAllocatedEnd / sSubtreeCardinality % T::L0_SIZE].child;
        size_t sNextOffsetVector = sAllocatedEnd % sSubtreeCardinality;
        for (size_t h = 0; h < T::HEIGHT - 2; h++)
        {
            Link* sSave = sLink;
            sSubtreeCardinality /= MIDDLE_SIZE;
            sLink = sLink[sNextOffsetVector / sSubtreeCardinality].child;
            if (0 == sNextOffsetVector)
                freeMiddle(sSave);
            sNextOffsetVector = sNextOffsetVector % sSubtreeCardinality;
        }
        freeMiddle(sLink);
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
            m_Alloc.free(sPath[T::HEIGHT - 1]->data);
        }
        /*
        for (size_t h = 0; 0 == sLeft && h < T::HEIGHT - 1; h++)
        {
            if (0 == sPath[T::HEIGHT - 1]->size)
        }
        if (0 == sLink->size)
        {
            m_Alloc.free(sLink->data);
        }
         */

        aPos += sSizeInBlock;
        aSize -= sSizeInBlock;
    } while (0 != aSize);
}

struct Link
{
    Link* next;
};

template <size_t MAX_BITS>
struct BSR
{
    static int get(unsigned long long aMask)
    {
        return (sizeof(aMask) * CHAR_BIT - 1) ^ __builtin_clzll(aMask);
    }
};

template <>
struct BSR<0>
{
    static int get(unsigned long long)
    {
        return 0;
    }
};

template <>
struct BSR<1>
{
    static int get(unsigned long long)
    {
        return 0;
    }
};

template <>
struct BSR<2>
{
    static int get(unsigned long long aMask)
    {
        return aMask >> 1;
    }
};

template <class T>
struct AllState
{
    static Link* buf()
    {
        static Link tmp[T::CHUNK_SIZE / sizeof(Link)];
        return tmp;
    }

    struct LevelState
    {
        Link* m_Level = nullptr;
        size_t m_Pos = 0;
    };

    unsigned long long m_Mask = 1;
    LevelState m_Levels[T::HEIGHT - 1];

    AllState()
    {
        m_Levels[0].m_Level = buf();
    }

    void step(size_t& aAllEnd)
    {
        //Link* sNew = new(T::Allocator::alloc()) Link[T::CHUNK_SIZE / sizeof(Link)];
        Link* sNew = buf();

        const unsigned long long sLastMask = 1ull << (T::HEIGHT - 2);
        if (__builtin_expect(m_Mask & sLastMask, 1))
        {
            aAllEnd += T::CHUNK_SIZE;
            const int i = T::HEIGHT - 2;
            //COUTF(i, m_Levels[i].m_Pos);
            LevelState& sLevel = m_Levels[i];
            sLevel.m_Level[sLevel.m_Pos].next = sNew;
            ++sLevel.m_Pos;
            if (i == 0)
            {
                sLevel.m_Pos &= T::L0_SIZE - 1;
            }
            else
            {
                unsigned long long sOver = sLevel.m_Pos >> (Log<T::CHUNK_SIZE / sizeof(Link)>::VALUE);
                sLevel.m_Pos &= T::CHUNK_SIZE / sizeof(Link) - 1;
                m_Mask ^= sOver << i;
            }
            return;
        }

        int i = BSR<T::HEIGHT - 2>::get(m_Mask);
        //COUTF(i, m_Levels[i].m_Pos);

        LevelState& sLevel = m_Levels[i];
        LevelState& sNextLevel = m_Levels[i + 1];

        sLevel.m_Level[sLevel.m_Pos].next = sNew;
        sNextLevel.m_Level = sNew;

        ++sLevel.m_Pos;
        if (__builtin_expect(i == 0, 0))
        {
            sLevel.m_Pos &= T::L0_SIZE - 1;
            m_Mask |= 2;
        }
        else
        {
            unsigned long long sOver = sLevel.m_Pos >> (Log<T::CHUNK_SIZE / sizeof(Link)>::VALUE);
            sLevel.m_Pos &= T::CHUNK_SIZE / sizeof(Link) - 1;
            m_Mask ^= (2 | sOver) << i;
        }
    }
};

struct Test
{
    /* Size (in bytes) of dynamically allocated tree nodes */
    static constexpr size_t CHUNK_SIZE = 128;
    /* Size (count) of top level node of the tree */
    static constexpr size_t L0_SIZE = 4;
    /* Height of the tree */
    static constexpr size_t HEIGHT = 6;
    /* Allocator for dynamically allocated tree nodes */
    typedef LocalMemPool<CHUNK_SIZE> Allocator;
};

size_t side_effect = 0;

static void checkpoint(const char* aText, size_t aOpCount)
{
    using namespace std::chrono;
    high_resolution_clock::time_point now = high_resolution_clock::now();
    static high_resolution_clock::time_point was;
    duration<double> time_span = duration_cast<duration<double>>(now - was);
    if (0 != aOpCount)
    {
        double Mrps = aOpCount / 1000000. / time_span.count();
        std::cout << aText << ": " << Mrps << " Mrps" << std::endl;
    }
    was = now;
}
int main(int, const char**)
{
    checkpoint("", 0);

    {
        NetBuffer<> n;
        const size_t N = 20000000;
        for (size_t i = 2; i < N; i++)
        {
            side_effect += n.alloc(8 * NetBufferTraitsBase::CHUNK_SIZE);
            n.unalloc(8 * NetBufferTraitsBase::CHUNK_SIZE - 1);
        }
        checkpoint("H=3 alloc 8 blocks / free 8 blocks - 1", N);
    }

    {
        NetBuffer<> n;
        const size_t N = 20000000;
        for (size_t i = 2; i < N; i++)
        {
            side_effect += n.alloc(8 * Test::CHUNK_SIZE);
            n.unalloc(8 * Test::CHUNK_SIZE);
        }
        checkpoint("H=3 alloc 8 blocks / free 8 blocks", N);
    }

    {
        NetBuffer<Test> n;
        const size_t N = 2000000;
        for (size_t i = 2; i < N; i++)
        {
            side_effect += n.alloc(8 * Test::CHUNK_SIZE);
            n.unalloc(8 * Test::CHUNK_SIZE - 1);
        }
        checkpoint("H=6 alloc 8 blocks / free 8 blocks - 1", N);
    }

    {
        NetBuffer<Test> n;
        const size_t N = 20000;
        for (size_t i = 2; i < N; i++)
        {
            side_effect += n.alloc(8 * NetBufferTraitsBase::CHUNK_SIZE);
            n.unalloc(8 * NetBufferTraitsBase::CHUNK_SIZE);
        }
        checkpoint("H=6 alloc 8 blocks / free 8 blocks", N);
    }

    std::cout << "side_effect=" << side_effect << std::endl;
}