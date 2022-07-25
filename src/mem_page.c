#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "link.h"
#include "mem_page.h"

/*===========================================================================*/

#if defined(WIN32)
#include <windows.h>
#else  /* Linux */
#include <pthread.h>
#endif /* WIN32 & Linux */

/* 获取线程 ID */
#if defined(WIN32)
#define THREAD_SELF GetCurrentThreadId()
#else /* Linux */
#define THREAD_SELF pthread_self()
#endif /* WIN32 & Linux */

/*===========================================================================*/

#if defined(WIN32)
typedef uintptr_t MEM_UINTPTR;
#else  /* Linux */
typedef unsigned long MEM_UINTPTR;
#endif /* WIN32 & Linux */

#if defined(WIN32)
#define CH_SEP  '\\'
#else /* Linux */
#define CH_SEP  '/'
#endif /* WIN32 & Linux */

/*===========================================================================*/

/* 初始化内存块填充值 */
#define INIT_BLOCK_PADDING 0x00

/* 数据对齐 */
#define DATA_ALIGN(size, align) \
    (((size) + (align) - 1) & (~((align) - 1)))

/* 当前最低对齐字节数 */
#define BYTE_ALIGN_COUNT 8

/* 64 位系统上采用 8 字节对齐的方式 */
#define INT_ALIGN(size) DATA_ALIGN(size, BYTE_ALIGN_COUNT)

/* 类型寻址, 计算完成后转为 type */
#define TYPE_OFFSET(type, ptr, offset) \
    ((type)((type)(ptr) + (MEM_UINTPTR)(offset)))

/* 反向寻址 */
#define TYPE_REOFFSET(type, ptr, offset) \
    ((type)((type)(ptr) - (MEM_UINTPTR)(offset)))

/* 字节偏移 */
#define BYTE_OFFSET(ptr, offset) TYPE_OFFSET(unsigned char *, ptr, offset)

/* 反向字节偏移 */
#define BYTE_REOFFSET(ptr, offset) TYPE_REOFFSET(unsigned char *, ptr, offset)

/* 取保存在内存中的地址 */
#define MEM_TO_ADDR(ptr) \
    ((unsigned char *)((MEM_UINTPTR)(*((unsigned long long *)(ptr)))))

/* 将地址 addr 保存到不小于 8 byte 的内存中, 保存区域首地址为 ptr */
#define ADDR_TO_MEM(ptr, addr) \
    *((unsigned long long *)(ptr)) = (unsigned long long)(MEM_UINTPTR)(addr)

/*===========================================================================*/

/*
 * 内存块和内存页
 *
 * 一个内存页除了包含头部数据，还包含若干个内存块，假设内存页的规格为
 * 1k，每个内存块管理 8 byte 的内存，那么这个内存页的结构为：
 *
 * -- MEM_PAGE_HEADER --
 *      0   MEM_BLOCK
 *      1   MEM_BLOCK
 *      2   MEM_BLOCK
 *      3   MEM_BLOCK
 *            ...
 *      254 MEM_BLOCK
 *      255 MEM_BLOCK
 * ----------------------
 *
 * 本结构继承自 LINK_NODE，见 link.h - LINK_NODE。
 *
 * 内存页面的管理遵循以下方式：
 *
 * 1.多个相同规格的内存页组成一个内存页链表，多个内存页链表组成一张
 * 内存页映射表，结构如下所示：
 * 
 * MEM_PAGE_LINK1 --- PAGE0 -- PAGE1 -- PAGE2 -- ... -- TAIL
 *        |
 * MEM_PAGE_LINK2 --- PAGE0 -- PAGE1 -- PAGE2 -- ... -- TAIL
 *        |
 * MEM_PAGE_LINK3 --- PAGE0 -- PAGE1 -- PAGE2 -- ... -- TAIL
 *        |
 *        .
 *        .
 *        |
 * MEM_PAGE_LINKn --- PAGE0 -- PAGE1 -- PAGE2 -- ... -- TAIL
 *
 * 2.内存页链表的检索方式：
 *        主要通过内存页信息索引表 mem_page_info_index 来快速获取映射表
 * 索引，工作流程如下：
 *        2.1 首先，对于内存页管理的内存块的大小，全部设计为 8 的整数
 * 倍，即：size = x * 8, x 的的值为(0, 1, 2, …… 128)；
 *        2.2 创建一个 “内存信息索引表”，将 x（倍数）定义为该表索引，
 * 即申请的内存大小为 8 字节，对应的索引为 1，申请的内存大小为 16 字
 * 节，则索引为 2，倍数最高为 128，代表申请内存为 1024 字节； 同时，
 * 我在确定索引之前，会首先将用户申请的内存容量转换为 8 的整数倍确保
 * 可以精确获取索引值，公式如下：
 *        x = ALIGN(size) >> 3；
 *        2.3 拿到 x 之后，直接在 “内存信息索引表” 中查询，查到的值为已
 * 经设计好的 “内存块分类信息表” 的索引，通过该索引可以获取到对应的 
 * x 的内存页信息；
 *        2.4 如果用户申请的内存大于 512，我这里暂时没有想出可以一步到
 * 位的算法，所以通过一个判断来直接定位到 “内存块分类信息表” 的最后
 * 一个元素，详见 get_page_index 函数实现；
 *        2.5 在调整内存分页情况时应直接重新设计表结构，无需更改代码；
 *
 * 3.内存页链表的管理方式：
 *        链表的的尾指针总是指向填满数据（也就是 status = MEM_PAGE_STATUS_FULL）
 * 的内存页，头指针则指向最近操作的内存块所属的内存页，新申请的内存页
 * 节点优先放在当前链表的第二个节点，除非当前链表的头结点页处于 
 * MEM_PAGE_STATUS_FULL 状态或者当前链表没有任何内存页， 这样的话保证在
 * 申请内存时可以以 O(1) 的时间取到对应的内存页；同时，当释放内存时，
 * 如果处于空闲内存状态的内存页多于一定数量时，将之释放交还给系统以
 * 节约内存；
 *
 * 4.内存页的管理方式：
 *        初始化内存页时，每个内存块会记录下一个空闲内存块的地址（通过
 * 将地址直接写入数据区的方式），内存页会记录第一个可用内存块；释放
 * 内存块时，内存页会记录当前释放的内存块的首地址，同时将先前记录的
 * 空闲内存块的地址写入这个已释放的内存块的数据区（相当于一个链表节
 * 点的插入），这样保证下一次申请内存的时候可以以 O(1) 的时间快速定
 * 位到该内存块。
 */
