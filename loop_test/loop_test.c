/*
 * loop_test.c - 长时间运行的测试程序，用于 gcore 演示
 * 模拟一个持续运行的后台服务，含多线程和动态数据
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define BUF_SIZE 256

/* 模拟全局状态 */
static int g_counter = 0;
static char g_message[BUF_SIZE] = "initial state";
static int g_running = 1;

/* 模拟工作数据 */
typedef struct {
    int id;
    int value;
    char name[64];
} work_item_t;

static work_item_t g_work_queue[8];
static int g_queue_len = 0;

/* 工作线程 */
static void *worker_thread(void *arg)
{
    int thread_id = *(int *)arg;
    while (g_running) {
        sleep(2);
        g_counter++;
        snprintf(g_message, BUF_SIZE,
                 "thread_%d working, counter=%d", thread_id, g_counter);
    }
    return NULL;
}

/* 主线程: 持续处理 */
int main(void)
{
    pthread_t tid1, tid2;
    int id1 = 1, id2 = 2;

    /* 初始化工作队列 */
    for (int i = 0; i < 5; i++) {
        g_work_queue[i].id = i;
        g_work_queue[i].value = i * 100;
        snprintf(g_work_queue[i].name, 64, "item_%d", i);
        g_queue_len++;
    }

    /* 启动工作线程 */
    pthread_create(&tid1, NULL, worker_thread, &id1);
    pthread_create(&tid2, NULL, worker_thread, &id2);

    printf("loop_test running (PID=%d), press Ctrl+C to stop\n", getpid());
    printf("  g_counter: %d\n", g_counter);
    printf("  g_message: %s\n", g_message);
    printf("  g_queue_len: %d\n", g_queue_len);

    /* 主循环 */
    while (g_running) {
        sleep(5);
        printf("[main] counter=%d, message=%s, queue_len=%d\n",
               g_counter, g_message, g_queue_len);
    }

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    return 0;
}
