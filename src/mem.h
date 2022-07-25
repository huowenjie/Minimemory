#ifndef __MEM_H__
#define __MEM_H__

#include <stddef.h>
#include <stdlib.h>

/*===========================================================================*/
/* 内存追踪 */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MEM_START create_res()
#define MEM_END clear_res()

/* 内存追踪通用宏 */
#ifdef USE_MEMORY
    #ifdef DEBUG
        #define MEM_MALLOC(len) mem_dbg_malloc((len), __FUNCTION__, __FILE__, __LINE__)
        #define MEM_REALLOC(p, len) mem_dbg_realloc((p), (len), __FUNCTION__, __FILE__, __LINE__)
        #define IDLE_MEM_FREE(p) mem_dbg_free(p)

        #define PRINT_MEM_INFO mem_dbg_print_info(0)
        #define PRINT_BLOCK_LIST(len) mem_dbg_print_block_list(0, (len))
        #define PRINT_LEAK_INFO mem_dbg_print_leak_info(0)
    #else
        #define MEM_MALLOC(len) mem_malloc(len)
        #define MEM_REALLOC(p, len) mem_realloc((p), (len))
        #define IDLE_MEM_FREE(p) mem_free(p)

        #define PRINT_MEM_INFO mem_print_info(0)
        #define PRINT_BLOCK_LIST(len) mem_print_block_list(0, (len))
        #define PRINT_LEAK_INFO mem_print_leak_info(0)
    #endif /* DEBUG */

    #define CLEAR_RES clear_res()
#else
    #define MEM_MALLOC(len) malloc(len)
    #define MEM_REALLOC(p, len) realloc((p), (len))
    #define IDLE_MEM_FREE(p) free(p)

    #define PRINT_MEM_INFO
    #define PRINT_BLOCK_LIST
    #define PRINT_LEAK_INFO

    #define CLEAR_RES
#endif /* USE_MEMORY */

#define MEM_CLEAR(p, len) mem_clear((p), (len))
#define MEM_CLEAR_FREE(p, len) (MEM_CLEAR((p), (len)), MEM_FREE((p)))

/* 初始化内存资源 */
void create_res();

/* 销毁内存资源 */
void clear_res();

/* 通用内存管理函数 */
void *mem_malloc(size_t len);
void *mem_realloc(void *ptr, size_t len);
void  mem_free(void *ptr);

/* 携带调试的内存管理函数 */
void *mem_dbg_malloc(size_t len, const char *func, const char *file, int line);
void *mem_dbg_realloc(void *ptr, size_t len, const char *func, const char *file, int line);
void  mem_dbg_free(void *ptr);

/* 内存擦除 */
void mem_clear(void *ptr, size_t len);

/* 打印内存信息 */
void mem_print_info();
void mem_dbg_print_info();

/* 打印内存块信息 */
void mem_print_block_list(size_t len);
void mem_dbg_print_block_list(size_t len);

/* 打印泄漏信息 */
void mem_print_leak_info();
void mem_dbg_print_leak_info();

#ifdef __cplusplus
}
#endif /* __cplusplus */

/*===========================================================================*/

#endif /* __MEM_H__ */