struct mem_page_st {
    MEM_PAGE *prev;             /* 上一页 */
    MEM_PAGE *next;             /* 下一页 */

    unsigned char type;         /* 内存页类型 */
    unsigned char status;       /* 内存页状态 */
    unsigned char using_count;  /* 已分配的内存块数量 */
    unsigned char block_num;    /* 当前内存页内存块数量 */
    int block_head;             /* 单位内存块头部大小 */
    int block_data;             /* 单位内存块数据大小 */
    int alloc_size;             /* 当前申请的数据空间大小 */

    MEM_BLOCK *idle;            /* 当前空闲的内存块地址 */
    MEM_PAGE *head_addr;        /* 内存页的头部地址 */
};

/* 内存块 */
struct mem_block_st {
    MEM_PAGE *page;             /* 所属 page */
    int status;                 /* 内存块状态 */
};

#define DATE_INFO_LENGTH 32
#define FILE_INFO_LENGTH 64
#define FUNC_INFO_LENGTH 64

/* 调试内存块 */
struct mem_block_dbg_st {
    MEM_PAGE *page;             /* 内存页的地址 */
    int status;                 /* 内存块状态 */
    int line;                   /* 调用 malloc 的行数 */
    unsigned long long thread;  /* 调用 malloc 的线程 */

    char date[DATE_INFO_LENGTH]; /* 调用时间，格式为 yyyy-mm-dd hh:MM:ss */
    char file[FILE_INFO_LENGTH]; /* 所属文件 */
    char func[FUNC_INFO_LENGTH]; /* 所属函数 */
};

/* 内存页链表，继承自 LINK */
struct mem_page_link_st {
    MEM_PAGE *head; /* 表头 */
    MEM_PAGE *tail; /* 表尾 */

    int count;      /* 节点总数 */
    int idle_num;   /* 有空闲内存块的节点总数 */
};

/* 内存页信息 */
typedef struct {
    int page_type;  /* 内存页类型 */
    int block_size; /* 单位内存块尺寸 */
    int total_size; /* 所有内存块的大小之和 */
    int block_num;  /* 每一页内存块的数量 */
} MEM_PAGE_INFO;

/*===========================================================================*/

#define MEM_PAGE_BLOCK_INFO_COUNT 15    /* 内存页信息表数量 */
#define MEM_PAGE_MAP_INDEX_COUNT 65     /* 内存页映射表索引数量，不包括 0 和 大内存 */
#define MEM_PAGE_MIN_BLOCK 0            /* 内存页可复用的最小内存块申请大小 */
#define MEM_PAGE_MAX_BLOCK 512          /* 内存页可复用的最大内存块申请大小 */
#define MEM_PAGE_MAX_IDLE 2             /* 每个链表最大空闲页数量 */

/*===========================================================================*/

/*
 * 内存页信息索引表，用于快速查找对应的内存块分类信息，这个表的
 * 索引为：申请长度（对齐之后） / 8 ，值为 mem_page_info_list 表的
 * 索引。
 */
static const unsigned char mem_page_info_index[MEM_PAGE_MAP_INDEX_COUNT] = {
     0,
     1,  2,  3,  3,  4,  4,  4,  4,    /*   1 ~ 8   */
     5,  5,  5,  5,  6,  6,  6,  6,    /*   9 ~ 16  */    
     7,  7,  7,  7,  8,  8,  8,  8,    /*  17 ~ 24  */
     9,  9,  9,  9,  9,  9,  9,  9,    /*  25 ~ 32  */
    10, 10, 10, 10, 10, 10, 10, 10,    /*  33 ~ 40  */
    11, 11, 11, 11, 11, 11, 11, 11,    /*  41 ~ 48  */
    12, 12, 12, 12, 12, 12, 12, 12,    /*  49 ~ 56  */
    13, 13, 13, 13, 13, 13, 13, 13    /*  57 ~ 64  */
};

