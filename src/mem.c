#include <string.h>
#include <stdlib.h>

#include "mem.h"
#include "mem_page.h"

/*===========================================================================*/

#if defined(WIN32)
#include <windows.h>
#else /* Linux */
#include <pthread.h>
#endif /* WIN32 & Linux */

#if defined(WIN32)
typedef HANDLE MUTEX_HANDLE;
#else
typedef pthread_mutex_t MUTEX_HANDLE;
#endif /* WIN32 & Linux */

static int create_mutex(MUTEX_HANDLE *handle) 
{
#if defined(WIN32)
    MUTEX_HANDLE mutex;

    if (!handle) {
        return MEM_FAILED;
    }

    /* 创建一个互斥量 */
    mutex = CreateMutex(NULL, 0, NULL);
    if (!mutex) {
        return MEM_FAILED;
    }

    *handle = mutex;

#else /* Linux */
    if (!handle) {
        return MEM_FAILED;
    }

    if (pthread_mutex_init(handle, NULL)) {
        return MEM_FAILED;
    }
#endif /* WIN32 & Linux */
    return MEM_SUCCESS;
}

static int destroy_mutex(MUTEX_HANDLE *handle)
{
#if defined(WIN32)
    if (!handle || !CloseHandle(*handle)) {
        return MEM_FAILED;
    }

    *handle = NULL;
#else /* Linux */
    if (!handle || pthread_mutex_destroy(handle)) {
        return MEM_FAILED;
    }
#endif /* WIN32 & Linux */
    return MEM_SUCCESS;
}

#if defined(WIN32)
#define MEM_LOCK(lock) WaitForSingleObject((lock), INFINITE)
#define MEM_UNLOCK(lock) ReleaseMutex((lock))
#else
#define MEM_LOCK(lock) pthread_mutex_lock(&(lock))
#define MEM_UNLOCK(lock) pthread_mutex_unlock(&(lock))
#endif

/*===========================================================================*/

/* 内存互斥锁 */
static MUTEX_HANDLE mem_lock;

void create_res() 
{
    create_mutex(&mem_lock);
}

void clear_res()
{
    MEM_LOCK(mem_lock);
    clear_mem_pages();
    MEM_UNLOCK(mem_lock);

    destroy_mutex(&mem_lock);
}

void *mem_malloc(size_t len)
{
    unsigned char *ret = NULL;
    int index = get_page_index(len);

    MEM_LOCK(mem_lock);

    /* 获取空闲内存页地址 */
    if (!usable_page_exist(index)) {
        /* 新分配一个空闲页 */
        mem_page_malloc(index, 0);
    }

    /* 获取空闲内存块 */
    ret = alloc_block(len);

    MEM_UNLOCK(mem_lock);
    return ret;
}

void *mem_realloc(void *ptr, size_t len)
{
    int size  = 0;
    int index = 0;
    int index_new = 0;
    int dst_size  = 0;

    unsigned char *ret = NULL;

    if (!ptr) {
        return NULL;
    }

    size = get_addr_block_len(ptr, 0);
    index = get_page_index(size);
    index_new = get_page_index(len);

    if (size < (int)len || index > index_new) {
        MEM_LOCK(mem_lock);

        /* 变更内存页 */
        if (!usable_page_exist(index_new)) {
            /* 新分配一个空闲页 */
            mem_page_malloc(index_new, 0);
        }

        /* 获取空闲内存块 */
        ret = alloc_block(len);
        dst_size = (int)((size < (int)len) ? size : len);

        if (ret) {
            /* 内存拷贝 */
            memcpy(ret, ptr, dst_size);

            /* 释放原始的内存块 */
            free_block(ptr, 0);
        }

        MEM_UNLOCK(mem_lock);
        return ret;
    }

    if (index == index_new) {
        return ptr;
    }

    return NULL;
}

void mem_free(void *ptr)
{
    if (!ptr) {
        return;
    }

    MEM_LOCK(mem_lock);
    free_block(ptr, 0);
    MEM_UNLOCK(mem_lock);
}

void *mem_dbg_malloc(size_t len, const char *func, const char *file, int line)
{
    unsigned char *ret  = NULL;
    int index = get_page_index(len);
    
    MEM_LOCK(mem_lock);

    /* 获取空闲内存页地址 */
    if (!usable_page_exist(index)) {
        /* 新分配一个空闲页 */
        mem_page_malloc(index, 1);
    }

    /* 获取空闲内存块 */
    ret = alloc_block_dbg(len, func, file, line);

    MEM_UNLOCK(mem_lock);
    return ret;
}

void *mem_dbg_realloc(void *ptr, size_t len, const char *func, const char *file, int line)
{
    int size  = 0;
    int index = 0;
    int index_new = 0;
    int dst_size  = 0;

    unsigned char *ret = NULL;

    if (!ptr) {
        return NULL;
    }

    size = get_addr_block_len(ptr, 1);
    index = get_page_index(size);
    index_new = get_page_index(len);

    if (size < (int)len || index > index_new) {
        MEM_LOCK(mem_lock);

        /* 变更内存页 */
        if (!usable_page_exist(index_new)) {
            /* 新分配一个空闲页 */
            mem_page_malloc(index_new, 1);
        }

        /* 获取空闲内存块 */
        ret = alloc_block_dbg(len, func, file, line);

        dst_size = (int)((size < (int)len) ? size : len);

        if (ret) {
            /* 内存拷贝 */
            memcpy(ret, ptr, dst_size);

            /* 释放原始的内存块 */
            free_block(ptr, 1);
        }

        MEM_UNLOCK(mem_lock);
        return ret;
    }

    if (index == index_new) {
        return ptr;
    }

    return NULL;
}

void *mem_dbg_calloc(size_t num, size_t size, const char *func, const char *file, int line)
{
    size_t len = num * size;
    unsigned char *ret = NULL;
    int index = get_page_index(len);

    MEM_LOCK(mem_lock);

    /* 获取空闲内存页地址 */
    if (!usable_page_exist(index)) {
        /* 新分配一个空闲页 */
        mem_page_malloc(index, 1);
    }

    /* 获取空闲内存块 */
    ret = alloc_block_dbg(len, func, file, line);

    MEM_UNLOCK(mem_lock);
    return ret;
}

void mem_dbg_free(void *ptr)
{
    if (!ptr) {
        return;
    }

    MEM_LOCK(mem_lock);
    free_block(ptr, 1);
    MEM_UNLOCK(mem_lock);
}

void mem_clear(void *ptr, size_t len)
{
#ifdef WIN32
    if (ptr) {
        SecureZeroMemory(ptr, len);
    }
#else /* Linux */
    volatile unsigned char *p =
        (volatile unsigned char *)ptr;

    if (p) {
        while (len-- > 0) {
            *p++ = 0;
        }
    }
#endif /* WIN32 & Linux */
}

void mem_print_info()
{
    page_print_basic_info(0);
}

void mem_dbg_print_info()
{
    page_print_basic_info(1);
}

void mem_print_block_list(size_t len)
{
    int index = get_page_index(len);
    page_print_block_list(index, 0);
}

void mem_dbg_print_block_list(size_t len)
{
    int index = get_page_index(len);
    page_print_block_list(index, 1);
}

void mem_print_leak_info()
{
    page_print_allocated_info(0);
}

void mem_dbg_print_leak_info()
{
    page_print_allocated_info(1);
}

/*===========================================================================*/
