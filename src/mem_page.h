#ifndef __MEM_PAGE_H__
#define __MEM_PAGE_H__

/*===========================================================================*/
/* 内存页 */
/*===========================================================================*/

#define MEM_SUCCESS 0
#define MEM_FAILED -1

/* 内存页规格 */
#define MEM_PAGE_TYPE_ZERO          0    /* 管理总容量为 0k 内存块的内存页 */
#define MEM_PAGE_TYPE_1K            1    /* 管理总容量为 1k 内存块的内存页 */
#define MEM_PAGE_TYPE_2K            2    /* 管理总容量为 2k 内存块的内存页 */
#define MEM_PAGE_TYPE_4K            3    /* 管理总容量为 4k 内存块的内存页 */
#define MEM_PAGE_TYPE_LARGE         4    /* 管理单个内存块较大的的内存页 */

/* 内存页状态 */
#define MEM_PAGE_STATUS_IDLE        0    /* 内存页完全空闲 */
#define MEM_PAGE_STATUS_USING       1    /* 内存页正在使用 */
#define MEM_PAGE_STATUS_FULL        2    /* 内存页已满 */

/* 内存块状态 */
#define MEM_BLOCK_STATUS_IDLE       0    /* 内存块空闲 */
#define MEM_BLOCK_STATUS_USING      1    /* 内存块被占用 */

typedef struct mem_page_st          MEM_PAGE;
typedef struct mem_block_st         MEM_BLOCK;
typedef struct mem_block_dbg_st     MEM_BLOCK_DBG;
typedef struct mem_page_link_st     MEM_PAGE_LINK;

/*-------------------------------------------------------*/

/* 获取分页索引 */
int get_page_index(size_t len);

/* 通过内存页获取索引 */
int get_page_index_ex(MEM_PAGE *page);

/* 是否存在可用页面 */
int usable_page_exist(int index);

/* 创建一个内存页 */
int mem_page_malloc(int index, int dbg);

/* 释放一个内存页，如果内存链表没有 idle 状态的内存页，返回相应的错误码 */
int mem_page_free(MEM_PAGE *page);

/* 清理内存页 */
void clear_mem_pages();

/* 从内存页分配一个空闲内存块 */
void *alloc_block(size_t len);
void *alloc_block_dbg(size_t len, const char *func, const char *file, int line);

/* 释放内存块 */
void free_block(void *address, int dbg);

/* 打印基本内存信息 */
void page_print_basic_info(int dbg);

/* 打印内存块列表 */
void page_print_block_list(int index, int dbg);

/* 打印已分配的内存信息 */
void page_print_allocated_info(int dbg);

/*-------------------------------------------------------*/
/* 内存结构信息 */

/* 获取所属地址内存块的长度, 不含头部 */
int get_addr_block_len(void *ptr, int dbg);

/*===========================================================================*/

#endif /* __MEM_PAGE_H__ */