/* 内存页分类信息表 */
static const MEM_PAGE_INFO mem_page_info_list[MEM_PAGE_BLOCK_INFO_COUNT] = {
    /* 内存页类型        单位内存块尺寸        内存块总大小    内存块数量*/
    { MEM_PAGE_TYPE_ZERO,        8,                   8,              1    },    /* 0  */
    { MEM_PAGE_TYPE_1K,          8,                1024,            128    },    /* 1  */
    { MEM_PAGE_TYPE_1K,         16,                1024,             64    },    /* 2  */
    { MEM_PAGE_TYPE_1K,         32,                1024,             32    },    /* 3  */
    { MEM_PAGE_TYPE_1K,         64,                1024,             16    },    /* 4  */
    { MEM_PAGE_TYPE_1K,         96,                1024,             10    },    /* 5  */
    { MEM_PAGE_TYPE_1K,        128,                1024,              8    },    /* 6  */
    { MEM_PAGE_TYPE_2K,        160,                2048,             12    },    /* 7  */
    { MEM_PAGE_TYPE_2K,        192,                2048,             10    },    /* 8  */
    { MEM_PAGE_TYPE_2K,        256,                2048,              8    },    /* 9  */
    { MEM_PAGE_TYPE_4K,        320,                4096,             12    },    /* 10 */
    { MEM_PAGE_TYPE_4K,        384,                4096,             10    },    /* 11 */
    { MEM_PAGE_TYPE_4K,        448,                4096,              9    },    /* 12 */
    { MEM_PAGE_TYPE_4K,        512,                4096,              8    },    /* 13 */
    { MEM_PAGE_TYPE_LARGE,      8,                   8,               1    },    /* 14 */
};

/* 内存页映射表 */
static MEM_PAGE_LINK mem_page_map[MEM_PAGE_BLOCK_INFO_COUNT] = { { 0 } };

/*===========================================================================*/

/* 初始化内存页 */
static void mem_page_initialize(int index, MEM_PAGE *page, int dbg);
static void mem_page_terminate(MEM_PAGE *page);

/* 获取内存块 */
static MEM_BLOCK *get_block(void *address, int dbg);

/* 填充 dbg 内存块 */
static void pad_dbg_block(
    MEM_BLOCK_DBG *block, const char *func, const char *file, int line);

/* 获取内存页名称, 用于打印信息 */
static const char *get_page_name(unsigned char type);
static const char *get_status_name(unsigned char status);
static const char *get_block_status_name(int status);

/* 打印泄漏信息 */
static int print_leak_info(MEM_PAGE *page, int dbg, char *buff);

/* 打印链表信息 */
static void print_link_info(
    MEM_PAGE_LINK *link, int index, char *buff);

/* 打印内存页信息 */
static void print_page_info(MEM_PAGE *page, char *buff);

/* 输出内存信息 */
static void output_mem_info_std(const char *info);

/* 获取当前格式化时间 */
int get_curtime(const char *format, char *buf, int len);

/*===========================================================================*/

int get_page_index(size_t len)
{
    /* 信息索引表索引 */
    int index = 0;

    if (len > MEM_PAGE_MAX_BLOCK) {
        return MEM_PAGE_BLOCK_INFO_COUNT - 1;
    }

    index = ((int)INT_ALIGN(len) >> 3);

    /* 根据内存字节数获取内存信息表索引 */
    return mem_page_info_index[index];
}

int get_page_index_ex(MEM_PAGE *page)
{
    int index = 0;

    /* 内存页为 0 内存类型或者大内存类型，index 手动指定 */
    if (!page || page->type == MEM_PAGE_TYPE_ZERO) {
        index = 0;
    } else if (page->type == MEM_PAGE_TYPE_LARGE) {
        index = MEM_PAGE_BLOCK_INFO_COUNT - 1;
    } else {
        index = get_page_index(page->block_data);
    }

    return index;
}

int usable_page_exist(int index)
{
    if (index > MEM_PAGE_BLOCK_INFO_COUNT - 1) {
        return 0;
    }

    if (!mem_page_map[index].count || 
        !mem_page_map[index].head) {
        return 0;
    }

    if ((mem_page_map[index].head)->status == MEM_PAGE_STATUS_FULL) {
        return 0;
    }

    return 1;
}

int mem_page_malloc(int index, int dbg)
{
    int ret = MEM_SUCCESS;
    int page_size = 0;
    int block_size = 0;

    MEM_PAGE_LINK *link = NULL;
    MEM_PAGE *idle_page = NULL;

    if (index > MEM_PAGE_BLOCK_INFO_COUNT - 1) {
        return MEM_FAILED;
    }

    block_size = dbg ? sizeof(MEM_BLOCK_DBG) : sizeof(MEM_BLOCK);

    /* 获取对应的内存页链表 */
    link = mem_page_map + index;

    /* 内存页大小 = 内存块总大小 + 每个内存块头部大小 + 内存页头部大小 */
    page_size = 
        mem_page_info_list[index].total_size + 
        block_size * mem_page_info_list[index].block_num +
        sizeof(MEM_PAGE);

    /* 创建内存页 */
    idle_page = (MEM_PAGE *)malloc(page_size);
    assert(idle_page);

    memset(idle_page, 0, page_size);
    mem_page_initialize(index, idle_page, dbg);

    /* 
     * 将新创建的内存页链接到头结点之后的位置，如果链表没有节点，
     * 或者节点状态均为 FULL，则以新节点作为链表头结点。
     */
    if (!link->head || link->head->status == MEM_PAGE_STATUS_FULL) {
        ret = link_insert((LINK *)link, 0, (LINK_NODE *)idle_page);
    } else {
        ret = link_insert_after(
            (LINK *)link, (LINK_NODE *)link->head, (LINK_NODE *)idle_page);
    }

    if (ret == MEM_SUCCESS) {
        link->idle_num++;
    }

    return ret;
}

