/* main.c源码 */
#define _XOPEN_SOURCE 700

#define FUSE_USE_VERSION 26
#include <stdio.h>
#include <string.h>
#include <fuse.h>
#include "../include/ddriver.h"
#include <linux/fs.h>
#include <pwd.h>

#define DEMO_DEFAULT_PERM        0777


/* 超级块 */
struct demo_super
{
    int     driver_fd;  /* 模拟磁盘的fd */

    int     sz_io;      /* 磁盘IO大小，单位B */  // 一个逻辑块是两个IO块大小（和任务二一致）
    int     sz_disk;    /* 磁盘容量大小，单位B */
    int     sz_blks;    /* 逻辑块大小，单位B */
};

/* 目录项 */
struct demo_dentry
{
    char    fname[128];
}; 

struct demo_super super;

#define DEVICE_NAME "ddriver"

/* 挂载文件系统 */
static int demo_mount(struct fuse_conn_info * conn_info){
    // 打开驱动
    char device_path[128] = {0};
    sprintf(device_path, "%s/" DEVICE_NAME, getpwuid(getuid())->pw_dir);
    super.driver_fd = ddriver_open(device_path);

    printf("super.driver_fd: %d\n", super.driver_fd);

    uint64_t sz_io, sz_disk;
    if (ddriver_ioctl(super.driver_fd, IOC_REQ_DEVICE_IO_SZ, &sz_io) 
        || ddriver_ioctl(super.driver_fd, IOC_REQ_DEVICE_IO_SZ, &sz_disk)) {
        printf("Fail to get disk info!\n");
        return 1;
    }

    /* 填充super信息 */
    super.sz_io = sz_io;
    super.sz_disk = sz_disk;
    super.sz_blks = 2 * sz_io;

    return 0;
}

/* 卸载文件系统 */
static int demo_umount(void* p){
    // 关闭驱动
    ddriver_close(super.driver_fd);
    return 0;
}

/* 遍历目录 */
static int demo_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
    // 此处任务一同学无需关注demo_readdir的传入参数，也不要使用到上述参数

    char filename[128]; // 待填充的
    char tmp_blk[512];

    /* 根据超级块的信息，从第500逻辑块读取一个dentry，ls将只固定显示这个文件名 */
    ddriver_seek(super.driver_fd, super.sz_blks * (500), SEEK_SET);
    ddriver_read(super.driver_fd, tmp_blk, super.sz_io);
    memcpy(filename, tmp_blk, sizeof(struct demo_dentry));
    printf("%s\n", filename);

    /* TODO: 计算磁盘偏移off，并根据磁盘偏移off调用ddriver_seek移动磁盘头到磁盘偏移off处 */

    /* TODO: 调用ddriver_read读出一个磁盘块到内存，512B */

    /* TODO: 使用memcpy拷贝上述512B的前sizeof(demo_dentry)字节构建一个demo_dentry结构 */

    /* TODO: 填充filename */

    // 此处大家先不关注filler，已经帮同学写好，同学填充好filename即可
    return filler(buf, filename, NULL, 0);
}

/* 显示文件属性 */
static int demo_getattr(const char* path, struct stat *stbuf)
{
    if(strcmp(path, "/") == 0)
        stbuf->st_mode = DEMO_DEFAULT_PERM | S_IFDIR;            // 根目录是目录文件
    else
        stbuf->st_mode = DEMO_DEFAULT_PERM | S_IFREG;            // 该文件显示为普通文件
    return 0;
}

/* 根据任务1需求 只用实现前四个钩子函数即可完成ls操作 */
static struct fuse_operations ops = {
	.init = demo_mount,						          /* mount文件系统 */		
	.destroy = demo_umount,							  /* umount文件系统 */
	.getattr = demo_getattr,							  /* 获取文件属性 */
	.readdir = demo_readdir,							  /* 填充dentrys */
};

int main(int argc, char *argv[])
{
    int ret = 0;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    ret = fuse_main(argc, argv, &ops, NULL);
    return ret;
}
