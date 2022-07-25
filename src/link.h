#ifndef __LINK_H__
#define __LINK_H__

/*===========================================================================*/
/* 无锁双向链表 */
/*===========================================================================*/

#define LINK_SUCCESS 0
#define LINK_FAILED -1

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct link_st LINK;
typedef struct link_node_st LINK_NODE;

/* 双向链表主体结构 */
struct link_st {
    LINK_NODE *head; /* 头节点 */
    LINK_NODE *tail; /* 尾节点 */

    int count;       /* 节点总数 */
};

/* 链表节点 */
struct link_node_st {
    LINK_NODE *prev;
    LINK_NODE *next;
};

/* 链表还原 */
void link_reset(LINK *link);

/* 将节点接入尾部 */
int link_push(LINK *link, LINK_NODE *node);

/* 按索引插入节点，如 0 则插在第一个，依次类推，查询节点复杂度为 O(n) */
int link_insert(LINK *link, int index, LINK_NODE *node);

/* 在目标节点之前插入节点, target 不能为空 */
int link_insert_before(LINK *link, LINK_NODE *target, LINK_NODE *node);

/* 在目标节点之后插入节点, target 不能为空 */
int link_insert_after(LINK *link, LINK_NODE *target, LINK_NODE *node);

/* 移除尾部节点，同时返回节点地址 */
LINK_NODE *link_pop(LINK *link);

/* 根据索引移除节点，查询节点复杂度为 O(n) */
LINK_NODE *link_remove(LINK *link, int index);

/* 移除目标节点, 移除成功返回该节点, 移除时会先查询节点是否存在 */
LINK_NODE *link_remove_node(LINK *link, LINK_NODE *target);

/* 
 * 强制移除目标节点, 移除成功返回该节点, 移除时不检查节点是否存在，
 * 算法复杂度为 O(1) ；
 *
 * 调用本函数定要万分小心，确保 target 属于 link。
 */
LINK_NODE *link_remove_force(LINK *link, LINK_NODE *target);

#ifdef __cplusplus
}
#endif /* __cplusplus */

/*===========================================================================*/

#endif /* __LINK_H__ */