int mem_page_free(MEM_PAGE *page)
{
    int index = 0;
    MEM_PAGE_LINK *link = NULL;

    if (!page) {
        return MEM_FAILED;
    }

    index = get_page_index_ex(page);
    if (index > MEM_PAGE_BLOCK_INFO_COUNT - 1) {
        return MEM_FAILED;
    }

    link = mem_page_map + index;
    link_remove_force(
            (LINK *)link, (LINK_NODE *)page);

    if (page->status == MEM_PAGE_STATUS_IDLE) {
        link->idle_num--;
    }

    mem_page_terminate(page);
    free(page);

    return MEM_SUCCESS;
}

void clear_mem_pages()
{
    int i;

    MEM_PAGE_LINK *link = NULL;
    unsigned char *tmp  = NULL;

    for (i = 0; i < MEM_PAGE_BLOCK_INFO_COUNT; i++) {
        link = mem_page_map + i;

        while (link->count > 0) {
            if (link->head->type == MEM_PAGE_TYPE_ZERO ||
                link->head->type == MEM_PAGE_TYPE_LARGE) {
                /* 指针偏移至内存块的数据区 */
                tmp = BYTE_OFFSET(link->head, sizeof(MEM_PAGE) + link->head->block_head);

                /* 获取大内存或 0 内存的地址 */
                tmp = MEM_TO_ADDR(tmp);
                if (tmp) {
                    tmp = BYTE_OFFSET(tmp, link->head->block_head);
                    free_block(tmp, link->head->block_head != sizeof(MEM_BLOCK));
                } 
            }

            mem_page_free(link->head);
        }

        link->idle_num = 0;
        link_reset((LINK *)link);
    }
}

void *alloc_block(size_t len)
{
    size_t size = 0;
    int index = 0;

    unsigned char *ret  = NULL;
    MEM_PAGE *page = NULL;
    MEM_PAGE_LINK *link = NULL;

    MEM_BLOCK_DBG *block_dbg  = NULL;
    MEM_BLOCK *block = NULL;

    index = get_page_index(len);
    if (index > MEM_PAGE_BLOCK_INFO_COUNT - 1) {
        return NULL;
    }

    link = mem_page_map + index;

    if (!link->count || !link->head) {
        return NULL;
    }

    page = link->head;
    if (page->status == MEM_PAGE_STATUS_FULL) {
        return NULL;
    }

    /*
     * 如果当前页可用内存块达到上限，则将当前的内存页
     * 调整至链表表尾
     */
    if (page->using_count == (page->block_num - 1)) {
        if (link->count > 1 && link->tail != page) {
            link_remove_force((LINK *)link, (LINK_NODE *)page);
            link_push((LINK *)link, (LINK_NODE *)page);
        }

        page->status = MEM_PAGE_STATUS_FULL;
    }

    if (!page->using_count) {
        link->idle_num--;
        if (page->block_num > 1) {
            page->status = MEM_PAGE_STATUS_USING;
        }
    }

    page->using_count++;
    page->alloc_size += (page->block_data + page->block_head);

    /* 定位到空闲内存块 */
    ret = (unsigned char *)page->idle;
    if (!ret) {
        return NULL;
    }

    /* 修改内存块的状态 */
    block = (MEM_BLOCK *)ret;
    block->status = MEM_BLOCK_STATUS_USING;

    /* 定位到数据区位置 */
    ret = BYTE_OFFSET(ret, page->block_head);

    /*
     * 获取下一个空闲内存块地址并保存：
     * 内存页填满时，不做处理。
     */
    if (page->status != MEM_PAGE_STATUS_FULL) {
        page->idle = (MEM_BLOCK *)MEM_TO_ADDR(ret);
    } else {
        page->idle = NULL;
    }

    /* 
     * 对于 0 内存和大内存的处理方式：
     *
     * 直接分配一个带着头的内存块，将分配的内存首地址保存
     * 在内存页的 8 字节的内存块中。
     */
    if (page->type == MEM_PAGE_TYPE_ZERO ||
        page->type == MEM_PAGE_TYPE_LARGE) {
        size = page->block_head + len;
        block = (MEM_BLOCK *)malloc(size);
        assert(block);

        block->page = page;
        block->status = MEM_BLOCK_STATUS_USING;

        /* 填充 debug 内存块 */
        if (page->block_head == sizeof(MEM_BLOCK_DBG)) {
            block_dbg = (MEM_BLOCK_DBG *)block;
            pad_dbg_block(block_dbg, __FUNCTION__, __FILE__, __LINE__);
        }

        /* 将新分配的内存地址保存到内存页的 8 字节内存块中 */
        ADDR_TO_MEM(ret, block);

        /* 更新内存页信息 */
        page->alloc_size += ((int)size);

        /* 获取大内存数据区地址 */
        ret = (unsigned char *)block;
        ret = BYTE_OFFSET(ret, page->block_head);

        /* 初始化空闲内存块 */
        memset(ret, INIT_BLOCK_PADDING, len);
    } else {
        /* 初始化空闲内存块 */
        memset(ret, INIT_BLOCK_PADDING, (size_t)page->block_data);
    }

    return ret;
}

void *alloc_block_dbg(size_t len, const char *func, const char *file, int line)
{
    MEM_PAGE *page  = NULL;
    MEM_BLOCK_DBG *block = NULL;
    unsigned char *ret = alloc_block(len);

    if (!ret) {
        return NULL;
    }

    block = (MEM_BLOCK_DBG *)get_block(ret, 1);
    assert(block);

    page = block->page;
    assert(page == page->head_addr);

    if (page->type == MEM_PAGE_TYPE_ZERO ||
        page->type == MEM_PAGE_TYPE_LARGE) {
        block = (MEM_BLOCK_DBG *)BYTE_OFFSET(page, sizeof(MEM_PAGE));
    }

    pad_dbg_block(block, func, file, line);
    return ret;
}

void free_block(void *address, int dbg)
{
    int index = 0;

    MEM_UINTPTR idle = 0;
    MEM_PAGE *page = NULL;
    unsigned char *cursor = NULL;
    MEM_PAGE_LINK *link = NULL;

    MEM_BLOCK_DBG *block_dbg = NULL;
    MEM_BLOCK *block = NULL;

    if (!address) {
        return;
    }

    block = get_block(address, dbg);
    assert(block);

    cursor = (unsigned char *)block;
    page   = block->page;
    assert(page->head_addr == page);

    if (!page->using_count || !page->alloc_size) {
        return;
    }

    index = get_page_index_ex(page);
    if (index > MEM_PAGE_BLOCK_INFO_COUNT - 1) {
        return;
    }

    link = mem_page_map + index;

    /* 大内存或者 0 内存直接释放内存，同时覆写该内存页的内存块内容 */
    if (page->type == MEM_PAGE_TYPE_ZERO ||
        page->type == MEM_PAGE_TYPE_LARGE) {
        free(cursor);

        /* 重新定位 block 位置 */
        block  = (MEM_BLOCK *)BYTE_OFFSET(page, sizeof(MEM_PAGE));
        cursor = (unsigned char *)block;

        /* 这种情况下，由于一个内存页只带有一个内存块，所以可以直接赋 0 */
        page->alloc_size = 0;

        /* 指针偏移至数据区 */
        cursor = BYTE_OFFSET(cursor, page->block_head);

        /* 覆写用户内存区域 */
        memset(cursor, INIT_BLOCK_PADDING, (size_t)page->block_data);
    } else {
        /* 指针偏移至数据区 */
        cursor = BYTE_OFFSET(cursor, page->block_head);

        /* 覆写用户内存区域 */
        memset(cursor, INIT_BLOCK_PADDING, (size_t)page->block_data);

        /* 
         * 将下一个空闲位置记录在当前内存块的内存区域，然后将
         * 当前内存块作为空闲块复用, 如果内存页已满，不做处理
         */
        if (page->status != MEM_PAGE_STATUS_FULL) {
            idle = (MEM_UINTPTR)page->idle;
            ADDR_TO_MEM(cursor, idle);
        }

        page->alloc_size -= (page->block_data + page->block_head);        
    }

    /* 还原内存块状态 */
    block->status = MEM_BLOCK_STATUS_IDLE;

    /* dbg 模式还原内存块头部信息区域 */
    if (dbg) {
        block_dbg = (MEM_BLOCK_DBG *)block;
        pad_dbg_block(block_dbg, NULL, NULL, 0);
    }

    /* 更新内存页信息 */
    page->idle = block;
    page->using_count--;

    /*
     * 不论当前内存页是什么状态，直接将当前内存页换到
     * 内存链表的头结点。
     */
    if (link->count > 1 && link->head != page) {
        link_remove_force((LINK *)link, (LINK_NODE *)page);
        link_insert((LINK *)link, 0, (LINK_NODE *)page);
    }

    /* 更改内存页的状态 */
    if (page->using_count == (page->block_num - 1)) {
        page->status = MEM_PAGE_STATUS_USING;
    }

    if (!page->using_count) {
        link->idle_num++;
        page->status = MEM_PAGE_STATUS_IDLE;

        /* 超过一个空闲页面则释放本页面以节省空间 */
        if (link->idle_num > MEM_PAGE_MAX_IDLE) {
            mem_page_free(page);
        }
    }
}

void page_print_basic_info(int dbg)
{
    int i;
    int j;

    char buff[128] = { 0 };

    MEM_PAGE_LINK *link = NULL;
    MEM_PAGE *page = NULL;

    output_mem_info_std("<============================basic check============================>\n");

    for (i = 0; i < MEM_PAGE_BLOCK_INFO_COUNT; i++) {
        link = mem_page_map + i;

        if(link->count > 0) {
            sprintf(buff, "<----------------------link %02d---------------------->\n", i);
            output_mem_info_std(buff);

            /* 打印链表信息 */
            print_link_info(link, i, buff);
            page = link->head;

            for (j = 0; j < link->count; j++) {
                if (!page) {
                    output_mem_info_std("page = null!!!\n");
                    break;
                }

                /* 打印内存页信息 */
                output_mem_info_std("---------------------- page -----------------------\n");
                print_page_info(page, buff);

                /* 打印内存泄漏信息 */
                if (page->using_count > 0) {
                    print_leak_info(page, dbg, buff);
                }
                page = page->next;
            }

            page = NULL;
            sprintf(buff, "<----------------------link %02d---------------------->\n", i);
            output_mem_info_std(buff);
        }
    }

    output_mem_info_std("<============================basic check============================>\n");
}

void page_print_block_list(int index, int dbg)
{
    int i;
    int j;

    char buff[128] = { 0 };

    MEM_PAGE_LINK *link = NULL;
    MEM_PAGE *page = NULL;
    MEM_BLOCK *block = NULL;
    unsigned char *cursor = NULL;

    int size = 0;

    if (index > MEM_PAGE_BLOCK_INFO_COUNT - 1) {
        return;
    }

    link = mem_page_map + index;
    page = link->head;

    /* 链表信息 */
    sprintf(buff, "<----------------------link %02d---------------------->\n", index);
    output_mem_info_std(buff);

    print_link_info(link, index, buff);

    for (i = 0; i < link->count; i++) {
        if (!page) {
            output_mem_info_std("page = null!!!\n");
            break;
        }

        output_mem_info_std("---------------------- page -----------------------\n");
        print_page_info(page, buff);

        block = (MEM_BLOCK *)BYTE_OFFSET(page, sizeof(MEM_PAGE));

        output_mem_info_std("---------------------- block -----------------------\n");

        for (j = 0; j < page->block_num; j++) {
            sprintf(buff, "(%d) [%p] -- status = %s size = %d\n", 
                j, block, get_block_status_name(block->status), page->block_data);
            output_mem_info_std(buff);

            if (page->type == MEM_PAGE_TYPE_ZERO ||
                page->type == MEM_PAGE_TYPE_LARGE) {
                if (page->alloc_size) {
                    size = 
                        page->alloc_size -
                        page->block_head -
                        page->block_head -
                        page->block_data;

                    cursor = BYTE_OFFSET(block, page->block_head);
                    block = (MEM_BLOCK *)MEM_TO_ADDR(cursor);

                    sprintf(buff, "(%d) [%p] -- status = %s size = %d\n", 
                        j + 1, block, get_block_status_name(block->status), size);
                    output_mem_info_std(buff);
                }
            } else {
                block = (MEM_BLOCK *)BYTE_OFFSET(block, page->block_head + page->block_data);
            }
        }

        output_mem_info_std("---------------------- block -----------------------\n");
        page = page->next;
    }

    sprintf(buff, "<----------------------link %02d---------------------->\n", index);
    output_mem_info_std(buff);
}

void page_print_allocated_info(int dbg)
{
    int i;
    int j;
    int size = 0;

    char buff[128] = { 0 };

    MEM_PAGE_LINK *link = NULL;
    MEM_PAGE *page = NULL;

    output_mem_info_std("<============================alloc check============================>\n");

    /* 遍历内存链表 */
    for (i = 0; i < MEM_PAGE_BLOCK_INFO_COUNT; i++) {
        link = mem_page_map + i;

        if(link->count > 0) {
            page = link->head;

            for (j = 0; j < link->count; j++) {
                if (!page) {
                    break;
                }

                /* 打印内存泄漏信息 */
                if (page->using_count > 0) {
                    /* 打印内存页信息 */
                    print_page_info(page, buff);
                    size += print_leak_info(page, dbg, buff);
                }

                page = page->next;
            }

            page = NULL;
        }
    }

    if (!size) {
        output_mem_info_std("No leak!\n");
    }

    output_mem_info_std("<============================alloc check============================>\n");
}

int get_addr_block_len(void *ptr, int dbg)
{
    MEM_BLOCK *block = NULL;
    MEM_PAGE *page = NULL;
    int ret = 0;

    if (!ptr) {
        return 0;
    }
    
    block = get_block(ptr, dbg);
    if (!block) {
        return 0;
    }

    page = block->page;
    assert(page->head_addr == page);

    /* 
     * 0 内存和大内存的内存块返回实际申请的内存块大小,
     * 等于总的申请大小 - 内存块头部大小
     */
    if (page->type == MEM_PAGE_TYPE_ZERO ||
        page->type == MEM_PAGE_TYPE_LARGE) {
        ret = 
            page->alloc_size -
            page->block_head -
            page->block_head -
            page->block_data;
    } else {
        ret = page->block_data;
    }

    return ret;
}

/*===========================================================================*/

void mem_page_initialize(int index, MEM_PAGE *page, int dbg)
{
    MEM_PAGE *head = NULL;
    MEM_BLOCK *block = NULL;
    unsigned char *cursor = NULL;

    int block_size = dbg ? sizeof(MEM_BLOCK_DBG) : sizeof(MEM_BLOCK);
    int page_size = sizeof(MEM_PAGE);
    int block_offset = 0;

    unsigned char i;

    if ((index > MEM_PAGE_BLOCK_INFO_COUNT - 1) || !page) {
        return;
    }

    head = page;

    head->prev = NULL;
    head->next = NULL;

    head->type = mem_page_info_list[index].page_type;
    head->status = MEM_PAGE_STATUS_IDLE;
    head->using_count = 0;
    head->block_num = mem_page_info_list[index].block_num;
    head->block_head = block_size;
    head->block_data = mem_page_info_list[index].block_size;
    head->alloc_size = 0;
    head->idle = (MEM_BLOCK *)BYTE_OFFSET(head, page_size);
    head->head_addr = head;

    cursor = BYTE_OFFSET(head, page_size);
    block_offset = head->block_head + head->block_data;

    /* 
     * 设置前 n - 1 个内存块的偏移值，同时将每个内存块的前 8 个字节
     * 填充为下一个内存块的首地址，最后一个内存块不做填充, 所有的地
     * 址均用 64 位整数保存。
     */
    for (i = 0; i < head->block_num - 1; i++) {
        block = (MEM_BLOCK *)cursor;
        block->page = page;
        block->status = MEM_BLOCK_STATUS_IDLE;

        ADDR_TO_MEM(cursor + block_size, cursor + block_offset);
        cursor = BYTE_OFFSET(cursor, block_offset);
    }

    /* 处理最后一个内存块 */
    block = (MEM_BLOCK *)cursor;
    block->page = page;
    block->status = MEM_BLOCK_STATUS_IDLE;
}

void mem_page_terminate(MEM_PAGE *page)
{
    unsigned char *pt = NULL;
    int size = 0;

    /* 计算内存页除头部以外的总大小（字节） */
    size = page->block_num * (page->block_data + page->block_head);
    pt = BYTE_OFFSET(page, sizeof(MEM_PAGE));

    /* 清除内存块 */
    memset(pt, 0, size);

    /* 最后清空头部 */
    memset(page, 0, sizeof(MEM_PAGE));
}

MEM_BLOCK *get_block(void *address, int dbg)
{
    unsigned char *pt = NULL;
    MEM_BLOCK *ret = NULL;

    int block_size = dbg ? sizeof(MEM_BLOCK_DBG) : sizeof(MEM_BLOCK);

    if (!address) {
        return NULL;
    }

    pt = (unsigned char *)address;
    pt = BYTE_REOFFSET(pt, block_size);

    ret = (MEM_BLOCK *)pt;
    return ret;
}

void pad_dbg_block(MEM_BLOCK_DBG *block, const char *func, const char *file, int line)
{
    const char *str = NULL;

    if (!block) {
        return;
    }

    if (!func && !file && !line) {
        block->line = 0;
        block->thread = 0;

        memset(block->date, INIT_BLOCK_PADDING, DATE_INFO_LENGTH);
        memset(block->file, INIT_BLOCK_PADDING, FILE_INFO_LENGTH);
        memset(block->func, INIT_BLOCK_PADDING, FUNC_INFO_LENGTH);
    } else {
        block->line = line;
        block->thread = (unsigned long long)THREAD_SELF;

        get_curtime("%Y-%m-%d %H:%M:%S", block->date, DATE_INFO_LENGTH);

        if (file[0]) {
            str = strrchr(file, CH_SEP);
            str = str ? (str + 1) : file;

            strncpy(block->file, str, FILE_INFO_LENGTH);
        }

        if (func[0]) {
            strncpy(block->func, func, FUNC_INFO_LENGTH);
        }

        block->date[DATE_INFO_LENGTH - 1] = '\0';
        block->file[FILE_INFO_LENGTH - 1] = '\0';
        block->func[FUNC_INFO_LENGTH - 1] = '\0';
    }
}

const char *get_page_name(unsigned char type)
{
    static char buff[32] = { 0 };

    switch (type) {
    case MEM_PAGE_TYPE_ZERO:  strcpy(buff, "MEM_PAGE_TYPE_ZERO");  break;
    case MEM_PAGE_TYPE_1K:    strcpy(buff, "MEM_PAGE_TYPE_1K");    break;
    case MEM_PAGE_TYPE_2K:    strcpy(buff, "MEM_PAGE_TYPE_2K");    break;
    case MEM_PAGE_TYPE_4K:    strcpy(buff, "MEM_PAGE_TYPE_4K");    break;
    case MEM_PAGE_TYPE_LARGE: strcpy(buff, "MEM_PAGE_TYPE_LARGE"); break;
    }

    return buff;
}

const char *get_status_name(unsigned char status)
{
    static char buff[32] = { 0 };

    switch (status) {
    case MEM_PAGE_STATUS_IDLE:  strcpy(buff, "MEM_PAGE_STATUS_IDLE");  break;
    case MEM_PAGE_STATUS_USING: strcpy(buff, "MEM_PAGE_STATUS_USING"); break;
    case MEM_PAGE_STATUS_FULL:  strcpy(buff, "MEM_PAGE_STATUS_FULL");  break;
    }

    return buff;
}

const char *get_block_status_name(int status)
{
    static char buff[32] = { 0 };

    switch (status) {
    case MEM_BLOCK_STATUS_IDLE:  strcpy(buff, "MEM_BLOCK_STATUS_IDLE");  break;
    case MEM_BLOCK_STATUS_USING: strcpy(buff, "MEM_BLOCK_STATUS_USING"); break;
    }

    return buff;
}

int print_leak_info(MEM_PAGE *page, int dbg, char *buff)
{
    int i;

    unsigned char *cursor = NULL;
    MEM_BLOCK *block = NULL;
    MEM_BLOCK_DBG *block_dbg = NULL;
    int offset = 0;
    int count = 0;

    if (!page || !buff) {
        return 0;
    }

    count = page->block_num;
    block = (MEM_BLOCK *)((unsigned char *)page + sizeof(MEM_PAGE));
    offset = page->block_head + page->block_data; 
    cursor = (unsigned char *)block;

    sprintf(buff, "page %p:\n", page);
    output_mem_info_std(buff);

    if (dbg) {
        for (i = 0; i < count; i++) {
            block_dbg = (MEM_BLOCK_DBG *)cursor;

            if (block_dbg->status == MEM_BLOCK_STATUS_USING) {
                sprintf(buff, "--- block[%d] block size = %d ---\n", i, offset);
                output_mem_info_std(buff);

                sprintf(buff, "    time = %s\n",   block_dbg->date);
                output_mem_info_std(buff);

                sprintf(buff, "    file = %s\n",   block_dbg->file);
                output_mem_info_std(buff);

                sprintf(buff, "    line = %d\n",   block_dbg->line);
                output_mem_info_std(buff);

                sprintf(buff, "    func = %s\n",   block_dbg->func);
                output_mem_info_std(buff);

                sprintf(buff, "    tid  = 0x%llX\n", block_dbg->thread);
                output_mem_info_std(buff);
            }

            cursor += offset;
        }
    } else {
        for (i = 0; i < count; i++) {
            block = (MEM_BLOCK *)cursor;

            if (block->status == MEM_BLOCK_STATUS_USING) {
                sprintf(buff, "--- block[%d] block size = %d ---\n", i, offset);
                output_mem_info_std(buff);
            }

            cursor += offset;
        }
    }

    sprintf(buff, "--- allocated size = %d byte ---\n", page->alloc_size);
    output_mem_info_std(buff);

    return page->alloc_size;
}

void print_link_info(MEM_PAGE_LINK *link, int index, char *buff)
{
    if (!link || !buff) {
        return;
    }

    if (index > MEM_PAGE_BLOCK_INFO_COUNT - 1) {
        return;
    }

    sprintf(buff, "%s page link:\n", get_page_name(mem_page_info_list[index].page_type));
    output_mem_info_std(buff);

    sprintf(buff, "count      = %d\n", link->count);
    output_mem_info_std(buff);

    sprintf(buff, "ilde_num   = %d\n", link->idle_num);
    output_mem_info_std(buff);

    sprintf(buff, "heade      = %p\n", link->head);
    output_mem_info_std(buff);

    sprintf(buff, "tail       = %p\n", link->tail);
    output_mem_info_std(buff);
}

void print_page_info(MEM_PAGE *page, char *buff)
{
    if (!page || !buff) {
        return;
    }

    sprintf(buff, "page %p status      = %s\n", page, get_status_name(page->status));
    output_mem_info_std(buff);

    sprintf(buff, "page %p using_count = %d\n", page, (int)page->using_count);
    output_mem_info_std(buff);

    sprintf(buff, "page %p block_num   = %d\n", page, (int)page->block_num);
    output_mem_info_std(buff);

    sprintf(buff, "page %p block_head  = %d\n", page, (int)page->block_head);
    output_mem_info_std(buff);

    sprintf(buff, "page %p block_data  = %d\n", page, (int)page->block_data);
    output_mem_info_std(buff);

    sprintf(buff, "page %p alloc_size  = %d\n", page, (int)page->alloc_size);
    output_mem_info_std(buff);

    sprintf(buff, "page %p idle        = %p\n", page, page->idle);
    output_mem_info_std(buff);

    sprintf(buff, "page %p next_page   = %p\n", page, page->next);
    output_mem_info_std(buff);
}

void output_mem_info_std(const char *info)
{
    printf("%s", info);
}

const char *format_mem_info(char *buff, int size, const char *info, ...)
{
    va_list args;

    if (buff) {
        va_start(args, info);
        vsnprintf(buff, (int)size, info, args);
        va_end(args);
    }
    return buff;
}

int get_curtime(const char *format, char *buf, int len)
{
    time_t lc_time = 0;
    size_t ret = 0;
    struct tm *lctm = NULL;

    if (!format || !format[0]) {
        return MEM_FAILED;
    }

    if (!buf) {
        return MEM_FAILED;
    }

    if (len <= 0) {
        return MEM_FAILED;
    }

    memset(buf, 0, len);

    /* 获取本地时间 */
    lc_time = time(NULL);
    lctm = localtime(&lc_time);

    if (!lctm) {
        return MEM_FAILED;
    }

    /* 转换时间信息为字符串 */
    ret = strftime(buf, len, format, lctm);
    return !ret ? MEM_FAILED : MEM_SUCCESS;
}
/*===========================================================================*/
